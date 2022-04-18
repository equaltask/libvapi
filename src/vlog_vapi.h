#ifndef __VLOG_VAPI_H__
#define __VLOG_VAPI_H__

#include <libvapi/vlog.h>

#include <errno.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern vlog_id_t platform_log_id;

int vlog_vapi_init(void);

/*!
 * \brief Print a trace in vapi with zero or more (extra) tags
 * \param level Log level
 * \param tags List of tags delimited by VLOG_TAGS
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vapi_printf(level, tags, fmt, ...) \
    vlog_printf(platform_log_id, level, VLOG_TAGS(tags), fmt, ##__VA_ARGS__)


/*!
 * \brief Print a debug trace in vapi (without (extra) tags)
 *
 * Use for internal information only relevant to the developer (e.g. when entering or leaving a function).
 *
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vapi_debug(fmt, ...) vapi_printf(VLOG_DEBUG, TAG_END, fmt, ##__VA_ARGS__)

/*!
 * \brief Print an info trace in vapi (without (extra) tags)
 *
 * Use for high-level information about the flow of the program relevant to a product tester for instance.
 *
 * This can also be used if an underlying service reports a minor failure
 * (e.g. writing to a socket connection fails because the connection was closed by the other side in the mean-time).
 *
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vapi_info(fmt, ...) vapi_printf(VLOG_INFO, TAG_END, fmt, ##__VA_ARGS__)

/*!
 * \brief Print a warning in vapi (without (extra) tags)
 *
 * Use for a failure with insignificant impact (e.g. unexpected value).
 *
 * This can also be used if an underlying service reports a major failure
 * (e.g. opening a socket connection fails because the other part is not started up yet, or due to a network error).
 *
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vapi_warning(fmt, ...) vapi_printf(VLOG_WARNING, TAG_END, fmt, ##__VA_ARGS__)

/*!
 * \brief Print an error in vapi (without (extra) tags)
 *
 * Use for a failure that results in application malfunctioning, both recoverable as well as non-recoverable
 * (see also vapi_critical).
 *
 * This should be used for programming errors (e.g. illegal parameters)
 * as well as for run-time failures (e.g. memory allocation failure).
 *
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vapi_error(fmt, ...) vapi_printf(VLOG_ERROR, TAG_END, fmt, ##__VA_ARGS__)

/*!
 * \brief Print a critical error in vapi (without (extra) tags)
 *
 * Use for a failure that prevents further functioning of the complete application.
 *
 * However, critical errors from platform level should be avoided,
 * since only the application itself can or should decide on aborting or not.
 *
 * Only use critical errors from platform level if for instance the main event loop
 * (and hence the application as a whole) cannot start up due to an initialization failure.
 *
 * \param fmt Message
 * \param ... Format string arguments
 */

#define vapi_critical(fmt, ...) vapi_printf(VLOG_CRITICAL, TAG_END, fmt, ##__VA_ARGS__)


#define vapi_print_location() vlog_print_location(platform_log_id)
#define vapi_enter_function() vlog_enter_function(platform_log_id)
#define vapi_leave_function() vlog_leave_function(platform_log_id)
#define vapi_print_var(var, fmt) vlog_print_var(platform_log_id, var, fmt)

#define vapi_print_errno(saved_errno) vapi_debug("errno = %d (%s)", saved_errno, strerror(saved_errno))

#ifdef __cplusplus
}
#endif

#endif  /*__VLOG_VAPI_H__*/
