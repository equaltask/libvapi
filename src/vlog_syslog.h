#ifndef __VLOG_SYSLOG_H__
#define __VLOG_SYSLOG_H__

#include <stdint.h>
#include <libvapi/vlog.h>
#include <libvapi/vmutex.h>
#include <libvapi/vtimer.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VLOG_MAX_SYSLOG_ADDR 32

typedef struct vlog_syslog_type {
    int fd;
    char local_addr[VLOG_MAX_SYSLOG_ADDR];
    int local_port;
    vtimer_t conn_timer;
    vthread_mutex_t mutex;
} vlog_syslog_type_t;

int vlog_syslog_init(vlog_syslog_type_t *handle);
int vlog_syslog_print(vlog_syslog_type_t *handle, int syslog_pri, const char *msg);
int vlog_syslog_close(vlog_syslog_type_t *handle);

#ifdef __cplusplus
}
#endif

#endif
