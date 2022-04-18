#include <libvapi/vsem.h>
#include <libvapi/vmem.h>
#include <libvapi/vlist.h>
#include <libvapi/vmutex.h>

#include <sys/eventfd.h> // eventfd
#include <errno.h> // errno
#include <unistd.h> // close

#include <event2/event.h> // event_new, event_add, event_free

#include "vlog_vapi.h"
#include "vloop_internal.h"

vlist_t sem_list;
vthread_mutex_t mtx;

struct vsem {
    int fd;
    vlist_t node;
};

typedef struct {
    vsem_t *sem;
    struct event *lev;
    vevent_t *yev;
    vsem_cb_t user_cb;
    void *user_ctxt;
} vsem_ctxt_t;

static __attribute__((constructor)) void init_vsem_mutex(void)
{
    vmutex_create(&mtx);
    vlist_init(&sem_list);
}

vsem_t *vsem_new(int val)
{
    int fd, saved_errno;
    vsem_t *sem;

    int ret = vmutex_lock(&mtx);
    if (ret != 0)
        return NULL;

    // We create a counting semaphore with EFD_SEMAPHORE because it is more general.
    // To have a non-counting semaphore (a mutex) user can give 1 as val.
    fd = eventfd(val, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    if (fd == -1) {
        vmutex_unlock(&mtx);
        saved_errno = errno;
        vapi_error("Failed to create eventfd");
        vapi_print_errno(saved_errno);
        return NULL;
    }

    sem = vmem_malloc(vmem_alloc_default(), sizeof(*sem));
    if (sem == NULL) {
        vapi_error("Failed to allocate memory for vsem");
        close(fd);
        vmutex_unlock(&mtx);
        return NULL;
    }

    sem->fd = fd;

    vlist_add_tail(&sem_list, &sem->node);
    vmutex_unlock(&mtx);

    return sem;
}

bool sem_exists(vsem_t *sem)
{
    vlist_t *node = NULL;
    vsem_t *el = NULL;

    int ret = vmutex_lock(&mtx);
    if (ret != 0)
        return false;

    vlist_foreach(&sem_list, node) {
        el = container_of(vsem_t, node, node);
        if (el == sem)
            break;
    }

    vmutex_unlock(&mtx);
    return el == sem;
}

void vsem_free(vsem_t *sem)
{
    if (!sem_exists(sem)) {
        return;
    }

    int ret = vmutex_lock(&mtx);
    if (ret != 0)
        return;

    vlist_delete(&sem->node);
    vmutex_unlock(&mtx);
    close(sem->fd);
    vmem_free(vmem_alloc_default(), sem);
}

static void finish_sem_wait(vevent_reason_t reason, vsem_ctxt_t *ctxt)
{
    event_free(ctxt->lev);
    ctxt->user_cb(reason, ctxt->user_ctxt);
    vmem_free(vmem_alloc_default(), ctxt);
}

static void eventfd_cb(evutil_socket_t fd, short events, void *ctx)
{
    int rc;
    uint64_t val = 0;
    vsem_ctxt_t *ctxt;

    ctxt = (vsem_ctxt_t *) ctx;

    rc = read(ctxt->sem->fd, &val, sizeof(uint64_t));
    if (rc > 0 && val > 0) {
        // this is the normal way for sem wait to finish
        vevent_delete(ctxt->yev);
        finish_sem_wait(VEVENT_OCCURED, ctxt);
    }
}

static void sem_wait_cancel(vevent_reason_t reason, void *ctx)
{
    // this is the abnormal way for sem wait to finish
    finish_sem_wait(reason, (vsem_ctxt_t *) ctx);
}

vevent_t *vsem_wait(vsem_t *sem, vsem_cb_t user_cb, void *user_ctxt)
{
    int rc;
    struct event *lev;
    vsem_ctxt_t *ctxt;
    vevent_t *yev;

    if (!sem_exists(sem)) {
        return NULL;
    }

    ctxt = vmem_malloc(vmem_alloc_default(), sizeof(*ctxt));

    if (ctxt == NULL) {
        vapi_error("Failed to allocate memory for vsem context");
        return NULL;
    }

    lev = event_new(vloop_get_base(), sem->fd, EV_READ | EV_PERSIST, eventfd_cb, ctxt);
    if (lev == NULL) {
        vapi_error("Failed to create libevent event for sem wait");
        vmem_free(vmem_alloc_default(), ctxt);
        return NULL;
    }

    rc = event_add(lev, NULL);
    if (rc == -1) {
        vapi_error("Failed to add eventfd to event loop");
        vmem_free(vmem_alloc_default(), ctxt);
        event_free(lev);
        return NULL;
    }

    yev = vevent_new(sem_wait_cancel, ctxt);
    if (yev == NULL) {
        vapi_error("Failed to create vevent for vsem");
        vmem_free(vmem_alloc_default(), ctxt);
        event_free(lev);
        return NULL;
    }

    ctxt->sem = sem;
    ctxt->lev = lev;
    ctxt->yev = yev;
    ctxt->user_cb = user_cb;
    ctxt->user_ctxt = user_ctxt;

    return yev;
}

int vsem_post(vsem_t *sem)
{
    int rc;
    uint64_t val = 1;

    if (!sem_exists(sem)) {
        return -1;
    }

    rc = write(sem->fd, &val, sizeof(val));
    if (rc != sizeof(val)) {
        vapi_error("Failed to write to eventfd");
        return -1;
    }

    return 0;
}
