#ifndef _YDBG_H_
#define _YDBG_H_

#include <stdarg.h>
#include <string.h>

#include <libvapi/vtypes.h>
#include <libvapi/vloop.h>

/*! \file vdbg.h
 *  \brief Interface definition for trace and debug.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifndef FEAT_YAPI_HYBRID_BUILD
#define __FORMAT_PRINTF(a, b)   __attribute__((format (printf, a, b)))
#else
#define __FORMAT_PRINTF(a, b)
#endif

/*!
 * \brief
 * Function callback type for help.
 * \sa vdbg_link_module, vdbg_link_cmd
 */
typedef void (*vdbg_help_cb)(void *ctx);
typedef void (*helpCallback_t)();

#define VDBG_MAX_CMD_LEN 512

/*!
 * \brief
 * Function callback type for command. The args string passed to the callback
 * shall not be longer than YDBG_MAX_CMD_LEN.
 * \sa vdbg_link_cmd
 */
typedef int (*vdbg_cmd_cb)(char *cmd, char *args, void *ctx);
typedef int (*commandCallback_t)(char *cmd, char *args);

/*!
 * \brief Create a trace and debug module.
 *
 * The trace and debug infrastructure consists of modules which contain commands.
 * A module is identified by a name, which must be unique in the local context
 * and must not contains space, tab, or newline characters.
 * If a user issues the '<module name> help' command, the mandatory help
 * callback will be executed.
 * Trace and debug commands can be added to a module.
 *
 * \param[in]   name        Name for the module.
 * \param[in]   help_proc   Function callback for module 'help'.
 * \param[in]   ctx         Context pointer that will passed to the help callback.
 * \return  0 on success, error -1 on failure.
 */
int vdbg_link_module(const char *name, vdbg_help_cb help_proc, void *ctx);

/*!
 * \brief Create a trace and debug command.
 *
 * The trace and debug infrastructure consists of modules which contain commands.
 * A command is identified by a name, which must be unique in the module and must not
 * contains space, tab, or newline characters.
 * - When a user issues the '<module name> <command name> help' command, the mandatory
 *   help callback will be executed.
 * - When a user issues a '<module name> <command name> <args>' command, the command
 *   is executed. The arguments can be fetched by the 'vdbg_scan_args' function.
 *
 * \param[in]   module      Name of the parent module.
 * \param[in]   cmd         Name for the command.
 * \param[in]   help_proc   Function callback for command 'help'.
 * \param[in]   cmd_proc    Function callback for command.
 * \param[in]   ctx         Context pointer that will passed to the help callback.
 * \return  0 on success, error -1 on failure.
 * \sa      vdbg_link_module, vdbg_scan_args
 */
int vdbg_link_cmd(const char *module, const char *cmd, vdbg_help_cb help_proc, vdbg_cmd_cb cmd_proc, void *ctx);

/*!
 * \brief Debug printf.
 *
 * Function equal to 'printf' to print to trace and debug output.
 *
 * \param[in]   fmt        Format of print.
 * \param[in]   ...        Arguments.
 * \return  0 on success, error -1 on failure.
 */
int vdbg_printf(const char *fmt, ...) __FORMAT_PRINTF(1, 2);

/*!
 * \brief Debug vprintf.
 *
 * Function equal to 'vprintf' to print to trace and debug output.
 *
 * \param[in]   fmt        Format of print.
 * \param[in]   args       Variable argument list.
 * \return  0 on success, error -1 on failure.
 */
int vdbg_vprintf(const char *fmt, va_list args) __FORMAT_PRINTF(1, 0);

/*!
 * \brief Retrieve arguments from buffer.
 * Function equal to 'sscanf' to get arguments from passed buffer.
 *
 * \param[in]   buf        Buffer string to scan from
 * \param[in]   fmt        Format for scan.
 * \param[in]   ...        Argument pointers of calling function.
 * \return  the number of input items successfully matched and assigned on success, EOF on failure.
 */
int vdbg_scan_args(const char *buf, const char *fmt, ...) __attribute__((format(scanf, 2, 3)));

/*!
 * \brief   Print a trace representing the hexadecimal dump of the given memory area on the trace and debug output
 *
 * \param[in]   mem         Memory area to dump
 * \param[in]   len         Size in byte of the memory area
 *
 * \return the number of byte allocated during the dump, -1 in case of error
 */
int vdbg_hexdump(void *mem, size_t len);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif

