
#ifndef __VLOG_FORMAT_H__
#define __VLOG_FORMAT_H__

#include "bufprintf.h" // buffer_t
//#include "generated/vlog_tags_values.h" // vlog_tags_t

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
    const char *TAG_name;
    char TAG_name_is_set;
    const char* TAG_name_prefix;
    int TAG_LEVEL;
} vlog_tags_t;

extern __thread vlog_tags_t vlog_tags;

struct vlog_format;
typedef struct vlog_format vlog_format_t;

vlog_format_t * vlog_format_compile(const char *fmt, int set_prefix);
void vlog_format_free(vlog_format_t *fmt);
int vlog_format(buffer_t *buf, vlog_format_t *fmt, const char *msg, vlog_tags_t *tags);
const char * vlog_format_get_string(vlog_format_t *fmt);
int vlog_format_get_len(vlog_format_t *fmt, const char *msg, vlog_tags_t *tags);

#ifdef __cplusplus
}
#endif

#endif // __VLOG_FORMAT_H__
