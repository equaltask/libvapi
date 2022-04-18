#ifndef __VLOG_CORE_H__
#define __VLOG_CORE_H__

#include <stdio.h>
#include <pthread.h>
#include <libvapi/vlog.h>
#include <libvapi/vlist.h>
#include <libvapi/vlog_file.h>

#include "vlog_syslog.h"
#include "vlog_format.h"

#define MODULE_PLATFORM_INIT_ID     0

#define VLOG_MAX_MSG_SIZE           255
#define VLOG_MAX_TAGS_SIZE          127
#define VLOG_MAX_SYSLOG_MSG_SIZE    1280
#define VLOG_MAX_MAPS_SIZE          4096
#define VLOG_MAX_MAPS_LINE_LEN      256

#define MAX_ENTRIES_PER_FILE        1000
#define MAX_NBR_FILES               10
#define MAX_OUTPUT_LEVEL_LEN        64
#define MAX_OUTPUT_FORMAT_LEN       256

#define VLOG_DISABLED_STR       "VLOG_DISABLED"
#define VLOG_ENABLED_STR        "VLOG_ENABLED"
#define VLOG_CRITICAL_STR       "VLOG_CRITICAL"
#define VLOG_ERROR_STR          "VLOG_ERROR"
#define VLOG_WARNING_STR        "VLOG_WARNING"
#define VLOG_INFO_STR           "VLOG_INFO"
#define VLOG_DEBUG_STR          "VLOG_DEBUG"

#ifdef __cplusplus
extern "C" {
#endif

static const char vlog_output_str[][16] = {
    "LOGFILE",
    "SYSLOG",
    "CONSOLE",
    "TNDD",
    "ERRORFILE",
    "OPENTRACING"
};

/* indexes for the logging output devices */
typedef enum vlog_output {
    VLOG_LOGFILE_INDEX = 0,
    VLOG_SYSLOG_INDEX,
    VLOG_CONSOLE_INDEX,
    VLOG_TNDD_INDEX,
    VLOG_ERRORFILE_INDEX,
    VLOG_OPENTRACING_INDEX,
    VLOG_DEST_MAX //last element
} vlog_output_t;

static const char vlog_vapi_component_str[][16] = {
    "YIPC",
    "YPROTO",
    "YTND",
    "YMUTEX",
    "YTIMER",
    "YLOOP"
};

typedef struct vlog_update_ctx {
    vlog_update_cb_t user_cb;
    void *user_ctx;
} vlog_update_ctx_t;


typedef struct vlog_module {
    uint32_t            m_logid;                    /* log id */
    char                m_name[VLOG_MAX_MOD_NAME];  /* module name */
    vlog_level_t        m_level;                    /* log level*/
    struct vlist        m_node;                     /* module node for module_list */
    //vlog_format_t*      m_format;                   /* format of the trace for this module */
    vlog_category_t     m_category;                 /* log category */
    vlog_update_ctx_t*  m_update_ctx;               /* log update callback context*/
} vlog_module_t;


typedef struct vlog_internal_config
{
    int                           m_initialized;
    char                          m_module_name[VLOG_MAX_MOD_NAME];
    int                           m_errorfile;
    int                           m_logfile;
    int                           m_logsys;
    int                           m_logoperator;
    int                           m_opentracing;
    int                           m_nbr_modules;
    struct vlist                  m_module_list;
    int                           m_destlevel[VLOG_DEST_MAX];
    int                           m_destinit[VLOG_DEST_MAX];
    vlog_format_t*                m_destformat[VLOG_DEST_MAX];
    vlog_file_type_t              m_vlogfile;
    vlog_file_type_t              m_yerrorfile;
    vlog_syslog_type_t            m_ysyslog;
    vlog_level_t                  m_default_loglevel;
    int                           m_set_loglevel_from_output_level;
} vlog_internal_config_t;

/* log module support */
int vlog_init(void);
void vlog_set_default_threshold(vlog_output_t output, vlog_level_t thresh, vlog_category_t category);
int vlog_get_nbr_modules(void);
int vlog_get_default_loglevel(void);
int vlog_get_module_by_name(const char *name, vlog_module_t **mod);
int vlog_module_get_data(vlog_id_t id, char name[VLOG_MAX_MOD_NAME], int *level, const char **format);
int vlog_output_get_data(vlog_output_t id, char name[VLOG_MAX_MOD_NAME], int *level, const char **format);
int vlog_status_get_data(vlog_vapi_component_t id, char name[VLOG_MAX_MOD_NAME], vlog_status_t *status);
int vlog_output_get_max_entries(vlog_output_t id);
int vlog_output_get_max_files(vlog_output_t id);
int vlog_output_set_opentracing_status(int id, int status);
int vlog_output_set_status(int id, int status);
int vlog_output_set_loglevel(int id, int value);
int vlog_output_set_format(int id, const char *fmt);
int vlog_module_config_loglevel(vlog_id_t id, int value);
int vlog_module_set_format(vlog_id_t id, const char *fmt);
int vlog_output_set_max_entries(int id, int value);
int vlog_output_set_max_files(int id, int value);
int vlog_output_cleanup_files(int id);

void vlog_output_trace(const char *span_name, const char *msg);
void vlog_output_error_record(const char *msg, vlog_level_t level, unsigned long type, const char *app, const char *file, int line);
ptrdiff_t vlog_strn_cleanup_and_trim(char *str, ptrdiff_t max_chars);

int vlog_get_maps_dump(char* buffer);

vlog_output_t vlog_output_to_id(char *output);
vlog_level_t vlog_level_from_str(const char * level);
vlog_vapi_component_t vlog_component_to_id(char* component);
vlog_status_t vlog_status_from_str(const char* status);

/*!
 * \brief  Log an error message from an asynchronous signal handler.
 *         This function is async-signal-safe and will do restricted
 *         error reporting:
 *           - write the message to console (possibly messing the
 *             output of the interrupted synchronous execution flow)
 *           - write the message to the error records log file (idem)
 * \param  str  IN  Error message
 */
void vlog_print_error_asyncsignalsafe(const char *str);

void vlog_set_static_tags(void);

int vlog_output_enabled(int output);

int vlog_print_error_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
