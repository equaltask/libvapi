#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <libvapi/vdbg.h>
#include <libvapi/vlog.h>
#include <libvapi/vtnd.h>

#include "vlog_core.h"
#include "vlog_dbg.h"
//#include "vlog_tags_filters_dbg.h"
//#include "vlog_tags_dbg.h"

#define STR_LINE    "--------------------------------------------------"

static void log_dbg_help(void *ctx)
{
    vdbg_printf("Log debug Help:\n");
    vdbg_printf("---------------\n");
    vdbg_printf("\n");
    vdbg_printf("Available log levels with numeric code:\n");
    vdbg_printf("VLOG_CRITICAL  [2]\n");
    vdbg_printf("VLOG_ERROR     [3]\n");
    vdbg_printf("VLOG_WARNING   [4]\n");
    vdbg_printf("VLOG_INFO      [6]\n");
    vdbg_printf("VLOG_DEBUG     [7]\n");
    vdbg_printf("\n");
    vdbg_printf("Available outputs with numeric code:\n");
    vdbg_printf("LOGFILE        [0]\n");
    vdbg_printf("SYSLOG         [1]\n");
    vdbg_printf("CONSOLE        [2]\n");
    vdbg_printf("TNDD           [3]\n");
    vdbg_printf("OPENTRACING    [5]\n");
    vdbg_printf("\n");
    vdbg_printf("Available vapi components with numeric code:\n");
    vdbg_printf("YIPC           [0]\n");
    vdbg_printf("YPROTO         [1]\n");
    vdbg_printf("YTND           [2]\n");
    vdbg_printf("YMUTEX         [3]\n");
    vdbg_printf("YTIMER         [4]\n");
    vdbg_printf("YLOOP          [5]\n");
    vdbg_printf("\n");
    vdbg_printf("Available commands:\n");
    vdbg_printf("* get          getting log parameters\n");
    vdbg_printf("* set          setting log parameters\n");
    vdbg_printf("* filter       setting log tag filters\n");
    vdbg_printf("* tag          list existing tags\n");
    vdbg_printf("* cleanup      cleanup log files\n");
    vdbg_printf("...\n");
}

static void get_help(void *ctx)
{
    vdbg_printf("Log debug Help: get\n");
    vdbg_printf("-------------------\n");
    vdbg_printf("\n");
    vdbg_printf("Usage: log get <params>\n");
    vdbg_printf("\n");
    vdbg_printf("Parameters:\n");
    vdbg_printf("* list                   prints registered log modules, outputs and vapi components\n");
    vdbg_printf("* module loglevel ID     prints the log details of module ID\n");
    vdbg_printf("* output loglevel ID     prints the log details of output ID\n");
    vdbg_printf("* output max_files ID    prints the max nbr of files for file output ID\n");
    vdbg_printf("* output max_entries ID  prints the max nbr of entries for file output ID\n");
    vdbg_printf("* opentracing ID         prints the log details of vapi component ID\n");
}

static void set_help(void *ctx)
{
    vdbg_printf("Log debug Help: set\n");
    vdbg_printf("-------------------\n");
    vdbg_printf("\n");
    vdbg_printf("Usage: log set <params>\n");
    vdbg_printf("\n");
    vdbg_printf("Parameters:\n");
    vdbg_printf("* module loglevel ID VAL                     sets the loglevel to VAL for module ID\n");
    vdbg_printf("* output loglevel ID VAL                     sets the loglevel to VAL for output ID\n");
    vdbg_printf("* module format ID FORMAT                    sets the log format to FORMAT for module ID\n");
    vdbg_printf("* output format ID FORMAT                    sets the log format to FORMAT for output ID\n");
    vdbg_printf("* output max_files ID VAL                    sets the max nbr of files to VAL for file output ID\n");
    vdbg_printf("* output max_entries ID VAL                  sets the max nbr of entries to VAL for file output ID\n");
    vdbg_printf("* opentracing ID VLOG_ENABLED|VLOG_DISABLED  enable/disable opentracing logs for vapi component ID\n");
    vdbg_printf("\n");
    vdbg_printf("Format examples:\n");
    vdbg_printf("* [%%TAG_FILE_NAME:%%TAG_FILE_LINE] %%MSG\n");
    vdbg_printf("* @%%TIME %%MSG\n");
    vdbg_printf("* VLAN %%08TAG_VLAN - %%MSG\n");
}

static void log_cleanup_help(void *ctx)
{
    vdbg_printf("Log debug Help: cleanup\n");
    vdbg_printf("------------------------\n");
    vdbg_printf("\n");
    vdbg_printf("Usage: cleanup <params>\n");
    vdbg_printf("\n");
    vdbg_printf("Parameters:\n");
    vdbg_printf("* output ID                            clean up the files for file output ID\n");
}

static void dump_maps_help(void *ctx)
{
    vdbg_printf("Log debug Help: dump_maps\n" \
                "------------------------\n" \
                "\n"                         \
                "Usage: log dump_maps\n");
}

/*****************************************************************************/

static vlog_level_t get_loglevel_from_string(char *level)
{
    int ret = 0;
    int i = 0;
    int count = 0;
    int len = strlen(level);
    vlog_level_t lvl = VLOG_DISABLED;

    for (i = 0; i < len; ++i) {
        ret = isdigit(level[i]);
        if (ret > 0)
            count++;
    }

    if (count == len)
        /* level descriptor fully numeric */
        lvl = atoi(level);
    else
        lvl = vlog_level_from_str(level);

    return lvl;
}

static vlog_id_t get_id_from_string(char *module)
{
    int ret = 0;
    int i = 0;
    int count = 0;
    int len = strlen(module);
    vlog_id_t id = -1;

    for (i = 0; i < len; ++i) {
        ret = isdigit(module[i]);
        if (ret > 0)
            count++;
    }

    if (count == len)
        /* module descriptor fully numeric */
        id = atoi(module);
    else
        id = vlog_module_get_logid(module);

    return id;
}

static vlog_output_t get_output_from_string(char *output)
{
    int ret = 0;
    int i = 0;
    int count = 0;
    int len = strlen(output);
    vlog_id_t id = -1;

    for (i = 0; i < len; ++i) {
        ret = isdigit(output[i]);
        if (ret > 0)
            count++;
    }

    if (count == len) {
        /* output descriptor fully numeric */
        id = atoi(output);
        if ( id >= VLOG_DEST_MAX ) id = -1;
    }
    else {
        id = vlog_output_to_id(output);
    }

    return id;
}

static vlog_vapi_component_t get_component_from_string(char *component)
{
    int ret = 0;
    int i = 0;
    int count = 0;
    int len = strlen(component);
    vlog_vapi_component_t id = 0;

    for (i = 0; i < len; ++i) {
        ret = isdigit(component[i]);
        if (ret > 0)
            count++;
    }

    if (count == len)
        /* output descriptor fully numeric */
        id = atoi(component);
    else
        id = vlog_component_to_id(component);

    return id;
}

static vlog_status_t get_status_from_string(char *status)
{
    int ret = 0;
    int i = 0;
    int count = 0;
    int len = strlen(status);
    vlog_status_t vlog_status = VLOG_STATUS_DISABLED;

    for (i = 0; i < len; ++i) {
        ret = isdigit(status[i]);
        if (ret > 0)
            count++;
    }

    if (count == len)
        /* level descriptor fully numeric */
        vlog_status = atoi(status);
    else
        vlog_status = vlog_status_from_str(status);

    return vlog_status;
}

static int set_cmd_log_module_format_detail(char *cmd, char *args)
{
    int ret = 0;
    char param1[VDBG_MAX_CMD_LEN] = "";
    char param2[VDBG_MAX_CMD_LEN] = "";
    char module[VDBG_MAX_CMD_LEN] = "";
    char *fmt_str;
    vlog_id_t id = 0;

    vdbg_scan_args(args, "%s %s %s", param1, param2, module);

    id = get_id_from_string(module);

    if ((signed)id == -1) {
        vdbg_printf("Error: bad module \"%s\" specified\n", module);
        vdbg_printf("Usage: log set module format MODULE FORMAT\n");
        return -1;
    }

    if (strlen(args) <= strlen(param1) + strlen(param2) + strlen(module) + 2 ||
        args[strlen(param1) + strlen(param2) + strlen(module) + 2] != ' ') {
        vdbg_printf("Error: bad command usage\n");
        vdbg_printf("Usage: log set module format MODULE FORMAT\n");
        return -1;
    }

    fmt_str = args + strlen(param1) + strlen(param2) + strlen(module) + 3;

    ret = vlog_module_set_format(id, fmt_str);
    if (ret != 0)
        vdbg_printf("Error: could not set format for %s\n", module);
    else
        vdbg_printf("Format \"%s\" set successfully for %s\n", fmt_str, module);

    return ret;
}

static int set_cmd_log_output_format_detail(char *cmd, char *args)
{
    int ret = 0;
    char param1[VDBG_MAX_CMD_LEN] = "";
    char param2[VDBG_MAX_CMD_LEN] = "";
    char output[VDBG_MAX_CMD_LEN] = "";
    char *fmt_str;
    int id = 0;

    vdbg_scan_args(args, "%s %s %s", param1, param2, output);

    id = get_output_from_string(output);

    if (id == -1) {
        vdbg_printf("Error: bad output \"%s\" specified\n", output);
        vdbg_printf("Usage: log set output format OUTPUT FORMAT\n");
        return -1;
    }

    if (strlen(args) <= strlen(param1) + strlen(param2) + strlen(output) + 2 ||
        args[strlen(param1) + strlen(param2) + strlen(output) + 2] != ' ') {
        vdbg_printf("Error: bad command usage\n");
        vdbg_printf("Usage: log set output format OUTPUT FORMAT\n");
        return -1;
    }

    fmt_str = args + strlen(param1) + strlen(param2) + strlen(output) + 3;

    ret = vlog_output_set_format(id, fmt_str);
    if (ret != 0) {
        vdbg_printf("Error: could not set format for %s\n", output);
    } else {
        vdbg_printf("Format \"%s\" set successfully for %s\n", fmt_str, output);
    }

    return ret;
}

/* set log module param */
static int set_cmd_log_module_detail(char *cmd, char *args)
{
    int ret = 0;
    char param1[VDBG_MAX_CMD_LEN] = "";
    char param2[VDBG_MAX_CMD_LEN] = "";
    char module[VDBG_MAX_CMD_LEN] = "";
    char level[VDBG_MAX_CMD_LEN] = "";
    vlog_id_t id = 0;
    vlog_level_t lvl = 0;

    vdbg_scan_args(args, "%s %s", param1, param2);
    /* param1 equals "module" here */

    if (strncmp("loglevel", param2, VDBG_MAX_CMD_LEN) == 0) {
        vdbg_scan_args(args, "%s %s %s %s", param1, param2, module, level);

        id = get_id_from_string(module);
        lvl = get_loglevel_from_string(level);

        if (strlen(module) == 0 || (signed)id == -1) {
            vdbg_printf("error: missing or unknown module [%s]\n", module);
            vdbg_printf("Usage: log set module loglevel ID VAL                     sets the loglevel to VAL for module ID\n");
            ret = -1;
        } else
        if (strlen(level) == 0 || lvl == 0) {
            vdbg_printf("error: missing or unknown level [%s]\n", level);
            vdbg_printf("Usage: log set module loglevel ID VAL                     sets the loglevel to VAL for module ID\n");
            ret = -1;
        } else {
            ret = vlog_module_set_loglevel(id, lvl);
            if (ret != 0)
                vdbg_printf("error: could not set loglevel for [%d]\n", (int)id);
        }
    } else
    if (strncmp("format", param2, VDBG_MAX_CMD_LEN) == 0) {
        ret = set_cmd_log_module_format_detail(cmd, args);
    } else {
        if (strlen(param2) == 0) {
            vdbg_printf("error: missing parameter\n");
        } else {
            vdbg_printf("error: unkown parameter [%s]\n", param2);
        }
        vdbg_printf("Usage: log set module loglevel ID VAL                     sets the loglevel to VAL for module ID\n");

        ret = -1;
    }

    return ret;
}

static int set_cmd_log_output_detail(char *cmd, char *args)
{
    int ret = 0;
    char param1[VDBG_MAX_CMD_LEN] = "";
    char param2[VDBG_MAX_CMD_LEN] = "";
    char output[VDBG_MAX_CMD_LEN] = "";
    char level[VDBG_MAX_CMD_LEN] = "";
    int id = 0;
    vlog_level_t lvl = 0;

    vdbg_scan_args(args, "%s %s", param1, param2);
    /* param1 equals "module" here */

    if (strncmp("loglevel", param2, VDBG_MAX_CMD_LEN) == 0) {
        vdbg_scan_args(args, "%s %s %s %s", param1, param2, output, level);

        id = get_output_from_string(output);
        lvl = get_loglevel_from_string(level);

        if (strlen(output) == 0 || id == -1) {
            vdbg_printf("error: missing or unknown output [%s]\n", output);
            vdbg_printf("Usage: log set output loglevel ID VAL                     sets the loglevel to VAL for output ID\n");
            ret = -1;
        } else
        if (strlen(level) == 0 || lvl == 0) {
            vdbg_printf("error: missing or unknown level [%s]\n", level);
            vdbg_printf("Usage: log set output loglevel ID VAL                     sets the loglevel to VAL for output ID\n");
            ret = -1;
        } else {
            ret = vlog_output_set_loglevel(id, lvl);
            if (ret != 0)
                vdbg_printf("error: could not set loglevel for [%d]\n", id);
        }
    } else if (strncmp("format", param2, VDBG_MAX_CMD_LEN) == 0) {
        ret = set_cmd_log_output_format_detail(cmd, args);
    } else if (strncmp("max_entries", param2, VDBG_MAX_CMD_LEN) == 0) {
        uint32_t max_nbr = MAX_ENTRIES_PER_FILE;
        vdbg_scan_args(args, "%s %s %s %u", param1, param2, output, &max_nbr);
        id = get_output_from_string(output);

        if (strlen(output) == 0 || id == -1 || max_nbr == 0) {
            vdbg_printf("error: missing/wrong output or value\n");
            vdbg_printf("Usage: log set output max_entries ID VAL                     sets the max_entries to VAL for output ID\n");
            ret = -1;
        } else {
            vlog_output_set_max_entries(id, max_nbr);
        }
    } else if (strncmp("max_files", param2, VDBG_MAX_CMD_LEN) == 0) {
        uint32_t max_nbr = MAX_NBR_FILES;
        vdbg_scan_args(args, "%s %s %s %u", param1, param2, output, &max_nbr);
        id = get_output_from_string(output);

        if (strlen(output) == 0 || id == -1 || max_nbr == 0) {
            vdbg_printf("error: missing/wrong output or value\n");
            vdbg_printf("Usage: log set output max_files ID VAL                     sets the max_files to VAL for output ID\n");
            ret = -1;
        } else {
            vlog_output_set_max_files(id, max_nbr);
        }
    } else {
        if (strlen(param2) == 0) {
            vdbg_printf("error: missing parameter\n");
        } else {
            vdbg_printf("error: unkown parameter [%s]\n", param2);
        }
        vdbg_printf("Usage: log set output loglevel ID VAL                     sets the loglevel to VAL for output ID\n");

        ret = -1;
    }

    return ret;
}

static int set_cmd_log_status_detail(char *cmd, char *args)
{
    int ret = 0;
    char param1[VDBG_MAX_CMD_LEN] = "";
    char component[VDBG_MAX_CMD_LEN] = "";
    char status[VDBG_MAX_CMD_LEN] = "";
    vlog_vapi_component_t component_id = 0;
    vlog_status_t component_status = 0;

    vdbg_scan_args(args, "%s %s %s", param1, component, status);

    if (strncmp("opentracing", param1, VDBG_MAX_CMD_LEN) == 0) {
        component_id = get_component_from_string(component);
        component_status = get_status_from_string(status);

        if (strlen(component) == 0) {
            vdbg_printf("error: missing vapi component\n");
            vdbg_printf("Usage: log set opentracing ID VLOG_ENABLED|VLOG_DISABLED  enable/disable opentracing logs for vapi component ID\n");
            ret = -1;
        } else
        if ((signed)component_id == -1) {
            vdbg_printf("error: unknown vapi component [%s]\n", component);
            vdbg_printf("Usage: log set opentracing ID VLOG_ENABLED|VLOG_DISABLED  enable/disable opentracing logs for vapi component ID\n");
            ret = -1;
        } else
        if (strlen(status) == 0) {
            vdbg_printf("error: missing status\n");
            vdbg_printf("Usage: log set opentracing ID VLOG_ENABLED|VLOG_DISABLED  enable/disable opentracing logs for vapi component ID\n");
            ret = -1;
        } else
        if ((signed)component_status == -1) {
            vdbg_printf("error: unknown status [%s]\n", status);
            vdbg_printf("Usage: log set opentracing ID VLOG_ENABLED|VLOG_DISABLED  enable/disable opentracing logs for vapi component ID\n");
            ret = -1;
        } else {
            ret = vlog_output_set_opentracing_status(component_id, component_status);
            if (ret != 0)
                vdbg_printf("error: could not set status level for [%d]\n", component_id);
        }
    } else {
        vdbg_printf("error: unkown parameter [%s]\n", param1);
        vdbg_printf("Usage: log set opentracing ID VLOG_ENABLED|VLOG_DISABLED  enable/disable opentracing logs for vapi component ID\n");
        ret = -1;
    }

    return ret;
}

/* main callback for setters */
static int set_cmd(char *cmd, char *args, void *ctx)
{
    char param1[VDBG_MAX_CMD_LEN] = "";

    vdbg_scan_args(args, "%s", param1);

    if (strlen(param1) == 0) {
        vdbg_printf("error: missing parameter\n");
        return 0;
    }

    if (strncmp("module", param1, VDBG_MAX_CMD_LEN) == 0) {
        return set_cmd_log_module_detail(cmd, args);
    } else if (strncmp("output", param1, VDBG_MAX_CMD_LEN) == 0) {
        return set_cmd_log_output_detail(cmd, args);
    } else if (strncmp("opentracing", param1, VDBG_MAX_CMD_LEN) == 0) {
        return set_cmd_log_status_detail(cmd, args);
    } else {
        vdbg_printf("error: unkown parameter [%s]\n", param1);
        vdbg_printf("Usage: log set help\n");
        return -1;
    }

    return 0;
}


/* simple dump of log all module names */
static int get_cmd_log_list(void)
{
    int ret = 0;
    int i = 0;
    int nr = 0;
    int level = -2;
    vlog_status_t status = 0;
    char name[VLOG_MAX_MOD_NAME] = { 0 };
    char header[128] = { 0 };
    const char *format;

    nr = vlog_get_nbr_modules();

    sprintf(header,"ID | %-16s | %-16s | %s","module name","level [VALUE]","format");
    vdbg_printf("%s\n",header);
    vdbg_printf("%.*s\n",(int)strlen(header), STR_LINE);

    for (i = 0; i <= nr; i++) {
        ret = vlog_module_get_data(i, name, &level, &format);
        if (ret != 0)
            continue;
        vdbg_printf("%.2d | %-16s | %-16s | %s\n",i, name, vlog_level_to_str(level), format);
    } /* end for */


    vdbg_printf("\n");
    sprintf(header,"ID | %-16s | %-16s | %s","output name","level [VALUE]","format");
    vdbg_printf("%s\n",header);
    vdbg_printf("%.*s\n",(int)strlen(header), STR_LINE);

    for (i = 0; i < VLOG_DEST_MAX; i++) {
        vlog_output_get_data(i, name, &level, &format);
        vdbg_printf("%.2d | %-16s | %-16s | %s\n",i, name, vlog_level_to_str(level), format);
    }


    vdbg_printf("\n");
    sprintf(header, "ID | %-16s | %-16s","vapi module name", "log status");
    vdbg_printf("%s\n", header);
    vdbg_printf("%.*s\n", (int)strlen(header), STR_LINE);

    for (i = 0; i < COMPONENT_DEST_MAX; i++) {
        vlog_status_get_data(i, name, &status);
        vdbg_printf("%.2d | %-16s | %-16s\n", i, name, vlog_status_to_str(status));
    }

    return 0;
}

/* simple dump of log module params */
static int get_cmd_log_module_detail(char *cmd, char *args, void *ctx)
{
    int ret = 0;
    int id = 0;
    int level = 0;
    char module[VDBG_MAX_CMD_LEN] = "";
    char param1[VDBG_MAX_CMD_LEN] = "";
    char param2[VDBG_MAX_CMD_LEN] = "";
    char name[VLOG_MAX_MOD_NAME] = "";
    char header[128] = "";
    const char *format;

    vdbg_scan_args(args, "%s %s %s", param1, param2, module);

    if (strlen(param2) == 0) {
        vdbg_printf("error: missing parameter\n");
        vdbg_printf("Usage: log get module loglevel ID   prints the log details of module ID\n");
        return 0;
    }

    if (strncmp("loglevel", param2, VDBG_MAX_CMD_LEN) != 0) {
        vdbg_printf("error: unknown parameter [%s]\n", param2);
        vdbg_printf("Usage: log get module loglevel ID   prints the log details of module ID\n");
        return 0;
    }

    id = get_id_from_string(module);
    if (strlen(module) == 0 || id == -1) {
        vdbg_printf("error: missing or unknown module [%s]\n", module);
        vdbg_printf("Usage: log get module loglevel ID   prints the log details of module ID\n");
        return 0;
    }

    ret = vlog_module_get_data(id, name, &level, &format);
    if (ret != 0) {
        vdbg_printf("error: log id [%d] not found\n", (int)id);
        return 0;
    }

    sprintf(header, "ID | %-32s | %-16s | %s\n", "module name", "level [VALUE]", "format");
    vdbg_printf("%s", header);
    vdbg_printf("%.*s\n", (int)strlen(header) - 1, STR_LINE);
    vdbg_printf("%.2d | %-32s | %-16s | %s\n", id, name, vlog_level_to_str(level), format);

    return 0;
}

/* simple dump of log output params */
static int get_cmd_log_output_detail(char *cmd, char *args, void *ctx)
{
    int ret = 0;
    int id = 0;
    int level = 0;
    char output[VDBG_MAX_CMD_LEN] = "";
    char param1[VDBG_MAX_CMD_LEN] = "";
    char param2[VDBG_MAX_CMD_LEN] = "";
    char name[VLOG_MAX_MOD_NAME] = "";
    char header[128] = "";
    const char *format;

    vdbg_scan_args(args, "%s %s %s", param1, param2, output);

    if (strlen(param2) == 0) {
        vdbg_printf("error: missing parameter\n");
        vdbg_printf("Usage: log get output loglevel ID   prints the log details of output ID\n");
        return 0;
    }

    id = get_output_from_string(output);
    if (strlen(output) == 0 || id == -1) {
        vdbg_printf("error: missing or unknown output [%s]\n", output);
        vdbg_printf("Usage: log get output <param> ID   prints the log details of output ID\n");
        return 0;
    }

    if (strncmp("loglevel", param2, VDBG_MAX_CMD_LEN) == 0) {
        ret = vlog_output_get_data(id, name, &level, &format);
        if (ret != 0) {
            vdbg_printf("error: log id [%d] not found\n", (int)id);
            return 0;
        }
        sprintf(header, "ID | %-32s | %-16s | %s\n", "output name", "level [VALUE]", "format");
        vdbg_printf("%s", header);
        vdbg_printf("%.*s\n", (int)strlen(header) - 1, STR_LINE);
        vdbg_printf("%.2d | %-32s | %-16s | %s\n", id, name, vlog_level_to_str(level), format);

    } else if (strncmp("max_entries", param2, VDBG_MAX_CMD_LEN) == 0) {
        sprintf(header, "ID | %-32s | %-16s\n", "output name", "max_entries [VALUE]");
        vdbg_printf("%s", header);
        vdbg_printf("%.*s\n", (int)strlen(header) - 1, STR_LINE);
        vdbg_printf("%.2d | %-32s | %d\n", id, name, vlog_output_get_max_entries(id));

    } else if (strncmp("max_files", param2, VDBG_MAX_CMD_LEN) == 0) {
        sprintf(header, "ID | %-32s | %-16s\n", "output name", "max_files [VALUE]");
        vdbg_printf("%s", header);
        vdbg_printf("%.*s\n", (int)strlen(header) - 1, STR_LINE);
        vdbg_printf("%.2d | %-32s | %d\n", id, name, vlog_output_get_max_files(id));

    } else {
        vdbg_printf("error: unknown parameter [%s]\n", param2);
        vdbg_printf("Usage: log get output <param> ID   prints the log details of output ID\n");
        return 0;
    }

    return 0;
}

static int get_cmd_log_status_detail(char *cmd, char *args, void *ctx)
{
    int ret = 0;
    char param1[VDBG_MAX_CMD_LEN] = "";
    char component[VDBG_MAX_CMD_LEN] = "";
    vlog_vapi_component_t component_id = 0;
    vlog_status_t component_status = 0;
    char header[128] = "";

    vdbg_scan_args(args, "%s %s", param1, component);

    if (strlen(component) == 0) {
        vdbg_printf("error: missing vapi component\n");
        vdbg_printf("Usage: log get opentracing ID       prints the log details of vapi component ID\n");
        return 0;
    }

    component_id = get_component_from_string(component);
    if ((signed)component_id == -1) {
        vdbg_printf("error: unknown vapi component [%s]\n", component);
        vdbg_printf("Usage: log get opentracing ID       prints the log details of vapi component ID\n");
        return 0;
    }

    ret = vlog_status_get_data(component_id, component, &component_status);
    if (ret != 0) {
        vdbg_printf("error: could not get status level for [%d]\n", component_id);
        vdbg_printf("Usage: log get opentracing ID       prints the log details of vapi component ID\n");
        return 0;
    }

    sprintf(header, "ID | %-32s | %-16s \n", "component name", "status");
    vdbg_printf("%s", header);
    vdbg_printf("%.*s\n", (int)strlen(header), STR_LINE);
    vdbg_printf("%.2d | %-32s | %-16s\n", component_id, component, vlog_status_to_str(component_status));

    return 0;
}

/* main callback for getters */
static int get_cmd(char *cmd, char *args, void *ctx)
{
    char param1[VDBG_MAX_CMD_LEN] = "";

    vdbg_scan_args(args, "%s", param1);

    if (strlen(param1) == 0) {
        vdbg_printf("error: missing parameter\n");
        return 0;
    }

    if (strncmp("list", param1, VDBG_MAX_CMD_LEN) == 0) {
        return get_cmd_log_list();
    } else if (strncmp("module", param1, VDBG_MAX_CMD_LEN) == 0) {
        return get_cmd_log_module_detail(cmd, args, ctx);
    } else if (strncmp("output", param1, VDBG_MAX_CMD_LEN) == 0) {
        return get_cmd_log_output_detail(cmd, args, ctx);
    } else if (strncmp("opentracing", param1, VDBG_MAX_CMD_LEN) == 0) {
        return get_cmd_log_status_detail(cmd, args, ctx);
    } else {
        vdbg_printf("error: unkown parameter [%s]\n", param1);
        vdbg_printf("Usage: log get help");
        return -1;
    }

    return 0;
}

/* Call back to dump maps*/
static int dump_maps_cb(char *cmd, char *args, void *ctx)
{
    char buffer[VLOG_MAX_MAPS_SIZE]= {'\0'};

    if (vlog_get_maps_dump(buffer) > 0) {
        vdbg_printf(buffer);
        return 0;
    } else {
        vdbg_printf("An error occured while fetching maps file. "\
                    "Please check error_records !\n");
        return -1;
    }
}

static int log_cleanup_cmd(char *cmd, char *args, void *ctx)
{
    char param1[VDBG_MAX_CMD_LEN] = "";
    char output[VDBG_MAX_CMD_LEN] = "";
    int id = 0;

    vdbg_scan_args(args, "%s %s", param1, output);

    if (strlen(param1) == 0) {
        vdbg_printf("error: missing parameter\n");
        return 0;
    }

    if (strncmp("output", param1, VDBG_MAX_CMD_LEN) == 0) {
        id = get_output_from_string(output);

        if (strlen(output) == 0 || id == -1) {
            vdbg_printf("error: missing or unknown output [%s]\n", output);
            vdbg_printf("Usage: log cleanup output ID                       cleans up the files for file output ID\n");
            return -1;
        } else {
            vlog_output_cleanup_files(id);
        }
    } else {
        vdbg_printf("error: unkown parameter [%s]\n", param1);
        vdbg_printf("Usage: log cleanup help");
        return -1;
    }

    return 0;
}

/****************************************************************************/
/* initialize tnd module 'log' */
int vlog_dbg_init_module(void)
{
    if (vdbg_is_initialized()) {
        vdbg_link_module("log", log_dbg_help, NULL);
        vdbg_link_cmd("log", "get", get_help, get_cmd, NULL);
        vdbg_link_cmd("log", "set", set_help, set_cmd, NULL);
        //vdbg_link_cmd("log", "filter", vlog_tags_filters_help_cb, vlog_tags_filters_cmd_cb, NULL);
        //vdbg_link_cmd("log", "tag", vlog_tags_help_cb, vlog_tags_cmd_cb, NULL);
        vdbg_link_cmd("log", "cleanup", log_cleanup_help, log_cleanup_cmd, NULL);
        vdbg_link_cmd("log", "dump_maps", dump_maps_help, dump_maps_cb, NULL);
    }

    return 0;
}
