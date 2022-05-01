#ifndef __VLOG_OPENTRACING__
#define __VLOG_OPENTRACING__

#include <libvapi/vlog.h>
#include <libvapi/vtimer.h>
#include <libvapi/vtypes.h>

//#include "generated/vlog_tags_values.h"

#include <stdint.h>

#ifdef __cplusplus

extern "C" {
#endif

typedef struct vlog_opentracing_connection {
    char service_name[VLOG_MAX_FILENAME + 1];
    char server_address[VLOG_MAX_FILENAME + 1];
    int server_port;
    int spans_nmb_per_flush;
    int delay_per_flush_in_second;
} vlog_opentracing_connection_t;

int vlog_opentracing_open(vlog_opentracing_connection_t* handle);
int vlog_opentracing_print(const char* span_name, vlog_tags_t* tags, const char* msg);
int vlog_opentracing_close();

#ifdef __cplusplus
}
#endif

#endif //__VLOG_OPENTRACING__
