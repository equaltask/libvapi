#ifndef __VSYSTEM_H__
#define __VSYSTEM_H__

#include <libvapi/vevent.h>
#include <sys/types.h>

/*! \file vsystem.h
 *  \brief Interface definition for the system interface of the libvapi platform.
 *
 *  This interface DOES NOT SUPPORT multiple threads and should not be used from
 *  a thread running a secondary event loop.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Maximum number of bytes received via the data callback at once.
 */
#define VSYSTEM_DATA_MAX_LEN    4096

/*!
 * \brief Callback type for process termination.
 *
 * \param reason    IN Termination reason.
 * \param ctx       IN User context provided in vsystem_call().
 * \param status    IN Process exit value.
 */
typedef void (*vsystem_terminate_cb_t)(vevent_reason_t reason, int status, void *ctx);

/*!
 * \brief Callback type for user to add cgroup settings between fork and exec.
 *
 * \param ctx       IN User context provided in vsystem_call().
 * \param pid       IN pid of command.
 */
typedef void (*vsystem_cgroup_cb_t)(void *ctx, pid_t pid);

/*!
 * \brief Callback type for data reception (from STDOUT or STDERR).
 *
 * \param data          IN Data received from the executed command.
 * \param data_length   IN Data length in bytes.
 * \param ctx           IN User context provide in vsystem_call().
 */
typedef void (*vsystem_data_cb_t)(char *data, int data_length, void *ctx);

/*!
 * \brief Initialize the vsystem api.
 *
 * Should only be called by the API during yloop initialization.
 * \return 0 on success, a negative value on error.
 */
int vsystem_init(void);

/*!
 * \brief Execute a command.
 *
 * This function will execute the command (like system()) and
 * register a callback which will either get called upon completion
 * or after timeout has expired.
 *
 * This will return immediately after command execution.
 *
 * This will NOT check if the command you are trying to execute is valid: it will execute it. It is
 * your responsibility to check the status code given to your callback to know what happened (see man execve for
 * error codes).
 *
 * \param command       IN Command to be executed.
 * \param on_terminate  IN Post-execution callback.
 * \param on_data       IN Data reception callback (from STDOUT or STDERR).
 * \param ctx           IN Callback context.
 * \return A vevent handle on success, NULL on failure
 */
vevent_t *vsystem_exec(char **command, vsystem_terminate_cb_t on_terminate, vsystem_data_cb_t on_data, void *ctx);
vevent_t *vsystem_exec1(char **command, vsystem_terminate_cb_t on_terminate, vsystem_data_cb_t on_data, void *ctx, pid_t *child_pid);

/*!
 * \brief Execute a command with evironments just like vsystem_exec.
 *
 * \param command         IN Command to be executed.
 * \param work_dir        IN working directory of target command.
 * \param env             IN environments for target command.
 * \param on_terminate    IN Post-execution callback.
 * \param cgroup_cb       IN callback to do cgroup setings
 * \param on_data_stdout  IN Data reception callback (from STDOUT).
 * \param on_data_stderr  IN Data reception callback (from STDERR).
 * \param redirect_stdout IN Data reception file descriptor (from STDOUT to fd).
 * \param redirect_stderr IN Data reception file descriptor (from STDERR to fd).
 * \param ctx             IN Callback context.
 * \param pid_out         OUT pid of target command.
 * \return A vevent handle on success, NULL on failure
 */
vevent_t *vsystem_exec_extend(char **command, char **env, char *work_dir,
                              vsystem_terminate_cb_t on_terminate,
                              vsystem_cgroup_cb_t cgroup_cb,
                              vsystem_data_cb_t on_data_stdout, int redirect_stdout,
                              vsystem_data_cb_t on_data_stderr, int redirect_stderr,
                              void *ctx, pid_t *pid_out);

/*!
 * \brief Exec into another program
 *
 * This function is equivalent to the execvp system call.
 *
 * \param file Path of the program or script to execute.
 * \param argv Argument list for the new program.
 * \return -1 in case of error, does not return when successful.
 */
int vsystem_execvp(const char *file, char *const argv[]);

/*!
 * \brief terminate child progress with pid.
 *
 * \param pid       IN pid of child progress.
 */
void vsystem_terminate(pid_t pid);

/*!
 * \brief stop all running child processes.
 */
void vsystem_stop_childs(void);

/*!
 * \brief Terminate the application immediately with a given return code.
 * \param status Return code to be given to the supervisor. Supported values are
 *               EXIT_SUCCESS and EXIT_FAILURE. In the first case the supervisor
 *               will just restart the app. In the second case it will perform
 *               escalation and then possibly restart the app (depending on
 *               escalation actions).
 */
void vsystem_exit(int status);

/*!
 * \brief Get process id for a given process name.
 * \param name process name
 * \return -1 in case error or not found, else pid_t
 */
pid_t vsystem_getpid_by_name(const char *name);

/*!
 * \brief Clear the supervisor environment
 *
 * This function is for advanced users only, that can judge whether
 * their application is used in an s6 supervised environment or not.
 * By default you must assume that your application is s6 supervised
 * and that this function must not be called!
 *
 * There are use cases where defaulting the environment can be usefull.
 */
void vsystem_unset_supervisor_env(void);

#ifdef __cplusplus
}
#endif

#endif

