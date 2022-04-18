#ifndef __VLOG_H__
#define __VLOG_H__

#include <libvapi/utility.h>
#include <libvapi/vtypes.h>
//#include <yAPI/generated/vlog_tags.h>
#include <libvapi/vlog_opentracing_wrapper.h>

#include <stdarg.h>
#include <stdlib.h>

/*! \file vlog.h
 *  \brief Logging interface of the libvapi platform.
 *
 *  The logging interface provides a central way for applications to print
 *  traces. These traces can be sent to different outputs (log file, console,
 *  syslog and the central T&D daemon) depending on the configuration.
 *
 *  The central logging macro is called [VLOG_PRINT_CORE]. It is the common point
 *  through which all traces go. It should not be called directly. Different kinds
 *  of macros are created on top of it to offer specific interfaces to suit the
 *  needs of different applications or domains. `VLOG_PRINT_CORE` takes the following
 *  inputs:
 *
 *    * A printf _variant_ which can either be `__vlog_printf` or `__vlog_vprintf`.
 *
 *    * A log id that identifies the log *module* (user of the logging interface).
 *      By default, traces are disabled for all modules, and they have to be
 *      enabled explicitly via T&D.
 *
 *    * A log _level_ specifying the severity of the trace. Above a certain
 *      threshold, it is considered to be an error level, and an error record
 *      is generated in addition to the trace.
 *
 *    * A variable number of _tags_ (zero or more). Tags are metadata associated
 *      to each trace by the tracing application. The primary purpose of tags is
 *      to allow trace filtering. See below about tags.
 *
 *    * A format string _message_ and its parameters, either in the form of
 *      variable arguments or of a va_list, depending on the variant given as
 *      first parameter.
 *
 *  Tags
 *  ----
 *
 *  Tags are primarily meant for trace filtering. For instance, DSL applications
 *  may associate a DSL line id to their traces. Then it is possible for the
 *  operator to filter traces for a given DSL line. A second usecase is to
 *  customize the trace prefix with the value of certain tags.
 *
 *  - Some tags are generic and are added automatically inside `VLOG_PRINT_CORE`.
 *
 *    + `TAG_FILE` and `TAG_LINE` will contain the source file and line number
 *          from where the trace is printed.
 *
 *    + `TAG_MODULE` is the name of the logging module.
 *
 *    + `TAG_LEVEL` is the log level of the trace.
 *
 *  - Some tags are specific to certain domains. It is the goal that each
 *    application associates to its traces tags that are relevant to its domain.
 *    DSL application may tag traces with a DSL line id, GPON applications
 *    with a PON id and NGPON2 applications with a channel pair, a channel
 *    group and a subchannel group. For this purpose, macros can be defined
 *    per application to pass the relevant tags to `VLOG_PRINT_CORE`.
 *
 *  - Tags are passed to the internal functions of vlog as variable arguments.
 *    It is probably a good idea to keep the amount of tags per trace limited.
 *    Traces should be tagged with information relevant for operator filtering,
 *    but not with various useless information.
 *
 *  Filtering
 *  ---------
 *
 *  Here is the complete picture of log filtering.
 *
 *
 *                                                (1)                  (2)
 *                                        +-----------------+     +------------+
 *                            trace       |  early filter   |     | tag filter |
 *                +-------->  module  --->| (module, level) |---->|   (tags)   |
 *                |           level       +-----------------+     +-----+------+
 *                            tags                                      |
 *             message                         +-------------+----------+--+------------+---------------+
 *     IN -->  module                          |             |             |            |               |
 *             level                           v             v             v            v               v
 *             tags                       +----+----+   +----+----+   +----+----   +----+----+   +------+------+
 *                                        | console |   |   T&D   |   | syslog |   |  file   |   | OpenTracing |
 *                |                   (3) | filter  |   | filter  |   | filter |   | filter  |   |   filter    |
 *           +----------+                 | (level) |   | (level) |   | (level)|   | (level) |   |   (level)   |
 *       (4) | is error |                 +---------+   +---------+   +---------   +---------+   +-------------+
 *           | (level)  |                      |             |             |            |               |
 *           +----------+                      v             v             v            v               v
 *                |                         CONSOLE     T&D DAEMON      SYSLOG      LOG FILE    DISTRIBUTED TRACES
 *                |        error record        ^             ^
 *                |        module              |             |
 *                +----->  level   ------------+-------------+----> ERROR RECORD
 *                         tags                                        FILE
 *
 *
 *    1. Early filter: if the level is below the configured threshold for this
 *       module, drop the trace. This is done to avoid processing the tags and
 *       the format string when not necessary.
 *
 *    2. Tag filter: for each tag associated to the trace, if there is a filter
 *       registered for this tag, execute it. If the filter is positive, drop
 *       the trace.
 *
 *    3. Output filter: for each output, if the level is below the configured
 *       threshold for this output, drop the trace.
 *
 *    4. Is error: error records are generated only if the level is above the
 *        configured error threshold.
 *
 * [VLOG_PRINT_CORE]: @ref VLOG_PRINT_CORE "VLOG_PRINT_CORE"
 *
 *  \example vlog_example.c
 *  \example vlog_tags_example.c
 */

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef FEAT_YAPI_HYBRID_BUILD
#define __FORMAT_PRINTF(a, b)         __attribute__((format (printf, a, b)))
#define __FORMAT_PRINTF_OT(a, b, c)   __attribute__((format (printf, b, c)))
#else
#define __FORMAT_PRINTF(a, b)
#define __FORMAT_PRINTF_OT(a, b, c)
#endif

/*** Constant value definitions ***/

//TODO: Use const variables instead of macros
#ifndef VLOG_MAX_MOD_NAME
#define VLOG_MAX_MOD_NAME	32
#endif

#ifndef VLOG_MAX_FILENAME
#define VLOG_MAX_FILENAME	128
#endif

#ifndef VLOG_MAX_SPAN_NAME
#define VLOG_MAX_SPAN_NAME	64
#endif

typedef unsigned long vlog_id_t; /*!< \brief Identifier for a log module (user of the logging interface) */


/*!
 * \brief Log levels, mapped from syslog levels.
 *
 * For error-related levels (warning/error/critical) automatically error records are generated also when
 * calling vlog print API's.
 */
typedef enum {
    VLOG_DISABLED = -1,
    VLOG_CRITICAL = 2,   /*!< Failure that prevents further functioning of the complete application. */
    VLOG_ERROR = 3,      /*!< Failure that results in application malfunction, but can be recovered. */
    VLOG_WARNING = 4,    /*!< Error with insignificant impact (e.g. unexpected value). */
    VLOG_INFO = 6,       /*!< High-level information about the flow of the program, relevant e.g. for a product tester. */
    VLOG_DEBUG = 7,      /*!< Internal information relevant only to the developer (e.g. "entering function F"). */
    VLOG_LEVEL_MAX
} vlog_level_t;


/* indexes for the yApi component */
typedef enum vlog_yapi_component {
    YIPC_INDEX = 0,
    YPROTO_INDEX,
    VTND_INDEX,
    VMUTEX_INDEX,
    VTIMER_INDEX,
    VLOOP_INDEX,
    COMPONENT_DEST_MAX //last element
} vlog_vapi_component_t;


/*!
 * \brief Log status.
 */
typedef enum {
    VLOG_STATUS_DISABLED = 0,
    VLOG_STATUS_ENABLED = 1,
} vlog_status_t;

/*!
 * \brief Log categories, used for syslog configuration.
 */
typedef enum {
    VLOG_INTERNAL,
    VLOG_OPERATOR,
    VLOG_CAT_MAX
} vlog_category_t;


typedef struct {
    char mod_name[VLOG_MAX_MOD_NAME];       /*!< module name */
    vlog_level_t mod_level;                 /*!< log level */
    vlog_category_t mod_category;           /*!< log category */
} vlog_module_config_t;



/*! \cond */

/*** Internal macros/functions not to be used outside this header file ***/
#define VLOG_SET_TAG(t,v) vlog_tags_set_ ## t (v);
#define VLOG_SET_TAG_1(e)
#define VLOG_SET_TAG_3(t,v,e) VLOG_SET_TAG(t,v)
#define VLOG_SET_TAG_5(t1,v1,t2,v2,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_3(t2,v2,e)
#define VLOG_SET_TAG_7(t1,v1,t2,v2,t3,v3,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_5(t2,v2,t3,v3,e)
#define VLOG_SET_TAG_9(t1,v1,t2,v2,t3,v3,t4,v4,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_7(t2,v2,t3,v3,t4,v4,e)
#define VLOG_SET_TAG_11(t1,v1,t2,v2,t3,v3,t4,v4,t5,v5,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_9(t2,v2,t3,v3,t4,v4,t5,v5,e)
#define VLOG_SET_TAG_13(t1,v1,t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_11(t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,e)
#define VLOG_SET_TAG_15(t1,v1,t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_13(t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,e)
#define VLOG_SET_TAG_17(t1,v1,t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,t8,v8,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_15(t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,t8,v8,e)
#define VLOG_SET_TAG_19(t1,v1,t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,t8,v8,t9,v9,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_17(t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,t8,v8,t9,v9,e)
#define VLOG_SET_TAG_21(t1,v1,t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,t8,v8,t9,v9,t10,v10,e) VLOG_SET_TAG(t1,v1) VLOG_SET_TAG_19(t2,v2,t3,v3,t4,v4,t5,v5,t6,v6,t7,v7,t8,v8,t9,v9,t10,v10,e)

#define VLOG_SET_TAGS__(variant, ...) variant(__VA_ARGS__)
#define VLOG_SET_TAGS_(N, ...) VLOG_SET_TAGS__(SYMBOL_CONCAT(VLOG_SET_TAG_, N), __VA_ARGS__)
#define VLOG_SET_TAGS(...) VLOG_SET_TAGS_(__VA_NARG__(__VA_ARGS__), __VA_ARGS__)
void __vlog_printf(const char *fmt, ...) __FORMAT_PRINTF(1, 2);
void __vlog_vprintf(const char *fmt, va_list ap) __FORMAT_PRINTF(1, 0);
void __vlog_printf_ot(const char *span_name, const char *fmt, ...) __FORMAT_PRINTF_OT(1, 2, 3);
void __vlog_vprintf_ot(const char *span_name, const char *fmt, va_list ap) __FORMAT_PRINTF_OT(1, 2, 0);
void __vlog_hexdump(void *addr, size_t size);
void __vlog_print_full(const char *str, size_t len);
int vlog_early_filter(vlog_id_t module, vlog_level_t level);
void vlog_set_default_tags(const char *file, int line, const char *function);

/*! \endcond */

/*!
 * \brief Central logging macro. Not to be used directly.
 *
 * All traces go through here. It has the following roles:
 *   - Early stop: do nothing if the log level is not activated for the log module.
 *   - Save each tag value in the corresponding global variable, and add default tags.
 *   - Call the logging function.
 *
 */

#define VLOG_PRINT_CORE(vlog_printf_variant, module, level, tags, fmtstr, ...) do { \
        if (vlog_early_filter(module, level)) break; \
        vlog_set_default_tags(__FILE__, __LINE__, __func__); \
        VLOG_SET_TAGS(tags); \
        vlog_printf_variant(fmtstr, ##__VA_ARGS__); \
    } while (0);

#define VLOG_PRINT_CORE_FULL(vlog_printf_variant, module, level, tags, addr, size) do { \
        if (vlog_early_filter(module, level)) break; \
        vlog_set_default_tags(__FILE__, __LINE__, __func__); \
        VLOG_SET_TAGS(tags); \
        vlog_printf_variant(addr, size); \
    } while (0);

#define VLOG_PRINT_CORE_OT(vlog_printf_variant, span_name, module, level, tags, fmtstr, ...) do { \
        if (vlog_early_filter(module, level)) break; \
        vlog_set_default_tags(__FILE__, __LINE__, __func__); \
        VLOG_SET_TAGS(tags); \
        vlog_printf_variant(span_name, fmtstr, ##__VA_ARGS__); \
    } while (0);

#define VLOG_PRINT_CORE_FULL_OT(vlog_printf_variant, span_name, module, tags, addr, size) do { \
        if (vlog_early_filter(module, VLOG_DEBUG)) break; \
        vlog_set_default_tags(__FILE__, __LINE__, __func__); \
        VLOG_SET_TAGS(tags); \
        vlog_printf_variant(span_name, addr, size); \
    } while (0);

/*** Public interfaces built on top of VLOG_PRINT_CORE. Define your own on top of them (see examples) ***/

/*!
 * \brief Delimit a list of tags to pass to one of the logging macros.
 *
 * It _must_ receive `TAG_END` as last element.
 * Example: `VLOG_TAGS(TAG_LIB, "yapi", TAG_VLAN, 4, TAG_END)`
 */

#define VLOG_TAGS(first, ...) first, ##__VA_ARGS__

/*!
 * \brief Print a trace with zero or more tags
 * \param module Log id
 * \param level Log level
 * \param tags List of tags delimited by VLOG_TAGS
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vlog_printf(module, level, tags, fmt, ...) \
    VLOG_PRINT_CORE(__vlog_printf, module, level, VLOG_TAGS(tags), fmt, ##__VA_ARGS__)
#define vlog_printf_ot(span_name, module, level, tags, fmt, ...) \
    VLOG_PRINT_CORE_OT(__vlog_printf_ot, span_name, module, level, VLOG_TAGS(tags), fmt, ##__VA_ARGS__)

/*!
 * \brief Print a trace with zero or more tags
 * \param module Log id
 * \param level Log level
 * \param tags List of tags delimited by VLOG_TAGS
 * \param fmt Message
 * \param args va_list of format string arguments
 * TODO check usage
 */

#define vlog_vprintf(module, level, tags, fmt, args) \
    VLOG_PRINT_CORE(__vlog_vprintf, module, level, VLOG_TAGS(tags), fmt, args)
#define vlog_vprintf_ot(span_name, module, level, tags, fmt, args) \
    VLOG_PRINT_CORE_OT(__vlog_vprintf_ot, span_name, module, level, VLOG_TAGS(tags), fmt, args)

/*!
 * \brief Print a hexdump trace with zero or more tags
 * \param module Log id
 * \param tags List of tags delimited by VLOG_TAGS
 * \param addr Pointer to the memory area to dump
 * \param size Number of bytes to dump from addr
 */

#define vlog_hexdump(module, tags, addr, size) \
    VLOG_PRINT_CORE_FULL(__vlog_hexdump, module, VLOG_DEBUG, VLOG_TAGS(tags), addr, size)

/*!
 * \brief Print a trace with zero or more tags
 *        Should be used in limited cases only because of performance impact.
 * \param module Log id
 * \param level Log level
 * \param tags List of tags delimited by VLOG_TAGS
 * \param str Pointer to message to print
 * \param len Message length
 */
#define vlog_print_full(module, level, tags, str, len) \
        VLOG_PRINT_CORE_FULL(__vlog_print_full, module, level, VLOG_TAGS(tags), str, len)


/*!
 * \brief Print a trace without tags
 * \param module Log id
 * \param level Log level
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vlog_notag_printf(module, level, fmt, ...) \
    vlog_printf(module, level, VLOG_TAGS(TAG_END), fmt, ##__VA_ARGS__)
#define vlog_notag_printf_ot(span_name, module, level, fmt, ...) \
    vlog_printf_ot(span_name, module, level, VLOG_TAGS(TAG_END), fmt, ##__VA_ARGS__)

/*!
 * \brief Print a trace without tags
 * \param module Log id
 * \param level Log level
 * \param fmt Message
 * \param args va_list of format string arguments
 */

#define vlog_notag_vprintf(module, level, fmt, args) \
    vlog_vprintf(module, level, VLOG_TAGS(TAG_END), fmt, args)
#define vlog_notag_vprintf_ot(span_name, module, level, fmt, args) \
    vlog_vprintf_ot(span_name, module, level, VLOG_TAGS(TAG_END), fmt, args)

/*!
 * \brief Print current location in source code
 * \param module Log id
 */

#define vlog_print_location(module) \
    vlog_printf(module, VLOG_DEBUG, TAG_END, \
                "Reached %s:%d in %s", __FILE__, __LINE__, __func__)
#define vlog_print_location_ot(span_name, module) \
    vlog_printf_ot(span_name, module, VLOG_DEBUG, TAG_END, \
                "Reached %s:%d in %s", __FILE__, __LINE__, __func__)

/*!
 * \brief Print a trace saying we are entering the current function
 * \param module Log id
 */

#define vlog_enter_function(module) \
    vlog_printf(module, VLOG_DEBUG, TAG_END, \
                "Entering function %s", __func__)
#define vlog_enter_function_ot(span_name, module) \
    vlog_printf_ot(span_name, module, VLOG_DEBUG, TAG_END, \
                "Entering function %s", __func__)

/*!
 * \brief Print a trace saying we are leaving the current function
 * \param module Log id
 */

#define vlog_leave_function(module) \
    vlog_printf(module, VLOG_DEBUG, TAG_END, \
                "Leaving function %s", __func__)
#define vlog_leave_function_ot(span_name, module) \
    vlog_printf_ot(span_name, module, VLOG_DEBUG, TAG_END, \
                "Leaving function %s", __func__)

/*!
 * \brief Print the value of a variable
 * \param module Log id
 * \param variable Variable to be printed
 * \param fmt Format of the variable (e.g. "%d")
 */

#define vlog_print_var(module, variable, fmt) \
    vlog_printf(module, VLOG_DEBUG, VLOG_TAGS(TAG_END), "%s = " fmt, #variable, variable)
#define vlog_print_var_ot(span_name, module, variable, fmt) \
    vlog_printf_ot(span_name, module, VLOG_DEBUG, VLOG_TAGS(TAG_END), "%s = " fmt, #variable, variable)

/*!
 * \brief   Register logging module.
 *
 * This operation registers a new log module. The log id is returned for later access.
 * By default the logging is disabled for the new module. It must be enabled by trace
 * and debug command.
 * Default log level will be VLOG_WARNING.
 *
 * \param   module_name IN  Module name registered to vlog library.
 * \param   log_id      OUT Log id returned to application for further logging requests.
 *
 * \return 0 in case of success, -1 in case of error
 */
int vlog_module_register(const char module_name[VLOG_MAX_MOD_NAME], vlog_id_t *log_id);

/*!
 * \brief   Get the module log_id.
 *
 * This operation retrieves the registerd module log_id.
 *
 * \param   module      IN  The registered name of the module.
 * TODO check usage, can be badly used
 *
 * \return The module log_id.
 */
vlog_id_t vlog_module_get_logid(const char module[VLOG_MAX_MOD_NAME]);

/*!
 * \brief   Returns the name of a log module
 *
 * \param   log_id     IN Log module
 *
 * \return  Pointer to module name.
 */
const char *vlog_module_get_name(vlog_id_t log_id);

/*!
 * \brief   Sets the level for a log module, only allowed to call from TnD context
 *
 * \param   log_id     IN Log module
 * \param   level      IN Log level
 *
 * \return 0 in case of success, -1 in case of error
 */
int vlog_module_set_loglevel(vlog_id_t log_id, int level);

/*!
 * \brief   Returns a string representing the name of the given log level
 *
 * \param   level      IN Log level
 *
 * \return  Pointer to log level name.
 */
const char *vlog_level_to_str(vlog_level_t level);

/*!
 * \brief Callback type for vlog update notification.
 *
 * \param log_id    IN Updated log module
 * \param level     IN New log level
 * \param user_ctx  IN User context provided in vlog_register_update_cb().
 */
typedef void (*vlog_update_cb_t)(vlog_id_t log_id, vlog_level_t level, void *user_ctx);

/*!
 * \brief   Returns a string representing the name of the given log status
 *
 * \param   status      IN Log status
 *
 * \return  Pointer to log status name.
 */
const char *vlog_status_to_str(vlog_status_t status);

/*!
 * \brief Register for logging update notification.
 *
 * This function will register a callback function for notification of logging updates.
 *
 * \param on_update     IN Update callback.
 * \param log_id        IN Log module
 * \param user_ctx      IN Callback context.
 * \return 0 on success, -1 on failure
 */
int vlog_register_update_cb(vlog_update_cb_t on_update, vlog_id_t log_id, void *user_ctx);

int vlog_level_enabled_on_vapi_component(vlog_vapi_component_t component_id);

/*!
 * \brief   Register logging module for operator syslog purpose.
 *
 * This operation registers a new log module. The log id is returned for later access.
 * By default the logging is disabled for the new module. It must be enabled by syslog
 * configuration interface or trace/debug command.
 * Default log level will be VLOG_WARNING.
 *
 * \param   syslog_module_name IN  Module name registered to vlog library.
 * \param   log_id             OUT Log id returned to application for further logging requests.
 *
 * \return 0 in case of success, -1 in case of error
 */
int vlog_register_operator_syslog(const char syslog_module_name[VLOG_MAX_MOD_NAME], vlog_id_t *log_id);

/*!
 * \brief   Returns a list of modules with corresponding syslog configuration.
 *
 * \param   cfg        OUT List of module level configurations
 * \param   nbr        OUT Number of module entries
 *
 * \return  0 in case of success, -1 in case of error
 */
int vlog_get_syslog_config(vlog_module_config_t *cfg, int *nbr);

/*!
 * \brief   Set the syslog configuration for the given modules.
 *
 * \param   cfg        IN List of module level configurations
 * \param   nbr        IN Number of module entries
 *
 * \return  0 in case of success, -1 in case of error
 */
int vlog_set_syslog_config(vlog_module_config_t *cfg, int nbr);

/*!
 * \brief   Reset to the default syslog configuration for all modules.
 *
 * \param   syslog_category     IN Syslog category (operator/internal/...)
 *
 * \return  0 in case of success, -1 in case of error
 */
int vlog_reset_syslog_config(vlog_category_t syslog_category);

#ifdef __cplusplus
}
#endif


#endif
