
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>

#include <libvapi/vlog.h>
#include <libvapi/vmutex.h>
//#include <libvapi/yconfig_json.h>
#include <libvapi/vtime.h>
#include <libvapi/vmem.h>
#include <libvapi/vthread.h> // vthread_getselfname
#include <libvapi/vloop.h> // vloop_get_application_name
#include <libvapi/vlog_file.h>
#include <libvapi/vtnd.h>
#include <libvapi/vtnd_log.h>

#include "vlog_vapi.h"
#include "vlog_core.h"
#include "vlog_syslog.h"
#include "vlog_format.h"
//#include "vlog_dbg.h"
#include "verror.h"
#include "bufprintf.h"
//#include "generated/vlog_tags_values.h"
//#include "vlog_tags_filters.h"
#include "vlog_vapi.h"

#define DEFAULT_HOMEDIR             "./"
#define MODULE_PLATFORM             "platform"

#define CLASS_PRIORITY_IS_ERROR_LEVEL(level) \
    (level <= VLOG_ERROR)
#define LEVEL_ENABLED_ON_OUTPUT(_out, _lvl) \
    (log_config.m_destinit[_out] && _lvl <= log_config.m_destlevel[_out])
#define OUTPUT_ENABLED(_out) \
    (log_config.m_destinit[_out] && log_config.m_destlevel[_out] != VLOG_DISABLED)
#define LEVEL_ENABLED_ON_MODULE(_mod, _lvl) \
    (_mod->m_level != VLOG_DISABLED && _lvl <= _mod->m_level)

#define BUILD_FRMT_STR( x)   #x
#define STR_FMT(x)          "%lx-%lx %*[^/]%" BUILD_FRMT_STR(x) "s"
#define MAPS_SCAN_FORMAT    STR_FMT(127)  /* VLOG_MAX_FILENAME-1 */

static vlog_internal_config_t log_config = {
    .m_initialized = 0,
    .m_module_name = "",
    .m_errorfile = 0,
    .m_logfile = 0,
    .m_logsys = 0,
    .m_logoperator = 0,
    .m_opentracing = 0,
    .m_nbr_modules = 6, /* 0 is platform; 1-4 are reserved, see trc_vprintf in ylegacy.c */
    .m_module_list.next = (struct vlist *) (((void *)&log_config) + offsetof(vlog_internal_config_t, m_module_list)),
    .m_module_list.prev = (struct vlist *) (((void *)&log_config) + offsetof(vlog_internal_config_t, m_module_list)),
    .m_destlevel[VLOG_LOGFILE_INDEX] = VLOG_DISABLED,
    .m_destlevel[VLOG_SYSLOG_INDEX] = VLOG_DISABLED,
    .m_destlevel[VLOG_CONSOLE_INDEX] = VLOG_WARNING,
    .m_destlevel[VLOG_TNDD_INDEX] = VLOG_DISABLED,
    .m_destlevel[VLOG_ERRORFILE_INDEX] = VLOG_DISABLED,
    .m_destlevel[VLOG_OPENTRACING_INDEX] = VLOG_DISABLED,
    .m_destinit[VLOG_LOGFILE_INDEX] = 0,
    .m_destinit[VLOG_SYSLOG_INDEX] = 0,
    .m_destinit[VLOG_CONSOLE_INDEX] = 1,
    .m_destinit[VLOG_TNDD_INDEX] = 0,
    .m_destinit[VLOG_ERRORFILE_INDEX] = 0,
    .m_destinit[VLOG_OPENTRACING_INDEX] = 0,
    .m_destformat[VLOG_LOGFILE_INDEX] = NULL,
    .m_destformat[VLOG_SYSLOG_INDEX] = NULL,
    .m_destformat[VLOG_CONSOLE_INDEX] = NULL,
    .m_destformat[VLOG_TNDD_INDEX] = NULL,
    .m_destformat[VLOG_OPENTRACING_INDEX] = NULL,
    .m_vlogfile = {
        .pathname = DEFAULT_HOMEDIR,
        .filename = "",
        .maxentries = MAX_ENTRIES_PER_FILE,
        .nb_logfiles_max = MAX_NBR_FILES,
        .fs_size_hwm = 0,
        .fs_size_lwm = 0,
        .currententries = 0
    },
    .m_yerrorfile = {
        .pathname = DEFAULT_HOMEDIR,
        .filename = "",
        .maxentries = MAX_ENTRIES_PER_FILE,
        .nb_logfiles_max = MAX_NBR_FILES,
        .fs_size_hwm = 0,
        .fs_size_lwm = 0,
        .currententries = 0
    },
    .m_ysyslog.fd = 0,
    .m_ysyslog.local_addr = "",
    .m_ysyslog.local_port = 514,
    .m_default_loglevel = VLOG_DISABLED,
    .m_set_loglevel_from_output_level = 0
};

static vthread_mutex_t vlog_config_lock;
static volatile sig_atomic_t error_records_file_fd_flag = 0;
static int error_records_file_fd = -1;
static const int c_syslog_user_fac = 8;
static const int c_syslog_local1_fac = 136;

/*!
 * Global constructor for vlog module config mutex.
 */
__attribute__ ((constructor)) void _vlog_constructor(void)
{
    vmutex_create(&vlog_config_lock);
}

/******************************************************************************/

static bool _log_is_initialized(void)
{
    if (log_config.m_initialized == 0)
        return false;
    else
        return true;
}


static int vlog_is_error_level(vlog_level_t level)
{
    return CLASS_PRIORITY_IS_ERROR_LEVEL(level);
}


static int get_module_by_id(vlog_id_t log_id, vlog_module_t **mod)
{
    int ret = -1;
    vlog_module_t *tmp = NULL;
    struct vlist *nodep = NULL;

    if (mod != NULL)
        *mod = NULL;

    vlist_foreach(&(log_config.m_module_list),nodep) {
        tmp = container_of(vlog_module_t, m_node, nodep);
        if (log_id == tmp->m_logid) {
            ret = 0;
            if (mod != NULL)
                *mod = tmp;
            break;
        }
    }

    return ret;
}


int vlog_get_module_by_name(const char *name, vlog_module_t **mod)
{
    int ret = -1;
    vlog_module_t *tmp = NULL;
    struct vlist *nodep = NULL;

    if (mod != NULL)
        *mod = NULL;

    vlist_foreach(&(log_config.m_module_list),nodep) {
        tmp = container_of(vlog_module_t, m_node, nodep);
        if (strcmp(name, tmp->m_name) == 0) {
            ret = 0;
            if (mod != NULL)
                *mod = tmp;
            break;
        }
    }

    return ret;
}


/******************************************************************************/


static void vlog_install_default_filters(void)
{
    return;
}

static int vlog_format_trace(char *output, size_t output_size, const char *msg, int output_index)
{
    int linefeed;
    buffer_t buf;
    static const char *trunc = "<TRUNCATED>\n";

    if (LEVEL_ENABLED_ON_OUTPUT(output_index, vlog_tags.TAG_LEVEL)) {
        bufinit(&buf, output, output_size);
        vlog_format(&buf, log_config.m_destformat[output_index], msg, &vlog_tags);
        linefeed = bufprintf(&buf, "\n");
        if (linefeed == 0)  /* line feed could not be printed means that the buffer is full */
            sprintf(&output[output_size - strlen(trunc) - 1], "%s", trunc);

        return 0;
    }

    return 1;
}

static int vlog_fulldump_format_trace(char **output, const char *msg, int output_index)
{
    char *result = NULL;
    buffer_t buf;
    int expanded_tags_len = -1;

    if (LEVEL_ENABLED_ON_OUTPUT(output_index, vlog_tags.TAG_LEVEL)) {
        expanded_tags_len = vlog_format_get_len(log_config.m_destformat[output_index], msg, &vlog_tags);
        if (expanded_tags_len == -1) {
            vapi_error("vlog_format_get_len failed");
            return 1;
        }

        result = vmem_calloc(vmem_alloc_default(), expanded_tags_len + 2); /* '\n' + '\0' */
        if (result == NULL) {
            vapi_error("vmem_calloc failed");
            return 1;
        }

        memset(result, 0, expanded_tags_len + 2);
        bufinit(&buf, result, expanded_tags_len + 2); /* '\n' + '\0' */
        vlog_format(&buf, log_config.m_destformat[output_index], msg, &vlog_tags);
        bufprintf(&buf, "\n");

        *output = result;
        return 0;
    }

    return 1;
}

void vlog_output_trace(const char *span_name, const char *msg)
{
    vlog_module_t *mod = NULL;
    buffer_t buf;
    char output[VLOG_MAX_TAGS_SIZE+VLOG_MAX_MSG_SIZE];
    char module_output[VLOG_MAX_TAGS_SIZE+VLOG_MAX_MSG_SIZE];
    int syslog_pri = c_syslog_local1_fac;

    memset(output, 0, sizeof(output));
    memset(module_output, 0, sizeof(module_output));

    bufinit(&buf, module_output, sizeof(module_output));
    get_module_by_id(vlog_tags.TAG_MODULE, &mod);
    vlog_format(&buf, mod ? mod->m_format : NULL, msg, &vlog_tags);

    if (vlog_format_trace(output, sizeof(output), module_output, VLOG_CONSOLE_INDEX) == 0)
        fputs(output, stdout);

    if (vlog_format_trace(output, sizeof(output), module_output, VLOG_LOGFILE_INDEX) == 0)
        vlog_file_write(&(log_config.m_vlogfile), output);

    if (vlog_format_trace(output, sizeof(output), module_output, VLOG_TNDD_INDEX) == 0)
        vtnd_log_write(output, vlog_tags.TAG_LEVEL);

    if (vlog_format_trace(output, sizeof(output), module_output, VLOG_SYSLOG_INDEX) == 0) {
        if (mod && mod->m_category == VLOG_OPERATOR) {
            syslog_pri = c_syslog_user_fac + (int)vlog_tags.TAG_LEVEL;
            vlog_syslog_print(&log_config.m_ysyslog, syslog_pri, output);
        }
        else {
            /* print internal logs as debug level to syslog only */
            syslog_pri += (int)vlog_tags.TAG_LEVEL;
            vlog_syslog_print(&log_config.m_ysyslog, syslog_pri, output);
        }
    }
}

static void vlog_fulldump_output_trace(const char *msg)
{
    vlog_module_t *mod = NULL;
    buffer_t buf;
    char *module_output = NULL;
    char *output = NULL;
    int msg_len = -1;
    int syslog_pri = c_syslog_local1_fac;

    get_module_by_id(vlog_tags.TAG_MODULE, &mod);
    msg_len = vlog_format_get_len(mod ? mod->m_format : NULL, msg, &vlog_tags);
    if (msg_len == -1) {
        vapi_error("vlog_format_get_len failed");
        return;
    }

    module_output = vmem_calloc(vmem_alloc_default(), msg_len + 2);  /* '\n' + '\0' */
    if (module_output == NULL) {
        vapi_error("vmem_calloc failed");
        return;
    }
    bufinit(&buf, module_output, msg_len + 2);  /* '\n' + '\0' */

    vlog_format(&buf, mod ? mod->m_format : NULL, msg, &vlog_tags);

    if (vlog_fulldump_format_trace(&output, module_output, VLOG_CONSOLE_INDEX) == 0) {
        fputs(output, stdout);
        vmem_free(vmem_alloc_default(), output);
    }

    if (vlog_fulldump_format_trace(&output, module_output, VLOG_LOGFILE_INDEX) == 0) {
        vlog_file_write(&(log_config.m_vlogfile), output);
        vmem_free(vmem_alloc_default(), output);
    }

    if (vlog_fulldump_format_trace(&output, module_output, VLOG_TNDD_INDEX) == 0) {
        vtnd_log_write(output, vlog_tags.TAG_LEVEL);
        vmem_free(vmem_alloc_default(), output);
    }

    if (vlog_fulldump_format_trace(&output, module_output, VLOG_SYSLOG_INDEX) == 0) {
        if (mod && mod->m_category == VLOG_OPERATOR) {
            syslog_pri = c_syslog_user_fac + (int)vlog_tags.TAG_LEVEL;
            vlog_syslog_print(&log_config.m_ysyslog, syslog_pri, output);
        }
        else {
            /* print internal logs as debug level to syslog only */
            syslog_pri += (int)vlog_tags.TAG_LEVEL;
            vlog_syslog_print(&log_config.m_ysyslog, syslog_pri, output);
        }
        vmem_free(vmem_alloc_default(), output);
    }

    vmem_free(vmem_alloc_default(), module_output);
}

void vlog_output_error_record(const char *msg, vlog_level_t level, unsigned long type,
                              const char *app, const char *file, int line)
{
    char error_record[VERROR_MAX_RECORD_SIZE];
    memset(error_record, 0, sizeof(error_record));

    verror_str(error_record, level, type, app, file, line, 0, "%s", msg);

    /* send to console */
    if (OUTPUT_ENABLED(VLOG_CONSOLE_INDEX))
        fputs(error_record, stdout);

    /* send to operator tnd daemon */
    if (LEVEL_ENABLED_ON_OUTPUT(VLOG_TNDD_INDEX, VLOG_WARNING))
        vtnd_log_write_error(error_record, level);

    /* send to file in ramfs */
    if (OUTPUT_ENABLED(VLOG_ERRORFILE_INDEX))
        vlog_file_write(&(log_config.m_yerrorfile), error_record);
}


/******************************************************************************/
/* returns the number of registered modules */
int vlog_get_nbr_modules(void)
{
    return log_config.m_nbr_modules;
}

vlog_id_t vlog_module_get_logid(const char module[VLOG_MAX_MOD_NAME])
{
    vlog_module_t *mod = NULL;
    vlog_get_module_by_name(module, &mod);
    if (mod == NULL)
        return -1;
    else
        return mod->m_logid;
}

int vlog_output_enabled(int output)
{
    return (OUTPUT_ENABLED(output));
}

static int vlog_module_level_enabled(vlog_id_t log_id, vlog_level_t log_level)
{
    vlog_module_t *mod = NULL;

    get_module_by_id(log_id, &mod);
    if (mod == NULL)
        return 1;

    /* error levels always have output */
    if (vlog_is_error_level(log_level))
        return 0;

    if (LEVEL_ENABLED_ON_MODULE(mod, log_level))
        return 0;

    /* not enabled */
    return 1;
}

int vlog_level_enabled_on_vapi_component(vlog_vapi_component_t component_id)
{
    return 0;
}

/* scan the line for useful maps info, and put it in output if found
 * Returns the length of output string,
 *         0 if the input does not hold any executable zones load info
 */
static size_t format_line(char * input, char *output)
{
   unsigned long int start_offset, end_offset;
   char elf_path[VLOG_MAX_FILENAME]={'\0'};

   /* Keep executable zones only, except vdso */
   if ((strstr(input, "r-xp") == NULL) || (strstr(input, "[vdso]") != NULL))
       return 0;

   /* Expected format :
    * b74d6000-b767f000 r-xp 00000000 00:01 114        /lib/libc-2.21.so */
   sscanf(input, MAPS_SCAN_FORMAT, &start_offset, &end_offset, elf_path);
   sprintf(output, "%lx-%lx %s\n", start_offset, end_offset, elf_path);

   return strlen(output);
}

/* Dump the executable zones from the maps file to the provided buffer
 * Returns the size of the dumped buffer
 *         0 if an error occured
 */
int vlog_get_maps_dump(char* buffer)
{
    char *current_line=NULL;
    size_t len, dummy=0, current_pos;
    char maps_path[VLOG_MAX_FILENAME] = {0};
    FILE *fp;

    /* Open maps file */
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", getpid());

    fp = fopen(maps_path, "r");
    if (fp == NULL) {
        vlog_print_error_asyncsignalsafe("Unable to open maps file !!");
        return 0;
    }

    sprintf(buffer, "\n******** MAPS ENTRIES *********\n");

    current_pos = strlen(buffer);
    /* Filter maps lines */
    while (!feof(fp)) {
        len = getline(&current_line, &dummy, fp);
        if (len > 0) {
            len = format_line(current_line, buffer + current_pos);
            current_pos += len;
        }
        if (current_line != NULL) {
            free(current_line);
            current_line = NULL; /* Let getline() allocate */
            dummy = 0;           /* the needed buffer */
        }
        /* Truncate the maps dump if we go bigger than VLOG_MAX_MAPS_SIZE */
        if ((current_pos + VLOG_MAX_MAPS_LINE_LEN) > VLOG_MAX_MAPS_SIZE)
            break;
    }
    sprintf(buffer + current_pos, "********** MAPS END **********\n");

    if (fp)
        fclose(fp);

    return strlen(buffer);
}

/*
 * Dump executable zones entries from maps file into the vlog_file
 * Returns 0 if successful
 *        -1 if an error occured
 */
int vlog_dump_maps_to_log_file(struct vlog_file_type* handle)
{
    char buffer[VLOG_MAX_MAPS_SIZE] = {'\0'};
    int  maps_size;

    if (handle == NULL) {
        vapi_warning("Trying to dump maps into a NULL file handle !\n");
        return -1;
    }

    maps_size = vlog_get_maps_dump(buffer);
    if (maps_size > 0) {
        vlog_file_write(handle, buffer);
        return 0;
    }
    else
        return -1;
}

/* open all log infrastructure (file,console,operator,syslog,opentracing) depending on 'destination' variable */
static int vlog_core_open(void)
{
    int rc = 0, status = 0;

    /* initialize console output */
    setvbuf(stdout,NULL,_IOLBF,0);

    /* initialize logging output file */
    if (!log_config.m_logfile && strlen(log_config.m_vlogfile.filename) > 0) {
        rc = vlog_file_open(&(log_config.m_vlogfile));
        if (rc == 0) {
            log_config.m_logfile = 1;
            log_config.m_destinit[VLOG_LOGFILE_INDEX] = 1;
        } else {
            vapi_warning("error: opening log file %s.\n", log_config.m_vlogfile.filename);
            status = -1;
        }
    }

    /* initialize error record output file */
    if (!log_config.m_errorfile && strlen(log_config.m_yerrorfile.filename) > 0) {
        log_config.m_yerrorfile.force_rotate = 1;
        rc = vlog_file_open(&(log_config.m_yerrorfile));
        if (rc == 0) {
            log_config.m_errorfile = 1;
            log_config.m_destinit[VLOG_ERRORFILE_INDEX] = 1;
            log_config.m_destlevel[VLOG_ERRORFILE_INDEX] = VLOG_ERROR;
            error_records_file_fd = vlog_file_get_fd(&(log_config.m_yerrorfile));
            error_records_file_fd_flag = 1;
            log_config.m_yerrorfile.cb = vlog_dump_maps_to_log_file;
            vlog_dump_maps_to_log_file(&(log_config.m_yerrorfile));
        } else {
            vapi_warning("error: opening error file %s.\n", log_config.m_yerrorfile.filename);
            status = -1;
        }
    }

    /* close syslog if open */
    if (log_config.m_logsys) {
        vlog_syslog_close(&log_config.m_ysyslog);
        log_config.m_logsys = 0;
    }

    /* initialize syslog output */
    if (strlen(log_config.m_ysyslog.local_addr) != 0) {
        rc = vlog_syslog_init(&log_config.m_ysyslog);
        if (rc == 0) {
            log_config.m_logsys = 1;
            log_config.m_destinit[VLOG_SYSLOG_INDEX] = 1;
        } else {
            status = -1;
        }
    }

    // XXX status overwritten!!!
    return status;
}

static int set_module_json_config(const char module_name[VLOG_MAX_MOD_NAME], vlog_id_t *log_id)
{
    int r, rc = -1;
    struct yconfig_json *json_ctx;
    char level_str[MAX_OUTPUT_LEVEL_LEN];
    char format_str[MAX_OUTPUT_FORMAT_LEN];
    char node[VLOG_MAX_MOD_NAME+24];
    vlog_level_t level;

    json_ctx = yconfig_json_get_ctx();
    if (json_ctx == NULL)
        return -1;

    r = snprintf(node, sizeof(node), "vlog.modules.%s.threshold", module_name);
    if (r > 0 && (unsigned)r < sizeof(node)) {
        r = yconfig_json_value_get_str(json_ctx, node, level_str, MAX_OUTPUT_LEVEL_LEN);
        if (r == 0) {
            level = vlog_level_from_str(level_str);
            if (level != 0) {
                rc = vlog_module_config_loglevel(*log_id, level);
            }
        }
    }

    r = snprintf(node, sizeof(node), "vlog.modules.%s.format", module_name);
    if (r > 0 && (unsigned)r < sizeof(node)) {
        r = yconfig_json_value_get_str(json_ctx, node, format_str, MAX_OUTPUT_FORMAT_LEN);
        if (r == 0) {
            rc = vlog_module_set_format(*log_id, format_str);
        }
    }

    return rc;
}

/* loads logging configuration from passed file */
static int vlog_core_load_config(struct yconfig_json *json_ctx)
{
    int ret = 0;
    char level[MAX_OUTPUT_LEVEL_LEN] = "";
    char format[MAX_OUTPUT_FORMAT_LEN] = "";
    int i = 0;

    ret = vmutex_lock(&vlog_config_lock);
    if (ret != 0)
        return -1;

    if (_log_is_initialized() == false) {
       vapi_warning("error: vlog not initialized yet [%s]",__func__);
       ret = -1;
       goto exit_vlog_config_load;
    }

    /* set log levels */
    const char *vlog_level_cfg[] = {
        "vlog.outputs.file.threshold",
        "vlog.outputs.syslog.threshold",
        "vlog.outputs.console.threshold",
        "vlog.outputs.tndd.threshold",
        "vlog.outputs.opentracing.threshold"
    };

    for (i = 0; i < VLOG_DEST_MAX - 1; ++i) {
        if (i != VLOG_ERRORFILE_INDEX) {
            yconfig_json_value_get_str(json_ctx, vlog_level_cfg[i], level, MAX_OUTPUT_LEVEL_LEN);

            int ret = 0;
            if (strncmp(VLOG_DISABLED_STR, level, MAX_OUTPUT_LEVEL_LEN) == 0)
                ret = vlog_output_set_loglevel(i, VLOG_DISABLED);
            else if (strncmp(VLOG_CRITICAL_STR, level, MAX_OUTPUT_LEVEL_LEN) == 0)
                ret = vlog_output_set_loglevel(i, VLOG_CRITICAL);
            else if (strncmp(VLOG_ERROR_STR, level, MAX_OUTPUT_LEVEL_LEN) == 0)
                ret = vlog_output_set_loglevel(i, VLOG_ERROR);
            else if (strncmp(VLOG_WARNING_STR, level, MAX_OUTPUT_LEVEL_LEN) == 0)
                ret = vlog_output_set_loglevel(i, VLOG_WARNING);
            else if (strncmp(VLOG_INFO_STR, level, MAX_OUTPUT_LEVEL_LEN) == 0)
                ret = vlog_output_set_loglevel(i, VLOG_INFO);
            else if (strncmp(VLOG_DEBUG_STR, level, MAX_OUTPUT_LEVEL_LEN) == 0)
                ret = vlog_output_set_loglevel(i, VLOG_DEBUG);

            if (ret < 0)
                vapi_error("warning: could not set output level %s for module %d", level, i);
        }
    }

    /* set formats */
    const char *vlog_format_cfg[] = {
        "vlog.outputs.file.format",
        "vlog.outputs.syslog.format",
        "vlog.outputs.console.format",
        "vlog.outputs.tndd.format"
    };

    for (i = 0; i < VLOG_DEST_MAX - 1; ++i) {
        yconfig_json_value_get_str(json_ctx, vlog_format_cfg[i], format, MAX_OUTPUT_FORMAT_LEN);
        vlog_output_set_format(i, format);
    }

    /* set vlogfile output configuration */
    yconfig_json_value_get_str(json_ctx, "vlog.outputs.file.name", log_config.m_vlogfile.filename, VLOG_MAX_FILENAME);
    yconfig_json_value_get_str(json_ctx, "vlog.outputs.file.directory", log_config.m_vlogfile.pathname, VLOG_MAX_FILENAME);
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.file.max_entries", &(log_config.m_vlogfile.maxentries));
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.file.max_files", &(log_config.m_vlogfile.nb_logfiles_max));
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.file.disk_spaceHWM", &(log_config.m_vlogfile.fs_size_hwm));
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.file.disk_spaceLWM", &(log_config.m_vlogfile.fs_size_lwm));

    /* set yerrorfile output configuration */
    yconfig_json_value_get_str(json_ctx, "vlog.outputs.error.name", log_config.m_yerrorfile.filename, VLOG_MAX_FILENAME);
    yconfig_json_value_get_str(json_ctx, "vlog.outputs.error.directory", log_config.m_yerrorfile.pathname, VLOG_MAX_FILENAME);
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.error.max_entries", &(log_config.m_yerrorfile.maxentries));
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.error.max_files", &(log_config.m_yerrorfile.nb_logfiles_max));
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.error.disk_spaceHWM", &(log_config.m_yerrorfile.fs_size_hwm));
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.error.disk_spaceLWM", &(log_config.m_yerrorfile.fs_size_lwm));

    yconfig_json_value_get_str(json_ctx, "vlog.outputs.syslog.local_addr", log_config.m_ysyslog.local_addr, VLOG_MAX_SYSLOG_ADDR);
    yconfig_json_value_get_int(json_ctx, "vlog.outputs.syslog.local_port", &(log_config.m_ysyslog.local_port));

    log_config.m_vlogfile.currententries = 0;
    log_config.m_yerrorfile.currententries = 0;

    /* Check to set json threshold configuration for early registered modules */
    vlog_module_t *tmp = NULL;
    struct vlist *nodep = NULL;

    vlist_foreach(&(log_config.m_module_list),nodep) {
        tmp = container_of(vlog_module_t, m_node, nodep);
        set_module_json_config(tmp->m_name, (vlog_id_t *)&tmp->m_logid);
    }

exit_vlog_config_load:
    vmutex_unlock(&vlog_config_lock);
    return ret;
}

/* internal function for creation of log module */
static int __vlog_module_register(const char module_name[VLOG_MAX_MOD_NAME], vlog_id_t *log_id, vlog_category_t category)
{
    vlog_module_t *new_mod = NULL;

    vlog_get_module_by_name(module_name, &new_mod);
    if (new_mod != NULL) {
        vapi_warning("module already registered [%s]", module_name);
        return 0;
    }

    new_mod = vmem_calloc(vmem_alloc_default(), sizeof(vlog_module_t));
    if (new_mod == NULL) {
        vapi_error("malloc [%s]", strerror(errno));
        return -1;
    }

    vlist_init(&(new_mod->m_node));
    vlist_add_tail(&(log_config.m_module_list),&(new_mod->m_node));
    strncpy(new_mod->m_name, module_name, VLOG_MAX_MOD_NAME-1);
    new_mod->m_logid = log_config.m_nbr_modules;
    new_mod->m_level = log_config.m_default_loglevel;
    new_mod->m_format = NULL;
    new_mod->m_update_ctx = NULL;
    new_mod->m_category = category;
    log_config.m_nbr_modules++; /* incremented with every registered module */

    *log_id = new_mod->m_logid;

    return 0;
}

/* creates and initializes a new module */
int vlog_module_register(const char module_name[VLOG_MAX_MOD_NAME], vlog_id_t *log_id)
{
    int ret = 0;

    ret = vmutex_lock(&vlog_config_lock);
    if (ret != 0)
        goto exit_register;

    ret = __vlog_module_register(module_name, log_id, VLOG_INTERNAL);

    vmutex_unlock(&vlog_config_lock);

    if (ret != 0)
        goto exit_register;

    set_module_json_config(module_name, log_id);

exit_register:
    return ret;
}

int vlog_module_config_loglevel(vlog_id_t id, int value)
{
    int ret = 0;
    vlog_module_t *mod = NULL;

    /* check params */
    ret = get_module_by_id(id, &mod);
    if (ret != 0) {
        vapi_warning("error: log id [%d] not found",(int)id);
        ret = -1;
        goto exit_set_loglevel;
    }

    if (mod->m_level == value) {
        vapi_warning("log id [%d] log level already [%s], not updated",(int)id, vlog_level_to_str(value));
        ret = 0;
        goto exit_set_loglevel;
    } else if (value < VLOG_DISABLED || value >= VLOG_LEVEL_MAX || value == 0 || value == 1 || value == 5) {
        /* if value is out of bound, or unsupported level (0, 1, 5) */
        vapi_warning("error: set log level for id [%d] invalid value [%d]",(int)id,value);
        ret = 0;
        goto exit_set_loglevel;
    } else {
        mod->m_level = value;
        if (mod->m_update_ctx && mod->m_update_ctx->user_cb)
            mod->m_update_ctx->user_cb(id, mod->m_level, mod->m_update_ctx->user_ctx);
    }

    ret = 0;

exit_set_loglevel:
    return ret;
}

int vlog_module_set_loglevel(vlog_id_t id, int value)
{
    struct vtnd_ctx *local_tnd_ctx = NULL;

    if (vtnd_get_base(&local_tnd_ctx) != 0 || local_tnd_ctx == NULL
        || local_tnd_ctx->cur_req == NULL) {
        vapi_error("not allowed to call outside of tnd request");
        return -1;
    }

    return vlog_module_config_loglevel(id, value);
}

static int vlog_modules_update_loglevel(vlog_level_t level, vlog_category_t category, vlog_level_t *output_level)
{
    vlog_module_t *tmp = NULL;
    struct vlist *nodep = NULL;
    int cnt = 0;

    vlist_foreach(&(log_config.m_module_list),nodep) {
        tmp = container_of(vlog_module_t, m_node, nodep);
        if (tmp->m_category == category)
            tmp->m_level = level;

        if (tmp->m_level > log_config.m_default_loglevel) {
            cnt++;
            if (tmp->m_level > *output_level)
                *output_level = tmp->m_level;
        }
    }

    return cnt;
}

void vlog_set_default_threshold(vlog_output_t output, vlog_level_t thresh, vlog_category_t category)
{
    int rc = 0;
    vlog_level_t output_level = VLOG_DISABLED;

    /* modules can be initialized with passed log level */
    if (thresh != VLOG_DISABLED) {
        log_config.m_default_loglevel = thresh;

        if (vlist_count(&(log_config.m_module_list)) > 0)
            rc = vlog_modules_update_loglevel(thresh, category, &output_level);

        if (rc > 0)
            log_config.m_destlevel[output] = output_level;
        else
            log_config.m_destlevel[output] = thresh;
    }
}

/* main init fuction, to be called once during main init (vloop) */
int vlog_init(void)
{
    struct yconfig_json *json_ctx = NULL;

    if (_log_is_initialized()) // already initialized
        return 0;

    /* init tag values */
    vlog_tags_clear();

    log_config.m_initialized = 1;

    /* if json_ctx initialized, read and initialize vlog_internal_config */
    json_ctx = yconfig_json_get_ctx();
    if (json_ctx == NULL)
        vapi_info("application uses default vlog configuration\n");
    else
        vlog_core_load_config(json_ctx);

    /* in case the default threshold is lower than the console or file threshold, enable it (e.g. --debug) */
    if (log_config.m_default_loglevel > log_config.m_destlevel[VLOG_CONSOLE_INDEX]) {
        log_config.m_destlevel[VLOG_CONSOLE_INDEX] = log_config.m_default_loglevel;
        vapi_info("setting console log level to [%s]", vlog_level_to_str(log_config.m_default_loglevel));
    }
    if (log_config.m_default_loglevel > log_config.m_destlevel[VLOG_LOGFILE_INDEX]) {
        log_config.m_destlevel[VLOG_LOGFILE_INDEX] = log_config.m_default_loglevel;
        vapi_info("setting file log level to [%s]", vlog_level_to_str(log_config.m_default_loglevel));
    }

    /* initialize trace and debug module */
    vlog_dbg_init_module();

    vlog_install_default_filters();

    /* open all required log facilities XXX error checking */
    return vlog_core_open();
}

int vlog_deinit(void)
{
    // TODO
    return 0;
}

ptrdiff_t vlog_strn_cleanup_and_trim(char *str, ptrdiff_t max_chars)
{
    char *c = str;
    char *last = c;

    while ((*c != '\0') && ((c - str) < max_chars)) {
        if (!isprint(*c))
            *c = ' ';
        else if (!isspace(*c))
            last = c;

        c++;
    }

    *(++last) = '\0';

    return (c - last);
}

void __vlog_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);

    __vlog_vprintf(fmt, args);

    va_end(args);
}

void __vlog_vprintf(const char *fmt, va_list ap)
{
    char msg[VLOG_MAX_MSG_SIZE];
    int is_error, is_filtered;

    memset(msg, 0, sizeof(msg));

    is_error = vlog_is_error_level(vlog_tags.TAG_LEVEL);
    is_filtered = vlog_tags_filter();
    if (is_error || !is_filtered)
        vsnprintf(msg, sizeof(msg), fmt, ap);

    vlog_strn_cleanup_and_trim(msg, VLOG_MAX_MSG_SIZE - 1);

    if (!is_filtered)
        vlog_output_trace(NULL, msg);

    vlog_tags_clear();
}

void __vlog_printf_ot(const char *spanName, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);

    __vlog_vprintf_ot(spanName, fmt, args);

    va_end(args);
}

void __vlog_vprintf_ot(const char* spanName, const char *fmt, va_list ap)
{
    char msg[VLOG_MAX_MSG_SIZE];
    int is_error, is_filtered;

    is_error = vlog_is_error_level(vlog_tags.TAG_LEVEL);

    is_filtered = vlog_tags_filter();

    if (is_error || !is_filtered)
        vsnprintf(msg, sizeof(msg), fmt, ap);

    vlog_strn_cleanup_and_trim(msg, VLOG_MAX_MSG_SIZE - 1);

    if (!is_filtered)
        vlog_output_trace(spanName, msg);

    vlog_tags_clear();
}

void __vlog_hexdump(void *addr, size_t size)
{
    int ret = -1;
    int is_filtered;
    char *output = NULL;

    if (addr == NULL)
        return;

    is_filtered = vlog_tags_filter();

    ret = hexdump(addr, size, &output);
    if (ret < 0) {
        vapi_warning("hexdump fail");
        return;
    }

    if (!is_filtered)
        vlog_fulldump_output_trace(output);

    vlog_tags_clear();
    free(output);
}

void __vlog_print_full(const char *str, __attribute__((unused)) size_t len)
{
    int is_filtered;

    if (str == NULL)
        return;

    is_filtered = vlog_tags_filter();
    if (!is_filtered)
        vlog_fulldump_output_trace(str);

    vlog_tags_clear();
}

void vlog_print_error_asyncsignalsafe(const char *str)
{
    int len = strlen(str);

    /* write to console */
    write(STDERR_FILENO, "\n", 1);
    write(STDERR_FILENO, str, len);

    /* write to error records log file */
    if (error_records_file_fd_flag == 1) {
        error_records_file_fd = vlog_file_get_fd(&(log_config.m_yerrorfile));
        if (error_records_file_fd > 0) {
            write(error_records_file_fd, "\n", 1);
            write(error_records_file_fd, str, len);
        }
    }
}


const char* vlog_module_get_name(vlog_id_t log_id)
{
    vlog_module_t *tmp = NULL;
    struct vlist *nodep = NULL;

    if (log_id == 0)
        return MODULE_PLATFORM;

    vlist_foreach(&(log_config.m_module_list),nodep) {
        tmp = container_of(vlog_module_t, m_node, nodep);
        if (log_id == tmp->m_logid)
            return tmp->m_name;
    }

    return NULL;
}


const char * vlog_level_to_str(vlog_level_t level)
{
    switch (level) {
        case VLOG_DISABLED: return VLOG_DISABLED_STR;
        case VLOG_CRITICAL: return VLOG_CRITICAL_STR;
        case VLOG_ERROR:    return VLOG_ERROR_STR;
        case VLOG_WARNING:  return VLOG_WARNING_STR;
        case VLOG_INFO:     return VLOG_INFO_STR;
        case VLOG_DEBUG:    return VLOG_DEBUG_STR;
        default:            return "VLOG_LEVEL_UNKNOWN";
    }
}


const char* vlog_status_to_str(vlog_status_t status)
{
    switch (status) {
        case VLOG_STATUS_DISABLED: return VLOG_DISABLED_STR;
        case VLOG_STATUS_ENABLED:  return VLOG_ENABLED_STR;
        default:                   return "VLOG_STATUS_UNKNOWN";
    }
}


vlog_level_t vlog_level_from_str(const char * level)
{
    if (strcmp(level,VLOG_DISABLED_STR) == 0) return VLOG_DISABLED;
    else if (strcmp(level,VLOG_CRITICAL_STR) == 0) return VLOG_CRITICAL;
    else if (strcmp(level,VLOG_ERROR_STR) == 0) return VLOG_ERROR;
    else if (strcmp(level,VLOG_WARNING_STR) == 0) return VLOG_WARNING;
    else if (strcmp(level,VLOG_INFO_STR) == 0) return VLOG_INFO;
    else if (strcmp(level,VLOG_DEBUG_STR) == 0) return VLOG_DEBUG;
    else return 0;
}


vlog_status_t vlog_status_from_str(const char* status)
{
    if (strcmp(status, VLOG_DISABLED_STR) == 0) return VLOG_STATUS_DISABLED;
    else if (strcmp(status, VLOG_ENABLED_STR) == 0) return VLOG_STATUS_ENABLED;
    else return -1;
}

vlog_output_t vlog_output_to_id(char *output)
{
    int i = 0;
    vlog_output_t id = -1;

    for (i = 0; i < VLOG_DEST_MAX; ++i) {
        if (strcmp(vlog_output_str[i],output) == 0) {
            id = i;
            break;
        }
    }

    return id;
}


vlog_vapi_component_t vlog_component_to_id(char* component)
{
    int i = 0;
    vlog_vapi_component_t id = -1;

    for (i = 0; i < COMPONENT_DEST_MAX; ++i) {
        if (strcmp(vlog_vapi_component_str[i], component) == 0) {
            id = i;
            break;
        }
    }

    return id;
}


int vlog_module_get_data(vlog_id_t id, char name[VLOG_MAX_MOD_NAME], int *level, const char **format)
{
    int ret = 0;
    vlog_module_t *mod = NULL;

    ret = get_module_by_id(id, &mod);
    if (ret != 0)
        return -1;

    strcpy(name, mod->m_name);
    *level = mod->m_level;
    *format = vlog_format_get_string(mod->m_format);

    return 0;
}

int vlog_output_get_max_entries(vlog_output_t id)
{
    if (id == VLOG_LOGFILE_INDEX)
        return log_config.m_vlogfile.maxentries;
    else if (id == VLOG_ERRORFILE_INDEX)
        return log_config.m_yerrorfile.maxentries;
    else {
        vapi_warning("error: can only get max_entries value for file output");
        return -1;
    }
}

int vlog_output_get_max_files(vlog_output_t id)
{
    if (id == VLOG_LOGFILE_INDEX)
        return log_config.m_vlogfile.nb_logfiles_max;
    else if (id == VLOG_ERRORFILE_INDEX)
        return log_config.m_yerrorfile.nb_logfiles_max;
    else {
        vapi_warning("error: can only get max_files value for file output");
        return -1;
    }
}

int vlog_output_get_data(vlog_output_t id, char name[VLOG_MAX_MOD_NAME], int *level, const char **format)
{
    if (id >= VLOG_DEST_MAX)
        return -1;

    strcpy(name, vlog_output_str[id]);
    *level = log_config.m_destlevel[id];
    *format = vlog_format_get_string(log_config.m_destformat[id]);
    return 0;
}


int vlog_output_set_status(int id, int status)
{
    if (id >= VLOG_ERRORFILE_INDEX) {
        vapi_warning("warning: cannot set level of error record file");
        return -1;
    } else {
        log_config.m_destinit[id] = status;
        return 0;
    }
}

int vlog_output_set_loglevel(int id, int value)
{
    if (id == VLOG_ERRORFILE_INDEX) {
        vapi_warning("warning: cannot set level of error record file");
        return -1;
    } else if (value < VLOG_DISABLED || value >= VLOG_LEVEL_MAX || value == 0) {
        vapi_warning("error: set log level for id [%d] invalid value [%d]",(int)id,value);
        return -1;
    } else if (id == VLOG_TNDD_INDEX && value >= VLOG_DEBUG) {
        vapi_warning("error: can not set DEBUG level for TNDD logging");
        value = VLOG_INFO;
    }

    log_config.m_destlevel[id] = value;
    return 0;
}


int vlog_output_set_format(int id, const char *fmt_str)
{
    int set_prefix = 0;

    if (id == VLOG_SYSLOG_INDEX)
        set_prefix = 1;

    vlog_format_t *old = log_config.m_destformat[id];
    vlog_format_t *pnew = vlog_format_compile(fmt_str, set_prefix);
    if (pnew == NULL)
        return 1;

    log_config.m_destformat[id] = pnew;
    if (old != NULL)
        vlog_format_free(old);

    return 0;
}

int vlog_output_set_max_entries(int id, int value)
{
    if (id == VLOG_LOGFILE_INDEX)
        log_config.m_vlogfile.maxentries = value;
    else if (id == VLOG_ERRORFILE_INDEX)
        log_config.m_yerrorfile.maxentries = value;
    else
        vapi_warning("error: can only set max_entries value for file output");

    return 0;
}

int vlog_output_set_max_files(int id, int value)
{
    if (id == VLOG_LOGFILE_INDEX)
        log_config.m_vlogfile.nb_logfiles_max = value;
    else if (id == VLOG_ERRORFILE_INDEX)
        log_config.m_yerrorfile.nb_logfiles_max = value;
    else
        vapi_warning("error: can only set max_files value for file output");

    return 0;
}

int vlog_output_cleanup_files(int id)
{
    if (id == VLOG_LOGFILE_INDEX)
        vlog_file_cleanup(&(log_config.m_vlogfile));
    else if (id == VLOG_ERRORFILE_INDEX)
        vlog_file_cleanup(&(log_config.m_yerrorfile));
    else
        vapi_warning("error: wrong file index given for file cleanup");

    const char* tnd_server = "tndd";
    if (strncmp(vloop_get_application_name(), tnd_server, strlen(tnd_server)) == 0)
        vtnd_log_server_cleanup(id);

    return 0;
}

int vlog_module_set_format(vlog_id_t id, const char *fmt_str)
{
    int ret = 0;
    vlog_module_t *mod = NULL;
    vlog_format_t *old;
    vlog_format_t *pnew;

    ret = get_module_by_id(id, &mod);
    if (ret != 0) {
        vapi_warning("Unknown log id %lu", id);
        return 1;
    }

    old = mod->m_format;
    pnew = vlog_format_compile(fmt_str, 0);
    if (pnew == NULL)
        return 1;

    mod->m_format = pnew;
    if (old != NULL)
        vlog_format_free(old);

    return 0;
}


int vlog_get_default_loglevel(void)
{
    if (!_log_is_initialized())
        return VLOG_DISABLED;
    else
        return log_config.m_default_loglevel;
}


int vlog_early_filter(vlog_id_t module, vlog_level_t level)
{
    // filter out non-error levels if the level is not activated for this module
    if (!vlog_is_error_level(level) && vlog_module_level_enabled(module, level))
        return 1;

    //vlog_tags_set_TAG_MODULE(module);
    //vlog_tags_set_TAG_LEVEL(level);

    return 0;
}

void vlog_set_default_tags(const char *file, int line, const char *function)
{
    //char *filename = strrchr(file, '/');

    //vlog_tags_set_TAG_FILE_PATH(file);
    //vlog_tags_set_TAG_FILE_LINE(line);
    //vlog_tags_set_TAG_FUNCTION(function);
    //vlog_tags_set_TAG_MODULE_NAME(vlog_module_get_name(vlog_tags.TAG_MODULE));

    //if (filename != NULL)
    //    vlog_tags_set_TAG_FILE_NAME(filename+1);
}

void vlog_set_static_tags(void)
{
    //vlog_tags_set_TAG_APP_NAME(vloop_get_application_name());
    //vlog_tags_set_TAG_APP_VERSION(vloop_get_build_version());
    //vlog_tags_set_TAG_PROCESS_ID(getpid());
    //vlog_tags_set_TAG_THREAD_ID(pthread_self());
    //vlog_tags_set_TAG_THREAD_NAME(vthread_getselfname());
}

int vlog_register_update_cb(vlog_update_cb_t on_update, vlog_id_t log_id, void *user_ctx)
{
    int ret = -1;
    vlog_module_t *mod = NULL;
    vlog_update_ctx_t *update_ctx = (struct vlog_update_ctx *)vmem_calloc(vmem_alloc_default(), sizeof(struct vlog_update_ctx));
    if (update_ctx == NULL) {
        vapi_error("%s: failed memory allocation", __FUNCTION__);
        return -1;
    }

    ret = get_module_by_id(log_id, &mod);
    if (ret != 0) {
        vapi_warning("Unknown log id %lu", log_id);
        vmem_free(vmem_alloc_default(), update_ctx);
        return -1;
    }

    update_ctx->user_cb = on_update;
    update_ctx->user_ctx = user_ctx;
    mod->m_update_ctx = update_ctx;

    return 0;
}

int vlog_register_operator_syslog(const char syslog_module_name[VLOG_MAX_MOD_NAME], vlog_id_t *log_id)
{
    int ret = 0;

    ret = vmutex_lock(&vlog_config_lock);
    if (ret != 0)
        goto exit_register;

    ret = __vlog_module_register(syslog_module_name, log_id, VLOG_OPERATOR);
    vmutex_unlock(&vlog_config_lock);
    if (ret != 0)
        goto exit_register;

    set_module_json_config(syslog_module_name, log_id);

exit_register:
    return ret;
}

int vlog_get_syslog_config(vlog_module_config_t *cfg, int *nbr)
{
    int tmplevel = -2;
    vlog_module_t *tmpmod = NULL;
    struct vlist *nodep = NULL;
    const char *format;
    char name[VLOG_MAX_MOD_NAME] = {0};

    *nbr = 0;
    vlog_output_get_data(VLOG_SYSLOG_INDEX, name, &tmplevel, &format);

    vlist_foreach(&(log_config.m_module_list),nodep) {
        tmpmod = container_of(vlog_module_t, m_node, nodep);
        strcpy(cfg[*nbr].mod_name, tmpmod->m_name);
        cfg[*nbr].mod_level = MIN(tmplevel, tmpmod->m_level);
        cfg[*nbr].mod_category = tmpmod->m_category;
        (*nbr)++;
    }

    return 0;
}

int vlog_set_syslog_config(vlog_module_config_t *cfg, int nbr)
{
    int i;
    int ret = 0;
    vlog_level_t level = VLOG_DISABLED;

    for (i=0; i<nbr; i++)
    {
        vlog_id_t mod_id = vlog_module_get_logid(cfg[i].mod_name);
        ret = vlog_module_config_loglevel(mod_id, cfg[i].mod_level);
        if (ret < 0)
            vapi_error("warning: could not set syslog module level for %s", cfg[i].mod_name);

        if (cfg[i].mod_level > level)
            level = cfg[i].mod_level;
    }

    if (level > log_config.m_destlevel[VLOG_SYSLOG_INDEX]) {
        ret = vlog_output_set_loglevel(VLOG_SYSLOG_INDEX, level);
        if (ret < 0) {
            vapi_error("warning: could not set syslog output level %d", level);
            return -1;
        }
    }

    return 0;
}

int vlog_reset_syslog_config(vlog_category_t syslog_category)
{
    vlog_set_default_threshold(VLOG_SYSLOG_INDEX, vlog_get_default_loglevel(), syslog_category);

    return 0;
}

int vlog_print_error_enabled(void)
{
    return (log_config.m_errorfile > 0);
}
