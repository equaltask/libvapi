#include <stdio.h>

#include <libvapi/vtimer.h>
#include <libvapi/vtime.h>
#include <libvapi/vloop.h>
#include <libvapi/vlog.h>

#define TIMER_EXAMPLE_BUFFER_SIZE 256

static vlog_id_t g_log_id;

#define YP(__level, __msg, ...) vlog_printf(g_log_id, __level, VLOG_TAGS(TAG_END), __msg, ##__VA_ARGS__)
#define YPI(__msg, ...)         YP(VLOG_INFO, __msg, ##__VA_ARGS__)
#define YPE(__msg, ...)         YP(VLOG_ERROR, __msg, ##__VA_ARGS__)

void timer_callback(vtimer_t timer, void *ctxt)
{
    (void)ctxt;
    vtimer_delete(timer);
    YPI("Timer callback called");
}

void _abstimer_cb(vtimer_t timer, void *ctx)
{
    struct timespec ts;
    char buffer[TIMER_EXAMPLE_BUFFER_SIZE];

    vtime_get_wallclock(&ts);

    if (vtime_time_date_str(buffer, TIMER_EXAMPLE_BUFFER_SIZE, &ts) != 0) {
        YPE("Could not convert timespec to string.");
        return;
    }

    YPI("Expiry time : %s", buffer);
}

int yloop_app_init(int argc, char* argv[])
{
    struct timespec abs_to;
    char buffer[TIMER_EXAMPLE_BUFFER_SIZE];

    vlog_module_register("timer_example", &g_log_id);

    if (vtimer_start_periodic(timer_callback, 1000, 0) == NULL) {
        YPE("Could not start timeout ...");
    }

    vtime_get_next_interval_time(TIME_INTVAL_15MIN, &abs_to);

    if (vtime_time_date_str(buffer, TIMER_EXAMPLE_BUFFER_SIZE, &abs_to) != 0) {
        YPE("Could not convert timespec to string.");
    }

    YPI("Expected expiry time : %s", buffer);

    struct timespec abs_intv = { .tv_sec = 900, .tv_nsec = 0 };
    if (vtimer_start_absperiodic_ts(_abstimer_cb, abs_to, abs_intv, 0) == NULL) {
        YPE("Could not start absolute timeout ...");
    }

    return VAPI_SUCCESS;
}

