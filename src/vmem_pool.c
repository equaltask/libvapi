#include <sys/param.h>  /* roundup, MAX */
#include <stddef.h>     /* offsetof */
#include <stdlib.h>     /* malloc */
#include <stdio.h>      /* snprintf */

#include <libvapi/vlist.h>
#include "vmem_pool.h"

/* Avoid conflict with C++ alignof. */
#define yalignof(type)   offsetof(struct { char c; type member; }, member)

/*
 * Type enforcing most restrictive alignment constraints.
 * Avoid conflict with C++ max_align_t.
 */
typedef union {
    long double ld;
    long long l;
    void *p;
    void (*fp)(void);
}
ymax_align_t;

typedef struct {
    ulong_t current;
    ulong_t lowest;
    ulong_t max;
    ulong_t total;
    ulong_t failed;
}
pool_stats_t;

typedef struct {
    char *buf_begin;
    char *buf_end;
    vlist_t node;
}
pool_entry_t;

typedef struct {
    void *head;
    void *tail;

    ulong_t size;
    ulong_t max_elem;

    vlist_t pool_list;
    pool_stats_t stats;
}
pool_t;

static ulong_t vmem_pool_get_elem(ulong_t nr_elem, size_t block_size)
{
    ulong_t pool_elem = nr_elem;

    if ((nr_elem >= 50) && ((block_size * nr_elem) >= (20 * 1024))) {
        if ((nr_elem % 5) == 0) pool_elem = (nr_elem / 5);
        else if ((nr_elem % 4) == 0) pool_elem = (nr_elem / 4);
    }

    return pool_elem;
}

static int vmem_pool_extend(vmem_pool_t pool)
{
    pool_t *top = (pool_t *)pool;
    size_t cnt = vlist_count(&top->pool_list);

    if ((cnt * top->max_elem) >= top->stats.max)
        return 0;

    /*
     * Round up pool header sizes to multiple of most restrictive alignment constraint
     * (to enforce proper alignment of first block after header).
     * */
    size_t pool_header_size = roundup(sizeof(pool_entry_t), yalignof(ymax_align_t));

    void *pool_ptr = malloc(pool_header_size + (top->size * top->max_elem));
    if (pool_ptr == NULL) {
        return 0;
    }

    pool_entry_t *pool_entry = (pool_entry_t *)pool_ptr;
    pool_entry->buf_begin = (char *)pool_ptr + pool_header_size;
    pool_entry->buf_end = pool_entry->buf_begin + (top->size * top->max_elem);

    vlist_add_tail(&top->pool_list, &pool_entry->node);

    /* Build linked list of blocks. */
    ulong_t i;
    void **prev_ptr = &top->head;
    char *curr = pool_entry->buf_begin;

    for (i = 0; i < top->max_elem; i++) {
        *prev_ptr = curr;
        prev_ptr = (void **)curr;
        curr += top->size;
    }

    /* Terminate the linked list. */
    *prev_ptr = 0;

    /* Keep the tail for releasing buffers to. */
    top->tail = prev_ptr;

    return 1;
}

vmem_pool_t vmem_pool_create(size_t size, ulong_t nr_elem)
{
    if ((size == 0) || (nr_elem == 0))
        return NULL;

    /*
     * Make sure block size is big enough to hold a data pointer
     * (to be able to link blocks to one another).
     */
    size_t block_size = MAX(size, sizeof(void *));

    /*
     * Round up block size to multiple of most restrictive alignment constraint
     * (to enforce proper alignment of consecutive blocks).
     */
    block_size = roundup(block_size, yalignof(ymax_align_t));

    ulong_t pool_elem = vmem_pool_get_elem(nr_elem, block_size);

    pool_t *top = (pool_t *)malloc(sizeof(pool_t));
    if (top == NULL)
        return NULL;

    top->max_elem = pool_elem;
    top->size = block_size;   /* Note: size would be more restrictive than block_size. */
    top->stats.max = top->stats.current = top->stats.lowest = nr_elem;
    top->stats.total = top->stats.failed = 0;
    vlist_init(&top->pool_list);

    if (!vmem_pool_extend(top)) {
        free(top);
        return NULL;
    }

    return (vmem_pool_t)top;
}

void *vmem_pool_alloc(vmem_pool_t pool, size_t size)
{
    pool_t *p = (pool_t *)pool;
    if (p == NULL)
        return NULL;

    if (size > p->size) {
        p->stats.failed++;
        return NULL;
    }

    void *ptr = p->head;
    if (ptr == NULL) {
        p->stats.failed++;
        return NULL;
    }

    p->head = *(void **)ptr;
    if (p->head == NULL && !vmem_pool_extend(p)) {
        p->tail = NULL;
    }

    p->stats.current--;
    p->stats.total++;
    if (p->stats.current < p->stats.lowest)
        p->stats.lowest = p->stats.current;

    return ptr;
}

void *vmem_pool_free(vmem_pool_t pool, void *ptr)
{
    pool_t *top = (pool_t *)pool;
    if (top == NULL)
        return ptr;

    if (ptr == NULL)
        return NULL;

    /* Check whether buffer to free belongs to this pool. */
    vlist_t *node;
    pool_entry_t *pool_entry = NULL;
    bool pool_found = false;

    vlist_foreach(&top->pool_list, node) {
        pool_entry = container_of(pool_entry_t, node, node);
        if (((char *)ptr < pool_entry->buf_begin) || (pool_entry->buf_end <= (char *)ptr)) {
            continue;
        } else {
            pool_found = true;
            break;
        }
    }

    if (!pool_found) {
        return ptr;
    }

    /* Add newly freed buffer at the end. */
    *(void **)ptr = NULL;
    if (top->tail != NULL)
        *(void **)top->tail = ptr;
    top->tail = ptr;

    if (top->head == NULL)
        top->head = top->tail;

    top->stats.current++;

    return NULL;
}

void vmem_pool_print(vmem_pool_t pool,
                     void (*print_cb)(void *cb_arg, const char *string),
                     void *cb_arg)
{
    pool_t *p = (pool_t *) pool;
    if (p == NULL)
        return;

    if (print_cb == NULL)
        return;

    int offset = 0;
    char buf[1024];
    vlist_t *node;
    pool_entry_t *pool_entry = NULL;

    offset += snprintf(&buf[offset], sizeof(buf) - offset - 1, "size=%ld  max=%ld,curr=%ld,low=%ld,total=%ld,fail=%ld\n",
                       p->size, p->stats.max, p->stats.current, p->stats.lowest, p->stats.total, p->stats.failed);
    vlist_foreach(&p->pool_list, node) {
        pool_entry = container_of(pool_entry_t, node, node);
        offset += snprintf(&buf[offset], sizeof(buf) - offset - 1, "\t\tmax=%ld  start=%p,end=%p\n", p->max_elem, pool_entry->buf_begin, pool_entry->buf_end);
    }
    print_cb(cb_arg, buf);
}
