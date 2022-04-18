#ifndef _GNU_SOURCE
#define _GNU_SOURCE     /* Needed for PTHREAD_PRIO_INHERIT */
#endif
#include <pthread.h>

#include <sys/param.h>  /* roundup */
#include <stdlib.h>     /* malloc, calloc, free, posix_memalign, realloc */
#include <malloc.h>     /* memalign */
#include <string.h>     /* memset */
#include <stdio.h>      /* snprintf, vsnprintf */
#include <stdarg.h>

#include <libvapi/vmem.h>
#include <libvapi/vtimer.h>
//#include "vmem_internal.h"
#include "vmem_pool.h"
#include "vlog_vapi.h"

/*****************************************************************************/

/*
 * Set if wrapmalloc is built into libvapi.
 * Unset otherwise.
 */
#define WRAPMALLOC_BUILTIN_ 0

#if WRAPMALLOC_BUILTIN_

/*
 * Include wrapmalloc.h.
 * Do not provide stubs for wrapmalloc's export functions.
 */
#   include "wrapmalloc.h"

#else   /* if WRAPMALLOC_BUILTIN_ == 0 */

/*
 * Provide stubs for wrapmalloc's export functions.
 * Preloading libwrapmalloc.so will override these stubs.
 */
#   define WRAPMALLOC_TRACE_ENABLE (0x0001)
#   define WRAPMALLOC_ERROR_ENABLE (0x0002)

unsigned wrapmalloc_set_options(unsigned options)
{
    return 0;
}

typedef void (*wrapmalloc_cb_t)(const char *string);

wrapmalloc_cb_t wrapmalloc_install_trace_callback(wrapmalloc_cb_t log_cb)
{
    return NULL;
}
wrapmalloc_cb_t wrapmalloc_install_error_callback(wrapmalloc_cb_t err_cb)
{
    return NULL;
}

#endif  /* WRAPMALLOC_BUILTIN_ */

/*****************************************************************************/

/*
 * No priority inheritance on the global lock since it is only used during the creation
 * of the allocators.
 */
static pthread_mutex_t chain_lock_ = PTHREAD_MUTEX_INITIALIZER;

/*
 * Global lock for allocators used to protect setting options and installing callback functions.
 * Even if an allocator is dedicated to a single thread, options and callbacks can be changed
 * from another thread as well (e.g. from the Trace and Debug thread).
 * Since those operations are typically low frequent and done from Trace and Debug context,
 * we can aford to only use one global lock (without priority inheritance) for all allocators
 * (instead of a dedicated one per allocator).
 */
static pthread_mutex_t alloc_lock_ = PTHREAD_MUTEX_INITIALIZER;

typedef struct allocator {
    /*
     * Allocator chain (constant after allocator creation).
     */
    struct allocator *next;         /* Protected by chain_lock_. */

    /*
     * Allocator related members (run-time changeable).
     */
    pthread_mutex_t *alloc_lock;    /* Mandatory lock to protect the allocator related members. */
    vmem_options_t options;         /* Run-time changeable options. */
    vmem_cb_t log_cb;               /* Trace callback function. */
    vmem_cb_t err_cb;               /* Error callback function. */

    /*
     * Pool related members (constant after allocator creation).
     */
    vmem_locktype_t lock_type;      /* Pool lock type, if any. */
    void *pool_lock;                /* Optional lock to protect a thread-safe pool. */
    vmem_pool_t pool;               /* Optional pool. */
}
allocator_t;

static allocator_t alloc_sys_ = {
    .next = NULL,

    .alloc_lock = &alloc_lock_,
    .options = 0u,
    .log_cb = NULL,
    .err_cb = NULL,

    .lock_type = vmem_locktype_none,
    .pool_lock = NULL,
    .pool = NULL
};

static allocator_t alloc_def_ = {
    .next = &alloc_sys_,

    .alloc_lock = &alloc_lock_,
    .options = 0u,
    .log_cb = NULL,
    .err_cb = NULL,

    .lock_type = vmem_locktype_none,
    .pool_lock = NULL,
    .pool = NULL
};

static allocator_t *chain_head_ = &alloc_def_;

static vmem_cb_t default_log_cb_ = NULL;
static vmem_cb_t default_err_cb_ = NULL;

static inline void chain_lock(void)
{
    pthread_mutex_lock(&chain_lock_);
}
static inline void chain_unlock(void)
{
    pthread_mutex_unlock(&chain_lock_);
}

static inline void alloc_lock(const allocator_t *alloc)
{
    pthread_mutex_lock(alloc->alloc_lock);
}
static inline void alloc_unlock(const allocator_t *alloc)
{
    pthread_mutex_unlock(alloc->alloc_lock);
}

static inline void pool_lock(const allocator_t *alloc)
{
    if (alloc->lock_type == vmem_locktype_mutex)
        pthread_mutex_lock((pthread_mutex_t *) alloc->pool_lock);
}

static inline void pool_unlock(const allocator_t *alloc)
{
    if (alloc->lock_type == vmem_locktype_mutex)
        pthread_mutex_unlock((pthread_mutex_t *) alloc->pool_lock);
}

static const char *get_lock_str(vmem_locktype_t lock_type)
{
    const char *none_str = "NONE";
    const char *mutex_str = "MUTEX";

    if (lock_type == vmem_locktype_mutex)
        return mutex_str;
    else
        return none_str;
}

static const char *get_alloc_str(const allocator_t *alloc)
{
    const char *sys_str = "SYSTEM";
    const char *def_str = "DEFAULT";
    const char *pool_str = "POOL";

    if (alloc == &alloc_sys_)
        return sys_str;
    else if (alloc == &alloc_def_)
        return def_str;
    else
        return pool_str;
}

#define invoke_cb_cond(alloc, error, format, ...) \
    if (invoke_cb_needed((alloc), (error))) \
        invoke_cb_core((alloc), (error), (format), __VA_ARGS__)

static inline int invoke_cb_needed(const allocator_t *alloc, int error)
{
    int err = (alloc->options & vmem_options_error);
    int log = (alloc->options & vmem_options_trace);

    return ((error && err && alloc->err_cb) || (log && alloc->log_cb));
}

static void invoke_cb_core(const allocator_t *alloc, int error, const char *format, ...)
__attribute__((format(printf, 3, 4)));

static void invoke_cb_core(const allocator_t *alloc, int error, const char *format, ...)
{
    va_list ap;
    char string[128];

    snprintf(string, sizeof(string), "<%s>::", get_alloc_str(alloc));

    va_start(ap, format);
    vsnprintf(string + strlen(string), sizeof(string) - strlen(string), format, ap);
    va_end(ap);

    int err = (alloc->options & vmem_options_error);
    vmem_cb_t err_cb = alloc->err_cb;
    if (error && err && err_cb)
        (*err_cb)(string);

    int log = (alloc->options & vmem_options_trace);
    vmem_cb_t log_cb = alloc->log_cb;
    if (log && log_cb)
        (*log_cb)(string);
}

static void malloc_trim_cb(vtimer_t timer, void *ctx)
{
    int rc = 0;

    rc = vmem_malloc_trim();
    if (rc == 1) {
        vapi_debug("Heap memory trimmed successfully");
    }
}

/*
 * Return the system allocator reference.
 */
vmem_alloc_t vmem_alloc_system(void)
{
    return (vmem_alloc_t) &alloc_sys_;
}

/*
 * Return the default allocator reference.
 */
vmem_alloc_t vmem_alloc_default(void)
{
    return (vmem_alloc_t) &alloc_def_;
}

/*
 * Create a pool allocator with a pool of a number of elements of the same size.
 */
vmem_alloc_t vmem_alloc_create_pool(size_t size, ulong_t nr_elem, vmem_locktype_t lock_type)
{
    /* Create allocator. */
    allocator_t *alloc = (allocator_t *) calloc(1, sizeof(allocator_t));

    /* Create and initialize pool. */
    if (alloc) {
        alloc->pool = vmem_pool_create(size, nr_elem);

        if (alloc->pool == NULL) {
            free(alloc);
            alloc = NULL;
        }
    }

    /* Create and initialize pool lock. */
    if (alloc) {
        if (lock_type == vmem_locktype_none) {
            alloc->lock_type = vmem_locktype_none;
            alloc->pool_lock = NULL;
        } else {
            alloc->lock_type = vmem_locktype_mutex;
            alloc->pool_lock = malloc(sizeof(pthread_mutex_t));

            if (alloc->pool_lock) {
                pthread_mutexattr_t mutex_attr;

                pthread_mutexattr_init(&mutex_attr);
                /* See pthread.h and features.h */
#ifdef __USE_UNIX98
                pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
#endif
                pthread_mutex_init((pthread_mutex_t *) alloc->pool_lock, &mutex_attr);
                pthread_mutexattr_destroy(&mutex_attr);
            } else {
                free(alloc->pool);
                free(alloc);
                alloc = NULL;
            }
        }
    }

    /* Initialize allocator. */
    if (alloc) {
        alloc->alloc_lock = &alloc_lock_;
        alloc->options = 0u;

        chain_lock();

        /* Initially, install the default callback functions. */
        alloc->log_cb = default_log_cb_;
        alloc->err_cb = default_err_cb_;

        /* Add allocator to the chain of existing allocators. */
        alloc->next = chain_head_;
        chain_head_ = alloc;

        chain_unlock();
    }

    /* Try to log on system allocator (since logging cannot be enabled yet on this allocator). */
    int error = (alloc == NULL);
    invoke_cb_cond(&alloc_sys_, error, "%s(size=%zu, nr_elem=%lu, lock_type=%s) = %p",
                   __FUNCTION__, size, nr_elem, get_lock_str(lock_type), alloc);

    return (vmem_alloc_t) alloc;
}

/*
 * Delete a pool allocator with its pool of elements.
 */
vmem_error_t vmem_alloc_delete_pool(vmem_alloc_t allocator)
{
    allocator_t *alloc = (allocator_t *) allocator;
    vmem_error_t ret = vmem_error_success;

    if (alloc) {
        if (alloc->pool) {
            free(alloc->pool);
        } else {
            ret = vmem_error_failure;
        }
        if (alloc->pool_lock) {
            pthread_mutex_destroy((pthread_mutex_t *) alloc->pool_lock);
            free(alloc->pool_lock);
        }
        free(alloc);
        alloc = NULL;
    } else {
        ret = vmem_error_failure;
    }

    return ret;
}

/*
 * Allocate a piece of memory from the provided allocator.
 */
void *vmem_malloc(vmem_alloc_t allocator, size_t size)
{
    allocator_t *alloc = (allocator_t *) allocator;
    void *ptr;

    if (alloc == &alloc_sys_) {
        ptr = NULL;
    } else if (alloc == &alloc_def_) {
        ptr = malloc(size);
    } else {
        pool_lock(alloc);

        ptr = vmem_pool_alloc(alloc->pool, size);

        pool_unlock(alloc);
    }

    int error = (ptr == NULL);
    invoke_cb_cond(alloc, error, "%s(allocator=%p, size=%zu) = %p", __FUNCTION__, alloc, size, ptr);

    return ptr;
}

/*
 * Calloc memory. Can be used on all kinds of allocators.
 */
void *vmem_calloc(vmem_alloc_t allocator, size_t size)
{
    allocator_t *alloc = (allocator_t *) allocator;
    void *ptr;

    if (alloc == &alloc_sys_) {
        ptr = NULL;
    } else if (alloc == &alloc_def_) {
        ptr = calloc(1, size);
    } else {
        pool_lock(alloc);

        ptr = vmem_pool_alloc(alloc->pool, size);

        pool_unlock(alloc);

        memset(ptr, 0, size);
    }

    int error = (ptr == NULL);
    invoke_cb_cond(alloc, error, "%s(allocator=%p, size=%zu) = %p", __FUNCTION__, alloc, size, ptr);

    return ptr;
}

/*
 * Free previously allocated memory.
 */
void *vmem_free(vmem_alloc_t allocator, void *ptr)
{
    allocator_t *alloc = (allocator_t *) allocator;
    void *ret_ptr;

    /* Log first (no error info): deallocation of bad pointer may cause termination. */
    invoke_cb_cond(alloc, 0, "%s(allocator=%p, ptr=%p) = ...", __FUNCTION__, alloc, ptr);

    if (alloc == &alloc_sys_) {
        ret_ptr = (void *) 0xbad;
    } else if (alloc == &alloc_def_) {
        free(ptr);
        ret_ptr = NULL;
    } else {
        pool_lock(alloc);

        ret_ptr = vmem_pool_free(alloc->pool, ptr);

        pool_unlock(alloc);
    }

    int error = (ret_ptr != NULL);
    invoke_cb_cond(alloc, error, "%s(allocator=%p, ptr=%p) = %p", __FUNCTION__, alloc, ptr, ret_ptr);

    return ret_ptr;
}

/*
 * Align memory on certain boundary. Only supported on non-pool interfaces.
 */
void *vmem_memalign(vmem_alloc_t allocator, size_t align, size_t size)
{
    allocator_t *alloc = (allocator_t *) allocator;
    void *ptr;

    if (alloc == &alloc_sys_) {
        ptr = NULL;
    } else if (alloc == &alloc_def_) {
        /* See 'man 3 posix_memalign'. */
#if _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
        /*
         * Use posix_memalign call instead of (obsolete) memalign call
         * (because memalign might not be implemented in an 'alternative' general purpose allocator).
         *
         * posix_memalign poses an extra restriction on alignment,
         * namely that it is a multiple of sizeof(void *) as well.
         */
        if (posix_memalign(&ptr, roundup(align, sizeof(void *)), size))
            ptr = NULL;
#else
        /*
         * Ensure same behaviour as with posix_memalign above (roundup).
         */
        ptr = memalign(roundup(align, sizeof(void *)), size);
#endif
    } else {
        ptr = NULL;
    }

    int error = (ptr == NULL);
    invoke_cb_cond(alloc, error, "%s(allocator=%p, align=%zu, size=%zu) = %p", __FUNCTION__, alloc, align, size, ptr);

    return ptr;
}

/*
 * Realloc memory. Only supported on non-pool interfaces.
 */
void *vmem_realloc(vmem_alloc_t allocator, void *ptr, size_t size)
{
    allocator_t *alloc = (allocator_t *) allocator;
    void *new_ptr;

    /* Log first (no error info): deallocation of bad pointer may cause termination. */
    invoke_cb_cond(alloc, 0, "%s(allocator=%p, ptr=%p, size=%zu) = ...", __FUNCTION__, alloc, ptr, size);

    if (alloc == &alloc_sys_) {
        new_ptr = NULL;
    } else if (alloc == &alloc_def_) {
        new_ptr = realloc(ptr, size);
    } else {
        new_ptr = NULL;
    }

    int error = (new_ptr == NULL);
    invoke_cb_cond(alloc, error, "%s(allocator=%p, ptr=%p, size=%zu) = %p", __FUNCTION__, alloc, ptr, size, new_ptr);

    return new_ptr;
}

/*
 * Release free memory from the top of the heap.
 */
int vmem_malloc_trim(void)
{
    return malloc_trim(0);
}

/*
 * Set options. Can be used on all kinds of allocators.
 */
vmem_error_t vmem_setoption(vmem_alloc_t allocator, vmem_options_t options)
{
    allocator_t *alloc = (allocator_t *) allocator;

    alloc_lock(alloc);

    alloc->options = (alloc->options & ~(options >> 16)) | (options & 0xffff);

    if (alloc == &alloc_sys_) {
        /* System allocator options. */
        unsigned sys_options = 0u;

        if (alloc->options & vmem_options_error)
            sys_options |= WRAPMALLOC_ERROR_ENABLE;

        if (alloc->options & vmem_options_trace)
            sys_options |= WRAPMALLOC_TRACE_ENABLE;

        wrapmalloc_set_options(sys_options);
    }

    alloc_unlock(alloc);

    return vmem_error_success;
}

/*
 * Get options. Can be used on all kinds of allocators.
 */
vmem_error_t vmem_getoption(vmem_alloc_t allocator, vmem_options_t *options)
{
    allocator_t *alloc = (allocator_t *) allocator;

    *options = alloc->options;

    return vmem_error_success;
}

/*
 * Install/override trace callback function.
 */
vmem_cb_t vmem_install_trace_callback(vmem_alloc_t allocator, vmem_cb_t log_cb)
{
    allocator_t *alloc = (allocator_t *) allocator;

    alloc_lock(alloc);

    vmem_cb_t prev = alloc->log_cb;
    alloc->log_cb = log_cb;

    if (alloc == &alloc_sys_) {
        /* System allocator trace callback function installation. */
        wrapmalloc_install_trace_callback(log_cb);
    }

    alloc_unlock(alloc);

    return prev;
}

/*
 * Install/override error callback function.
 */
vmem_cb_t vmem_install_error_callback(vmem_alloc_t allocator, vmem_cb_t err_cb)
{
    allocator_t *alloc = (allocator_t *) allocator;

    alloc_lock(alloc);

    vmem_cb_t prev = alloc->err_cb;
    alloc->err_cb = err_cb;

    if (alloc == &alloc_sys_) {
        /* System allocator error callback function installation. */
        wrapmalloc_install_error_callback(err_cb);
    }

    alloc_unlock(alloc);

    return prev;
}

/*
 * Dump allocator info (using the specfied write callback function).
 */
void vmem_alloc_print(vmem_alloc_t allocator,
                      void (*print_cb)(void *cb_arg, const char *string),
                      void *cb_arg)
{
    allocator_t *alloc = (allocator_t *) allocator;

    if (print_cb == NULL)
        return;

    char buf[128];

    snprintf(buf, sizeof(buf), "%s allocator %p:\n", get_alloc_str(alloc), alloc);
    print_cb(cb_arg, buf);

    alloc_lock(alloc);

    snprintf(buf, sizeof(buf), "\talloc_lock=%p  options=%#.4x  trace_cb=%p,error_cb=%p\n",
             alloc->alloc_lock, alloc->options, alloc->log_cb, alloc->err_cb);

    alloc_unlock(alloc);

    print_cb(cb_arg, buf);

    snprintf(buf, sizeof(buf), "\tlock_type=%s,pool_lock=%p  pool=%p\n",
             get_lock_str(alloc->lock_type), alloc->pool_lock, alloc->pool);
    print_cb(cb_arg, buf);

    if (alloc->pool) {
        print_cb(cb_arg, "\t\t");

        pool_lock(alloc);

        vmem_pool_print(alloc->pool, print_cb, cb_arg);

        pool_unlock(alloc);

        print_cb(cb_arg, "\n");
    }
}

/*
 * (Re)initialize vmem_alloc_t iterator.
 */
void vmem_alloc_iter_init(vmem_alloc_iter_t *iter)
{
    iter->curr = (vmem_alloc_t) chain_head_;
}

/*
 * Get next vmem_alloc_t from iterator.
 */
vmem_alloc_t vmem_alloc_iter_next(vmem_alloc_iter_t *iter)
{
    allocator_t *curr = (allocator_t *) iter->curr;

    if (curr)
        iter->curr = (vmem_alloc_t) curr->next;

    return (vmem_alloc_t) curr;
}

/*
 * Install a default trace callback function.
 * Every allocator created up till now which has no callback function set yet, will get this one.
 * Every allocator created further on will get this callback function by default as well.
 */
void vmem_install_default_trace_callback(vmem_cb_t log_cb)
{
    vmem_alloc_iter_t iter;
    vmem_alloc_t curr;

    chain_lock();   /* Exclude vmem_alloc_create_pool. */

    default_log_cb_ = log_cb;

    vmem_alloc_iter_init(&iter);
    while ((curr = vmem_alloc_iter_next(&iter))) {
        allocator_t *alloc = (allocator_t *) curr;

        alloc_lock(alloc);  /* Exclude vmem_install_trace_callback. */

        if (alloc->log_cb == NULL)
            alloc->log_cb = log_cb;

        alloc_unlock(alloc);
    }

    chain_unlock();
}

/*
 * Install a default error callback function.
 * Every allocator created up till now which has no callback function set yet, will get this one.
 * Every allocator created further on will get this callback function by default as well.
 */
void vmem_install_default_error_callback(vmem_cb_t err_cb)
{
    vmem_alloc_iter_t iter;
    vmem_alloc_t curr;

    chain_lock();   /* Exclude vmem_alloc_create_pool. */

    default_err_cb_ = err_cb;

    vmem_alloc_iter_init(&iter);
    while ((curr = vmem_alloc_iter_next(&iter))) {
        allocator_t *alloc = (allocator_t *) curr;

        alloc_lock(alloc);  /* Exclude vmem_install_error_callback. */

        if (alloc->err_cb == NULL)
            alloc->err_cb = err_cb;

        alloc_unlock(alloc);
    }

    chain_unlock();
}

int vmem_trim_start(void)
{
    vtimer_t timer = vtimer_start_periodic(malloc_trim_cb, 300000, NULL);
    if (!timer) {
        vapi_error("Failed to start periodic timer for vmem_malloc_trim call");
        return -1;
    }

    return 0;
}
