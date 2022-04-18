#include <unistd.h>
#include <stdbool.h>

#include <libvapi/vloop.h>
#include <libvapi/vfs.h>
#include <libvapi/vmem.h>
#include <libvapi/vloop_demand_event.h>
#include <libvapi/vdbg.h>

#include <event2/event.h>

#include "vlog_vapi.h"

static const int pipe_reader = 0;
static const int pipe_writer = 1;

static void close_pipe(vloop_on_demand_event_handle_t handle)
{
    vloop_on_demand_event_handle *ode_handle = (vloop_on_demand_event_handle *)handle;
    if (vfs_close_simple(ode_handle->pipe_fd[pipe_reader]) < 0)
        vapi_info("pipe cannot be closed: fd = %d", ode_handle->pipe_fd[pipe_reader]);
    if (vfs_close_simple(ode_handle->pipe_fd[pipe_writer]) < 0)
        vapi_info("pipe cannot be closed: fd = %d", ode_handle->pipe_fd[pipe_writer]);
}

static int set_fd_non_blocking(int fd)
{
    int ret;
    int flags = fcntl(fd, F_GETFL);

    flags |= O_NONBLOCK;

    ret = fcntl(fd, F_SETFL, flags);
    if (ret < 0)
        vapi_info("fcntl failed: fd = %d, errno = %d (%s)", fd, errno, strerror(errno));

    return ret;
}

static int create_non_blocking_pipe(vloop_on_demand_event_handle_t handle)
{
    vloop_on_demand_event_handle *ode_handle = (vloop_on_demand_event_handle *)handle;

    if (pipe(ode_handle->pipe_fd) < 0) {
        vapi_info("pipe failed: errno = %d (%s)", errno, strerror(errno));
        return -1;
    }

    if (set_fd_non_blocking(ode_handle->pipe_fd[pipe_reader]) < 0) {
        close_pipe(ode_handle);
        return -1;
    }

    if (set_fd_non_blocking(ode_handle->pipe_fd[pipe_writer]) < 0) {
        close_pipe(ode_handle);
        return -1;
    }

    return 0;
}

static void free_on_demand_event_handle(vloop_on_demand_event_handle_t handle)
{
    vloop_on_demand_event_handle *ode_handle = (vloop_on_demand_event_handle *)handle;

    if (!ode_handle) {
        vapi_info("vloop: no on-demand event handle given");
        return;
    }

    if (ode_handle->vloop_handle) {
        vloop_disable_cb(ode_handle->vloop_handle, VLOOP_FD_READ);
        vloop_remove_fd(ode_handle->vloop_handle);
    }

    if (ode_handle->pipe_fd[pipe_reader] != -1)
        close_pipe(ode_handle);

    vmem_free(vmem_alloc_default(), ode_handle);
}

static int on_demand_event_cb(int fd, vloop_event_handle_t event_handle, void *ctxt)
{
    vloop_on_demand_event_handle *ode_handle = (vloop_on_demand_event_handle *)ctxt;
    char buf[1024];

    int len = vfs_read(ode_handle->pipe_fd[pipe_reader], &buf, sizeof(buf));
    if (!ode_handle->event_cb) {
        vapi_info("vloop: no callback specified for on-demand event");
        return -1;
    }

    return ode_handle->event_cb((vloop_on_demand_event_handle_t)ode_handle, buf, len, ode_handle->user_ctxt);
}

int vloop_remove_on_demand_event(vloop_on_demand_event_handle_t handle)
{
    vloop_on_demand_event_handle *ode_handle = (vloop_on_demand_event_handle *)handle;
    if (!ode_handle) {
        vapi_info("vloop: no on-demand event handle given");
        return -1;
    }

    free_on_demand_event_handle(ode_handle);
    return 0;
}

vloop_on_demand_event_handle_t vloop_add_on_demand_event(vloop_on_demand_cb event_cb, int priority, void *ctxt)
{
    if (!event_cb) {
        vapi_info("vloop: no on-demand callback specified");
        return NULL;
    }

    vloop_on_demand_event_handle *ode_handle = vmem_calloc(vmem_alloc_default(), sizeof(*ode_handle));
    if (!ode_handle) {
        vapi_info("vloop: cannot allocate on-demand handle");
        return NULL;
    }

    ode_handle->pipe_fd[pipe_reader] = -1;
    ode_handle->pipe_fd[pipe_writer] = -1;
    ode_handle->event_cb = event_cb;
    ode_handle->user_ctxt = ctxt;

    if (create_non_blocking_pipe(ode_handle) < 0)
        return NULL;

    ode_handle->vloop_handle = vloop_add_fd(ode_handle->pipe_fd[pipe_reader], VLOOP_FD_READ, on_demand_event_cb, NULL, (void *)ode_handle);
    if (!ode_handle->vloop_handle) {
        vapi_info("vloop: cannot add pipe reader end: fd = %d", ode_handle->pipe_fd[pipe_reader]);
        free_on_demand_event_handle(ode_handle);
        return NULL;
    }

    if (vloop_set_read_event_prio(ode_handle->vloop_handle, priority) < 0) {
        vapi_error("vloop: cannot set on-demand event priority (vloop_set_max_prio missing?): priority = %d", priority);
        free_on_demand_event_handle(ode_handle);
        return NULL;
    }

    if (vloop_enable_cb(ode_handle->vloop_handle, VLOOP_FD_READ) < 0) {
        vapi_info("vloop: cannot enable pipe reader callback");
        free_on_demand_event_handle(ode_handle);
        return NULL;
    }

    return ode_handle;
}

int vloop_trigger_on_demand_event(vloop_on_demand_event_handle_t handle, const char *msg, int len)
{
    vloop_on_demand_event_handle *ode_handle = (vloop_on_demand_event_handle *)handle;
    if (!ode_handle) {
        vapi_info("vloop: no on-demand event handle given");
        return -1;
    }

    if (ode_handle->pipe_fd[pipe_writer] == -1) {
        vapi_info("vloop: no on-demand pipe writer end known");
        return -1;
    }

    if (vfs_write(ode_handle->pipe_fd[pipe_writer], msg, len) != len) {
        vapi_info("vloop: cannot write to pipe write end: fd = %d", ode_handle->pipe_fd[pipe_writer]);
        return -1;
    }

    return 0;
}

int vloop_print_on_demand_event(vloop_on_demand_event_handle_t ode_handle)
{
    vloop_on_demand_event_handle *handle = (vloop_on_demand_event_handle *)ode_handle;
    vdbg_printf("callback=%10p, ctxt=%10p, pipe={ %3d, %3d }",
                handle->event_cb, handle->user_ctxt,
                handle->pipe_fd[0], handle->pipe_fd[1]);
    return 0;
}
