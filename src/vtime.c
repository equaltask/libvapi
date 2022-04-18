// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>

#include <libvapi/vtime.h>
#include <libvapi/vlog.h>
#include <libvapi/vsystem.h>
#include <libvapi/vmem.h>
#include <libvapi/vtimer.h>

#include "vlog_vapi.h"

typedef struct vtime_update_ctx {
    vtime_update_cb_t update_cb;
    void *user_ctx;
    struct timespec prev_ts;
} vtime_update_ctx_t;

typedef struct vtime_tz_ctx {
    struct tm tz_tm;
    vtime_timezone_cb_t tz_cb;
    void *user_ctx;
} vtime_tz_ctx_t;

static int _interval_to_sec(vtime_interval_t intval)
{
    switch (intval) {
    case TIME_INTVAL_1MIN:
        return 60;
    case TIME_INTVAL_5MIN:
        return 300;
    case TIME_INTVAL_15MIN:
        return 900;
    case TIME_INTVAL_1HR:
        return 3600;
    case TIME_INTVAL_24HR:
        return 86400;
    default:
        break;
    }
    return 0;
}

int vtime_get_time(struct timespec *time)
{
    if (clock_gettime(CLOCK_MONOTONIC, time) == -1) {
        vapi_error("%s failed : %s", __func__, strerror(errno));
        return -1;
    }

    return 0;
}

int vtime_get_wallclock(struct timespec *time)
{
    if (clock_gettime(CLOCK_REALTIME, time) == -1) {
        vapi_error("%s failed : %s", __func__, strerror(errno));
        return -1;
    }

    return 0;
}

int vtime_get_next_interval_time(vtime_interval_t interval, struct timespec *time)
{
    struct timespec clock_ts;
    int intval_sec = 0;

    if (clock_gettime(CLOCK_REALTIME, &clock_ts) == -1) {
        vapi_error("%s failed : %s", __func__, strerror(errno));
        return -1;
    }

    intval_sec = _interval_to_sec(interval);
    time->tv_nsec = 0;
    if (intval_sec > 0) {
        time->tv_sec = clock_ts.tv_sec + intval_sec - (clock_ts.tv_sec % intval_sec);
    } else {
        time->tv_sec = 0;
        return -1;
    }

    return 0;
}

int vtime_convert_utc_tm(time_t timep, struct tm *time)
{
    if (gmtime_r(&timep, time) == NULL) {
        vapi_error("%s failed : %s", __func__, strerror(errno));
        return -1;
    }
    return 0;
}

int vtime_get_utc_tm(struct tm *time)
{
    struct timespec ts;
    if (vtime_get_wallclock(&ts) != 0) {
        return -1;
    }
    return vtime_convert_utc_tm(ts.tv_sec, time);
}

int vtime_convert_wallclock_tm(time_t timep, struct tm *time)
{
    tzset();
    if (localtime_r(&timep, time) == NULL) {
        vapi_error("%s failed : %s", __func__, strerror(errno));
        return -1;
    }
    return 0;
}

int vtime_convert_wallclock_time_t(struct tm *time, time_t *timep)
{
    if (time == NULL || timep == NULL)
        return -1;

    time_t time_converted = mktime(time);
    if (time_converted == (time_t) -1) {
        vapi_error("%s failed : %s", __func__, strerror(errno));
        return -1;
    }
    *timep = time_converted;

    return 0;
}

int vtime_get_wallclock_tm(struct tm *time)
{
    struct timespec ts;
    if (vtime_get_wallclock(&ts) != 0) {
        return -1;
    }
    return vtime_convert_wallclock_tm(ts.tv_sec, time);
}

int vtime_set_wallclock_tm(struct tm *time)
{
    struct timespec ts;

    tzset();

    /*
     * Portability note: timelocal is rather rare.
     * mktime is functionally identical to timelocal,
     * and essentially universally available.
     */
    ts.tv_sec = mktime(time);
    ts.tv_nsec = 0;

    if (ts.tv_sec == -1) {
        // wrong date input
        return -1;
    }

    if (ts.tv_sec < -1) {
        // date before EPOCH, we cannot update REALTIME CLOCK with such value
        return -2;
    }

    if (ts.tv_sec > (INT_MAX - 31536000)) {  // (2**31-1) -(seconds in a year))
        // date to close to 2038
        return -3;
    }

    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        vapi_error("%s clock_settime failed : %s", __func__, strerror(errno));
        return -4;
    }

    return 0;
}

int vtime_get_wallclock_string(char *buf, int len)
{
    struct timespec ts;

    if (vtime_get_wallclock(&ts) != 0) {
        return -1;
    }

    return vtime_time_date_str(buf, len, &ts);
}

int vtime_time_date_str(char *buf, int len, struct timespec *time)
{
    int ret;
    struct tm t;

    tzset();
    if (localtime_r(&(time->tv_sec), &t) == NULL) {
        return -1;
    }

    ret = strftime(buf, len, "%d/%m/%Y-%H:%M:%S", &t);
    if (ret == 0) {
        return -1;
    }
    len -= ret;

    ret = snprintf(&buf[ret], len, ".%06ld", time->tv_nsec / 1000);
    if (ret >= len) {
        return -1;
    }

    return 0;
}

static const char *gc_timezone_file = "/etc/localtime";
static const int gc_max_tz_file_path_length = 1024;

struct _vtime_ctx {
    vevent_cb_t user_cb;
    void *user_ctx;
};

/*!
 * Dummy data reception handler used to debug ysystem calls.
 */
static void _on_data(char *data, int data_length, void *cookie)
{
    data[data_length] = '\0';
    vapi_debug("_on_data : ['%s']", data);
}

/*!
 * Generic callback used for vtime_set_timezone.
 */
static void _on_terminate_tz(vevent_reason_t reason, int status, void *cookie)
{
    struct _vtime_ctx *ctx = (struct _vtime_ctx *)cookie;

    if (reason != VEVENT_OCCURED) {
        vapi_error("Timezone modification failed (reason = %d / status = %d).", reason, status);
    }

    if (ctx->user_cb) {
        ctx->user_cb(reason, ctx->user_ctx);
    }

    vmem_free(vmem_alloc_default(), ctx);
}

/*!
 * Link the next timezone file according to the context index.
 * \param ctx IN The timezone set context.
 */
static vevent_t *_link_timezone_file(struct _vtime_ctx *ctx, const char *tz_name)
{
    char tz_file[gc_max_tz_file_path_length];
    snprintf(tz_file, gc_max_tz_file_path_length - 1, "/usr/share/zoneinfo/%s", tz_name);

    const char *cmd[] = { "ln", "-nsf", tz_file, gc_timezone_file, 0 };

    return vsystem_exec((char **)cmd, _on_terminate_tz, _on_data, (void *)ctx);
}

vevent_t *vtime_set_timezone_name(vevent_cb_t user_cb, const char *tz_name, void *user_ctx)
{
    if (vtime_valid_timezone_name(tz_name) != 0) {
        vapi_error("invalid tz_name for vtime_set_timezone.");
        return NULL;
    }

    struct _vtime_ctx *ctx = (struct _vtime_ctx *)vmem_calloc(vmem_alloc_default(), sizeof(struct _vtime_ctx));
    if (ctx == NULL) {
        vapi_error("Could not allocate memory for vtime_set_timezone context.");
        return NULL;
    }

    ctx->user_cb = user_cb;
    ctx->user_ctx = user_ctx;

    vevent_t *event = _link_timezone_file(ctx, tz_name);
    if (event == NULL) {
        vapi_error("Could not run ysystem_exec for vtime_set_timezone.");
        vmem_free(vmem_alloc_default(), ctx);
        return NULL;
    }

    return event;
}

vevent_t *vtime_set_timezone_offset(vevent_cb_t user_cb, int tz_offset, void *user_ctx)
{
    char name[255] = {0};
    if (tz_offset < 0)
        snprintf(name, 254, "Etc/GMT+%i", -tz_offset);
    else if (tz_offset == 0)
        snprintf(name, 254, "Etc/GMT");
    else
        snprintf(name, 254, "Etc/GMT-%i", tz_offset);
    return vtime_set_timezone_name(user_cb, name, user_ctx);
}

int vtime_valid_timezone_name(const char *tz_name)
{
    // check that file exist /usr/share/zoneinfo/...
    struct stat buffer;
    char filename[255] = {0};
    snprintf(filename, 254, "/usr/share/zoneinfo/%s", tz_name);
    int rc = stat(filename, &buffer);
    if (rc == 0) {
        if (S_ISDIR(buffer.st_mode)) rc = -2;
    }
    return rc;
}

int vtime_valid_timezone_offset(int tz_offset)
{
    // check that file exist /usr/share/zoneinfo/...
    struct stat buffer;
    char filename[255] = {0};
    if (tz_offset < 0) {
        snprintf(filename, 254, "/usr/share/zoneinfo/Etc/GMT+%i", -tz_offset);
    } else if (tz_offset == 0) {
        snprintf(filename, 254, "/usr/share/zoneinfo/Etc/GMT");
    } else {
        snprintf(filename, 254, "/usr/share/zoneinfo/Etc/GMT-%i", tz_offset);
    }

    int rc = stat(filename, &buffer);
    if (rc == 0) {
        if (S_ISDIR(buffer.st_mode)) rc = -2;
    }
    return rc;
}

int vtime_get_uptime(struct timespec *time)
{
    int ret = -1;
    struct sysinfo info;

    if (time == NULL) {
        return -1;
    }

    ret = sysinfo(&info);
    if (ret == -1) {
        vapi_error("%s failed : %s", __func__, strerror(errno));
        return -1;
    }

    time->tv_sec = (time_t)info.uptime;
    time->tv_nsec = 0;

    return 0;
}

static void _check_time_cb(vtimer_t timer, void *ctx)
{
    struct timespec ts;
    vtime_update_ctx_t *update_ctx = (vtime_update_ctx_t *)ctx;

    if (vtime_get_wallclock(&ts) != 0) {
        vapi_error("%s: failed to retrieve wallclock time", __func__);
        return;
    }

    if (labs(ts.tv_sec - update_ctx->prev_ts.tv_sec) > 900) {
        time_t delta = ts.tv_sec - update_ctx->prev_ts.tv_sec;
        update_ctx->update_cb(delta, update_ctx->user_ctx);
    }
    update_ctx->prev_ts.tv_sec = ts.tv_sec;
}

int vtime_register_update_cb(vtime_update_cb_t on_update, void *user_ctx)
{
    struct timespec ts;
    vtime_update_ctx_t *update_ctx = (struct vtime_update_ctx *)vmem_calloc(vmem_alloc_default(), sizeof(struct vtime_update_ctx));

    if (update_ctx == NULL) {
        vapi_error("%s: failed memory allocation", __func__);
        return -1;
    }

    memset(update_ctx, 0, sizeof(vtime_update_ctx_t));

    if (vtime_get_wallclock(&ts) != 0) {
        vapi_error("%s: failed to retrieve wallclock time", __func__);
    }

    update_ctx->update_cb = on_update;
    update_ctx->user_ctx = user_ctx;
    update_ctx->prev_ts = ts;

    vtimer_t timer = vtimer_start_periodic(_check_time_cb, 10000, update_ctx);
    if (!timer) {
        vapi_error("Failed to start periodic timer for checking time updates");
        return -1;
    }

    return 0;
}

static void _check_timezone_cb(vtimer_t timer, void *ctx)
{
    vtime_tz_ctx_t *tz_ctx = (vtime_tz_ctx_t *)ctx;
    struct tm tz_tm;
    time_t tz_time, tz_time_orig;
    time_t delta = 0;

    vtime_get_wallclock_tm(&tz_tm);

    if (strcmp(tz_ctx->tz_tm.tm_zone, tz_tm.tm_zone) != 0) {
        vapi_debug("Time zone is changed from '%s' to '%s'.", tz_ctx->tz_tm.tm_zone, tz_tm.tm_zone);
        vtime_convert_wallclock_time_t(&tz_tm, &tz_time);
        vtime_convert_wallclock_time_t(&tz_ctx->tz_tm, &tz_time_orig);
        delta = tz_time - tz_time_orig - 2; // adjust for 2sec timer
        tz_ctx->tz_cb(tz_tm.tm_zone, delta, tz_ctx->user_ctx);
    }
    memcpy(&(tz_ctx->tz_tm), &tz_tm, sizeof(tz_tm));
}

int vtime_register_timezone_cb(vtime_timezone_cb_t on_timezone_change, void *user_ctx)
{
    vtime_tz_ctx_t *tz_ctx = (vtime_tz_ctx_t *)vmem_calloc(vmem_alloc_default(), sizeof(vtime_tz_ctx_t));
    if (tz_ctx == NULL) {
        vapi_error("%s: failed memory allocation", __func__);
        return -1;
    }

    struct tm tz_tm;
    vtime_get_wallclock_tm(&tz_tm);

    memset(tz_ctx, 0, sizeof(vtime_tz_ctx_t));
    tz_ctx->tz_cb = on_timezone_change;
    tz_ctx->user_ctx = user_ctx;
    memcpy(&(tz_ctx->tz_tm), &tz_tm, sizeof(tz_tm));

    vtimer_t timer = vtimer_start_periodic(_check_timezone_cb, 2000, tz_ctx);
    if (!timer) {
        vapi_error("Failed to start periodic timer for checking timezone updates");
        return -1;
    }

    return 0;
}
