#include <errno.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

/* libevent specific code. */
#include <event2/event.h>
#include <event2/event_struct.h>

#include <libvapi/vlog.h>
#include <libvapi/vloop.h>
#include <libvapi/vmem.h>
#include <libvapi/vtimer.h>

#include "vlog_vapi.h"
#include "vloop_internal.h"

#define USE_LIBEVENT_PERIODIC_TIMER

enum _vtimer_type {
    VTIMER_TIMEOUT,
    VTIMER_PERIODIC,
    VTIMER_ABSTIMEOUT,
    VTIMER_ABSPERIODIC
};

enum _vtimer_state {
    VTIMER_CREATED,
    VTIMER_RUNNING
};

struct _vtimer {
    vtimer_cb_t callback;
    struct timespec timeout;
    struct timespec interval;
    enum _vtimer_type type;
    enum _vtimer_state state;
    void *user_context;

    struct _vtimer *prev;
    struct _vtimer *next;

    /* libevent specific part */
    struct event *ev;
    struct timeval timeout_val;

    /* absolute timers */
    int timer_fd;
    vloop_event_handle_t timeout_evhdl;
    struct timespec abstimeout_val;

    vlog_opentracing_context_ptr jsonopentracer_context;
    int jsonopentracer_context_size;
};

static __thread struct _vtimer *g_head;

/*************************************************************************************
 * Libevent specific code.
 */

static void _int_callback(evutil_socket_t fd, short what, void *arg)
{
    if (arg == NULL)
        return;

    vtimer_t tmr_handle = (vtimer_t)arg;
    struct _vtimer *tmr = (struct _vtimer *)arg;

    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_int_callback_%p", arg);

        if (tmr->jsonopentracer_context == NULL || tmr->jsonopentracer_context_size == 0) {
            vlog_start_parent_span(span_name);
        } else {
            vlog_start_follows_from_span(span_name, tmr->jsonopentracer_context, tmr->jsonopentracer_context_size);
        }
    }

    if (tmr->type == VTIMER_PERIODIC) {
        /* Depending on the implementation, the timer needs to be restarted here or
         * not. If during the timer creation the EV_PERSIST flag has been specified,
         * the timer is periodic so no need to restart it here. That is in fact the
         * correct solution since restarting a one-shot time causes drift.
         */
#if !defined(USE_LIBEVENT_PERIODIC_TIMER)
        (void)evtimer_add(tmr->ev, &tmr->timeout_val);
#endif
    } else {
        event_del(tmr->ev);
        tmr->state = VTIMER_CREATED;
    }

    tmr->callback(tmr_handle, tmr->user_context);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        vlog_finish_span(span_name);
    }
}

/*************************************************************************************/

static int _timerfd_callback(int fd, vloop_event_handle_t event_handle, void *ctx)
{
    if (ctx == NULL)
        return -1;

    int ret = VAPI_FAILURE;
    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_callback_%p", ctx);

        if (event_handle->jsonopentracer_context == NULL || event_handle->jsonopentracer_context_size == 0) {
            ret = vlog_start_parent_span(span_name);
        } else {
            ret = vlog_start_follows_from_span(span_name, event_handle->jsonopentracer_context, event_handle->jsonopentracer_context_size);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vtimer: cannot start span %s", span_name);
        }
    }

    uint64_t val = 0;
    vtimer_t tmr_handle = (vtimer_t)ctx;
    struct _vtimer *tmr = (struct _vtimer *)ctx;

    if (tmr->type == VTIMER_ABSTIMEOUT) {
        tmr->state = VTIMER_CREATED;
    }

    if (read(tmr->timer_fd, &val, sizeof(uint64_t)) != sizeof(uint64_t)) {
        vapi_error("Failed to read data from timerfd: errno=%d (%s)", errno, strerror(errno));

        if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
            vlog_finish_span(span_name);
        }

        return -1;
    }

    tmr->callback(tmr_handle, tmr->user_context);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        if (ret == VAPI_SUCCESS)
            vlog_finish_span(span_name);
    }

    return 0;
}

static struct _vtimer *_create_timer(vtimer_cb_t callback, enum _vtimer_type type, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size)
{
    int ret = 0;

    struct _vtimer *tmr = vmem_malloc(vmem_alloc_default(), sizeof(struct _vtimer));
    if (tmr == NULL)
        return NULL;

    memset(tmr, 0, sizeof(*tmr));

    tmr->callback = callback;
    tmr->type = type;
    tmr->user_context = ctxt;
    tmr->state = VTIMER_CREATED;

    /* Element will be inserted in the front of the list. */
    tmr->next = g_head;
    tmr->prev = 0;

    tmr->jsonopentracer_context = jsonopentracer_context;
    tmr->jsonopentracer_context_size = jsonopentracer_context_size;

    if (g_head)
        g_head->prev = tmr;
    g_head = tmr;

    switch (type) {
    case VTIMER_TIMEOUT:
    case VTIMER_PERIODIC:
        /* START: libevent specific code. */
#if defined(USE_LIBEVENT_PERIODIC_TIMER)
        tmr->ev = event_new(vloop_get_base(), -1, EV_PERSIST, _int_callback, tmr);
#else
        tmr->ev = evtimer_new(vloop_get_base(), _int_callback, tmr);
#endif
        /* END libevent specific code. */
        if (tmr->ev == 0) {
            vtimer_delete((vtimer_t)tmr);
            return NULL;
        }
        break;

    case VTIMER_ABSTIMEOUT:
    case VTIMER_ABSPERIODIC:
        tmr->timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
        if (tmr->timer_fd == 0) {
            vtimer_delete((vtimer_t)tmr);
            return NULL;
        }

        tmr->timeout_evhdl = vloop_add_fd_ot(tmr->timer_fd, VLOOP_FD_READ, _timerfd_callback, NULL, tmr, jsonopentracer_context, jsonopentracer_context_size);
        if (tmr->timeout_evhdl == 0) {
            close(tmr->timer_fd);
            vtimer_delete((vtimer_t)tmr);
            return NULL;
        }

        ret = vloop_enable_cb(tmr->timeout_evhdl, VLOOP_FD_READ);
        if (ret != 0) {
            vloop_remove_fd(tmr->timeout_evhdl);
            close(tmr->timer_fd);
            vtimer_delete((vtimer_t)tmr);
            return NULL;
        }
        break;

    default:
        vapi_error("Not supported timer type");
        return NULL;
    }

    return tmr;
}

static int _start_timer(struct _vtimer *timer, struct timespec timeout, struct timespec interval)
{
    if (timer == NULL)
        return -1;

    timer->timeout = timeout;
    timer->interval = interval;
    timer->state = VTIMER_RUNNING;

    struct itimerspec newits = { .it_interval = interval, .it_value = timeout };

    switch (timer->type) {
    case VTIMER_TIMEOUT:
    case VTIMER_PERIODIC:
        /* START libevent specific code. */
        timer->timeout_val.tv_sec = timer->timeout.tv_sec;
        timer->timeout_val.tv_usec = timer->timeout.tv_nsec / 1000;

        /* libevent uses time cache, which can lead to inaccurate timeout when
         * new timer is started in user callback.
         * Hence forcing time update here for non zero-timers.
         */
        if (!((timeout.tv_sec == 0) && (timeout.tv_sec == 0)))
            event_base_update_cache_time(vloop_get_base());

        return evtimer_add(timer->ev, &timer->timeout_val);
    /* END libevent specific code. */

    case VTIMER_ABSTIMEOUT:
    case VTIMER_ABSPERIODIC:
        return timerfd_settime(timer->timer_fd, TFD_TIMER_ABSTIME, &newits, NULL);

    default:
        vapi_error("Not supported timer type");
        break;
    }

    return -1;
}

static inline vtimer_t _set_timer(vtimer_cb_t callback, struct timespec timeout, struct timespec interval, enum _vtimer_type type, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context,
                                  int jsonopentracer_context_size)
{
    struct _vtimer *timer = _create_timer(callback, type, ctxt, jsonopentracer_context, jsonopentracer_context_size);

    if (!timer)
        return NULL;

    if (_start_timer(timer, timeout, interval) != 0) {
        vapi_error("Failed to start timer");
        return NULL;
    }

    return (vtimer_t)timer;
}

vtimer_t vtimer_start_periodic_ts(vtimer_cb_t callback, struct timespec timeout, void *ctxt)
{
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };
    return _set_timer(callback, timeout, dummy, VTIMER_PERIODIC, ctxt, NULL, 0);
}

vtimer_t vtimer_start_periodic_ts_ot(vtimer_cb_t callback, struct timespec timeout, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size)
{
    int ret = VAPI_FAILURE;
    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_start_periodic_ts_ot_%p", callback);

        if (jsonopentracer_context != NULL && jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, jsonopentracer_context, jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }
    }

    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };

    if (ret == VAPI_FAILURE) {
        vlog_notag_printf(0, VLOG_WARNING, "vtimer: cannot start span %s", span_name);
    } else {
        vtimer_record_tags(span_name, dummy, timeout);
    }

    vtimer_t _timer = _set_timer(callback, timeout, dummy, VTIMER_PERIODIC, ctxt, jsonopentracer_context, jsonopentracer_context_size);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        if (ret == VAPI_SUCCESS)
            vlog_finish_span(span_name);
    }

    return _timer;
}

vtimer_t vtimer_start_periodic(vtimer_cb_t callback, int interval_ms, void *ctxt)
{
    struct timespec ts = { .tv_sec = interval_ms / 1000, .tv_nsec = (interval_ms % 1000) * 1000000 };
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };
    return _set_timer(callback, ts, dummy, VTIMER_PERIODIC, ctxt, NULL, 0);
}

vtimer_t vtimer_start_periodic_ot(vtimer_cb_t callback, int interval_ms, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size)
{
    struct timespec ts = { .tv_sec = interval_ms / 1000, .tv_nsec = (interval_ms % 1000) * 1000000 };
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };

    int ret = VAPI_FAILURE;
    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_start_periodic_ot_%p", callback);

        if (jsonopentracer_context != NULL && jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, jsonopentracer_context, jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vtimer: cannot start span %s", span_name);
        } else {
            vtimer_record_tags(span_name, dummy, ts);
        }
    }

    vtimer_t _timer = _set_timer(callback, ts, dummy, VTIMER_PERIODIC, ctxt, jsonopentracer_context, jsonopentracer_context_size);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        if (ret == VAPI_SUCCESS)
            vlog_finish_span(span_name);
    }

    return _timer;
}

vtimer_t vtimer_start_timeout_ts(vtimer_cb_t callback, struct timespec timeout, void *ctxt)
{
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };
    return _set_timer(callback, timeout, dummy, VTIMER_TIMEOUT, ctxt, NULL, 0);
}

vtimer_t vtimer_start_timeout_ts_ot(vtimer_cb_t callback, struct timespec timeout, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size)
{
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };

    int ret = VAPI_FAILURE;
    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_start_timeout_ts_ot_%p", callback);

        if (jsonopentracer_context != NULL && jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, jsonopentracer_context, jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vtimer: cannot start span %s", span_name);
        } else {
            vtimer_record_tags(span_name, dummy, timeout);
        }
    }

    vtimer_t _timer = _set_timer(callback, timeout, dummy, VTIMER_TIMEOUT, ctxt, jsonopentracer_context, jsonopentracer_context_size);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        if (ret == VAPI_SUCCESS)
            vlog_finish_span(span_name);
    }

    return _timer;
}

vtimer_t vtimer_start_timeout(vtimer_cb_t callback, int timeout_ms, void *ctxt)
{
    struct timespec ts = { .tv_sec = timeout_ms / 1000, .tv_nsec = (timeout_ms % 1000) * 1000000 };
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };
    return _set_timer(callback, ts, dummy, VTIMER_TIMEOUT, ctxt, NULL, 0);
}

vtimer_t vtimer_start_timeout_ot(vtimer_cb_t callback, int timeout_ms, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size)
{
    struct timespec ts = { .tv_sec = timeout_ms / 1000, .tv_nsec = (timeout_ms % 1000) * 1000000 };
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };

    int ret = VAPI_FAILURE;
    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_start_timeout_ot_%p", callback);

        if (jsonopentracer_context != NULL && jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, jsonopentracer_context, jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vtimer: cannot start span %s", span_name);
        } else {
            vtimer_record_tags(span_name, dummy, ts);
        }
    }

    vtimer_t _timer = _set_timer(callback, ts, dummy, VTIMER_TIMEOUT, ctxt, jsonopentracer_context, jsonopentracer_context_size);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        if (ret == VAPI_SUCCESS)
            vlog_finish_span(span_name);
    }

    return _timer;
}

vtimer_t vtimer_start_abstimeout_ts(vtimer_cb_t callback, struct timespec abstimeout, void *ctxt)
{
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };
    return _set_timer(callback, abstimeout, dummy, VTIMER_ABSTIMEOUT, ctxt, NULL, 0);
}

vtimer_t vtimer_start_abstimeout_ts_ot(vtimer_cb_t callback, struct timespec abstimeout, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size)
{
    struct timespec dummy = { .tv_sec = 0, .tv_nsec = 0 };

    int ret = VAPI_FAILURE;
    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_start_abstimeout_ts_ot_%p", callback);

        if (jsonopentracer_context != NULL && jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, jsonopentracer_context, jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vtimer: cannot start span %s", span_name);
        } else {
            vtimer_record_tags(span_name, dummy, abstimeout);
        }
    }

    vtimer_t _timer = _set_timer(callback, abstimeout, dummy, VTIMER_ABSTIMEOUT, ctxt, jsonopentracer_context, jsonopentracer_context_size);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        if (ret == VAPI_SUCCESS)
            vlog_finish_span(span_name);
    }

    return _timer;
}

vtimer_t vtimer_start_absperiodic_ts(vtimer_cb_t callback, struct timespec abstimeout, struct timespec interval, void *ctxt)
{
    return _set_timer(callback, abstimeout, interval, VTIMER_ABSPERIODIC, ctxt, NULL, 0);
}

vtimer_t vtimer_start_absperiodic_ts_ot(vtimer_cb_t callback, struct timespec abstimeout, struct timespec interval, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context,
                                        int jsonopentracer_context_size)
{
    int ret = VAPI_FAILURE;
    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_start_absperiodic_ts_ot_%p", callback);

        if (jsonopentracer_context != NULL && jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, jsonopentracer_context, jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vtimer: cannot start span %s", span_name);
        } else {
            vtimer_record_tags(span_name, interval, abstimeout);
        }
    }

    vtimer_t _timer = _set_timer(callback, abstimeout, interval, VTIMER_ABSPERIODIC, ctxt, jsonopentracer_context, jsonopentracer_context_size);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        if (ret == VAPI_SUCCESS)
            vlog_finish_span(span_name);
    }

    return _timer;
}

int vtimer_delete(vtimer_t timer)
{
    struct _vtimer *tmr = (struct _vtimer *)timer;

    if (tmr == NULL)
        return -1;

    if (tmr == g_head) {
        g_head = tmr->next;
        if (g_head)
            g_head->prev = 0;
    } else {
        if (tmr->next != 0)
            tmr->next->prev = tmr->prev;
        if (tmr->prev != 0)
            tmr->prev->next = tmr->next;
    }

    switch (tmr->type) {
    case VTIMER_TIMEOUT:
    case VTIMER_PERIODIC:
        /* START libevent specific code. */
        evtimer_del(tmr->ev);
        event_free(tmr->ev);
        /* END libevent specific code. */
        break;

    case VTIMER_ABSTIMEOUT:
    case VTIMER_ABSPERIODIC:
        if (tmr->timer_fd != 0)
            close(tmr->timer_fd);
        if (tmr->timeout_evhdl != 0)
            vloop_remove_fd(tmr->timeout_evhdl);
        break;

    default:
        vapi_error("Not supported timer type");
        break;
    }

    vmem_free(vmem_alloc_default(), tmr);

    return 0;
}

int vtimer_set_prio(vtimer_t timer, int prio)
{
    struct _vtimer *tmr = (struct _vtimer *)timer;
    int ret = 0;

    if (tmr == NULL)
        return -1;

    switch (tmr->type) {
    case VTIMER_TIMEOUT:
    case VTIMER_PERIODIC:
        vapi_warning("No timer priority support for type %d", tmr->type);
        break;

    case VTIMER_ABSTIMEOUT:
    case VTIMER_ABSPERIODIC:
        ret = vloop_set_read_event_prio(tmr->timeout_evhdl, prio);
        break;

    default:
        vapi_error("Not supported timer type");
        break;
    }

    return ret;
}

void vtimer_record_tags(const char *span_name, struct timespec interval, struct timespec timeout)
{
    char tag_interval[51];
    char tag_timeout[51];

    snprintf(tag_interval, 50, "seconds: %lld, nanoseconds: %ld", (long long)interval.tv_sec, interval.tv_nsec);
    snprintf(tag_timeout, 50, "seconds: %lld, nanoseconds: %ld", (long long)timeout.tv_sec, timeout.tv_nsec);

    vlog_record_tag(span_name, "interval", tag_interval);
    vlog_record_tag(span_name, "timeout", tag_timeout);
}

////////////////////// debug interface ////////////////////////////

static char *vtimer_type_strings[] = {
    "VTIMER_TIMEOUT    ",
    "VTIMER_PERIODIC   ",
    "VTIMER_ABSTIMEOUT ",
    "VTIMER_ABSPERIODIC"
};

static char *vtimer_state_strings[] = {
    "VTIMER_CREATED",
    "VTIMER_RUNNING"
};

static void vtimer_display(struct _vtimer *tmr)
{
    printf("%8p %8p %s %s %10ld:%06ld %10ld:%06ld %8p\n", tmr, tmr->callback,
           vtimer_type_strings[tmr->type], vtimer_state_strings[tmr->state],
           tmr->timeout.tv_sec, tmr->timeout.tv_nsec / 1000,
           tmr->interval.tv_sec, tmr->interval.tv_nsec / 1000,
           tmr->user_context);
}

void vtimer_dump_all()
{
    struct _vtimer *tmr = g_head;
    printf("\n");
    printf("tmr       cb         type               state            timeout           interval        uctxt\n");
    printf("====================================================================================================\n");
    while (tmr) {
        vtimer_display(tmr);
        tmr = tmr->next;
    }
}
