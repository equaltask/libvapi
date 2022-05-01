
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <unistd.h>

#include "vlog_core.h"
#include "vlog_opentracing.h"

void (*timer_callback)(void *);

void TimerCallback(vtimer_t timer, void *ctxt)
{
    timer_callback(ctxt);
}

void createTimer(time_t interval, void *ptr, void (*callback)(void *))
{
    timer_callback = callback;

    struct timespec time;
    time.tv_nsec = 0;
    time.tv_sec = interval;

    vtimer_start_periodic_ts(TimerCallback, time, ptr);
}

int vlog_opentracing_open(vlog_opentracing_connection_t *handle)
{
    if (handle == NULL)
        return -1;

    vlog_create_tracer(handle->service_name, handle->server_address, handle->server_port,
                       handle->spans_nmb_per_flush, handle->delay_per_flush_in_second, createTimer);
    return 0;
}

int vlog_opentracing_print(const char *span_name, vlog_tags_t *tags, const char *msg)
{
    if (span_name == NULL || strlen(span_name) <= 0 || msg == NULL)
        return -1;

    if (strlen(msg) > 0) {
        //return vlog_record_log(span_name, tags->TAG_FUNCTION, vlog_level_to_str(tags->TAG_LEVEL), msg);
    }

    return 0;
}

int vlog_opentracing_close()
{
    vlog_close_tracer();

    return 0;
}
