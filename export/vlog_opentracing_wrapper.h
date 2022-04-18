#ifndef __VLOG_OPENTRACING_WRAPPER_H__
#define __VLOG_OPENTRACING_WRAPPER_H__

#include <libvapi/vlog.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char vlog_opentracing_context;
typedef char *vlog_opentracing_context_ptr;

int vlog_create_tracer(
    const char *service_name,
    const char *server_address,
    const unsigned int server_port,
    const unsigned int spans_nmb_per_flush,
    const unsigned int delayPerFlush,
    void (*createTimer)(time_t, void *, void (*callback)(void *)));
int vlog_close_tracer();

int vlog_start_parent_span(const char *span_name);
int vlog_start_child_span(const char *span_name, const vlog_opentracing_context_ptr jsonopentracer_parent_context, const int jsonopentracer_parent_context_size);
int vlog_start_follows_from_span(const char *span_name, const vlog_opentracing_context_ptr jsonopentracer_parent_context, const int jsonopentracer_parent_context_size);

bool vlog_is_parent_span(const char *span_name);

int vlog_finish_span(const char *span_name);

int vlog_record_baggage(const char *span_name, const char *baggage_name, const char *baggage_value);
int vlog_record_tag(const char *span_name, const char *tag_name, const char *tag_value);
int vlog_record_log(const char *span_name, const char *log_fctn_name, const char *log_level, const char *log_msg);

int vlog_get_span_context_size(const char *span_name);
vlog_opentracing_context_ptr vlog_get_span_context(const char *span_name);

#ifdef __cplusplus
}
#endif

#endif //__VLOG_OPENTRACING_WRAPPER_H__
