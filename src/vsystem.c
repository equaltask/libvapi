#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <event2/event.h>

#include <libvapi/vmutex.h>
#include <libvapi/vsystem.h>
#include <libvapi/vlog.h>
#include <libvapi/vmem.h>
#include <libvapi/vevent.h>
#include <libvapi/vlist.h>

#include "vsignal.h"
#include "vloop_internal.h"
#include "vlog_vapi.h"

typedef struct vsystem_command {
    vlist_t node;
    int fd[2];
    int fd_stderr[2];
    pid_t pid;
    vsystem_terminate_cb_t on_terminate;
    vsystem_data_cb_t on_data;
    vsystem_data_cb_t on_data_stderr;
    void *ctx;
    struct event *data_event;
    struct event *data_event_stderr;
    vevent_t *hat_event;
    vevent_reason_t event_reason;
    bool extend;
} vsystem_command_t;

static vthread_mutex_t g_cmdlist_lock;
static vlist_t g_commands;
static bool g_initialized = false;

/**
 * Read data from a file descriptor.
 */
static ssize_t _read_data_for_pid(int fd, char *buffer, int buffer_length, int pid)
{
    while (1) {
        ssize_t count = read((int)fd, buffer, buffer_length);
        if (count == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN) {
                break;
            } else {
                vapi_warning("Unexpected error on data reading event for pid %d : '%s'", pid, strerror(errno));
                return -1;
            }
        } else if (count == 0) {
            return 0;
        }

        return count;
    }

    return -1;
}

static void _mark_all_fds_for_cloexec()
{
    int fd;
    int nr_fds = getdtablesize();
    if (nr_fds == -1)
        nr_fds = 2048;

    for (fd = 3; fd < nr_fds; fd++) {
        (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
}

/**
 * Terminate the command.
 *
 * Will call on_terminate (if set), and cleanup the timer and event associated with this command.
 */
static inline void _terminate(vsystem_command_t *cmd, vevent_reason_t reason, int status)
{
    if (cmd->on_data) {
        char buffer[VSYSTEM_DATA_MAX_LEN] = {0};
        if (cmd->data_event) {
            event_free(cmd->data_event);
        }

        /* Check the data pipe one last time. */
        ssize_t count = _read_data_for_pid(cmd->fd[0], buffer, sizeof(buffer) - 1, cmd->pid);
        if (count > 0) {
            cmd->on_data(buffer, count, cmd->ctx);
        }
        close(cmd->fd[0]);
    }

    if (cmd->on_data_stderr) {
        char buffer[VSYSTEM_DATA_MAX_LEN] = {0};
        if (cmd->data_event_stderr) {
            event_free(cmd->data_event_stderr);
        }

        /* Check the data pipe one last time. */
        ssize_t count = _read_data_for_pid(cmd->fd_stderr[0], buffer, sizeof(buffer) - 1, cmd->pid);
        if (count > 0) {
            cmd->on_data_stderr(buffer, count, cmd->ctx);
        }
        close(cmd->fd_stderr[0]);
    }

    /* Set the correct reason for timeout/cancel */
    if (cmd->event_reason == VEVENT_TIMEOUT || cmd->event_reason == VEVENT_CANCEL) {
        reason = cmd->event_reason;
    }

    if (cmd->on_terminate) {
        cmd->on_terminate(reason, status, cmd->ctx);
    }

    /* For timeout/cancel, the hat_event was already deleted in vevent */
    if (cmd->hat_event) {
        vevent_delete(cmd->hat_event);
    }
    vmem_free(vmem_alloc_default(), cmd);
}

/**
 * Callback connected to vsignal.
 *
 * This is called whenever a SIGCHLD signal is received
 *
 *  i.e when a SIGNAL is sent to a process, then it's checked whether the SIGNAL is indicated as to be blocked.
 *  when the signal is indicated at to be blocked, it is stored and a pending bit is set ( in a datastruct having a bit per signal)
 *  when the signal is not indicated to be blocked, then the signal handler is called. When during this time the same signal is received it will be stored
 *  in the pending queue. I.e the system considers the signal to be blocked.
 *  Once a signal bit is set pending, a subsequent signal ( same SIG nr) will be ignored.
 *  During handling of a signal, it's possible to prevent that the signal handler is interrupted to handle other signals, this is via the blocking signal
 *  mask given during the installation of the signal handler.
 *  The order in which the pending signals will be handled si lowest signal number first.
 *  Note that signal delivery in a multithread process is to be evaluated for all threads, the kernel howber will firt look to the main thread and if the signal is blocked
 *  it will try to find another tread that is not blocked. If multiple are eligible, it's random selected.
 *
 *  Above text is to explain why we run through the list of al child pid's and cannot just check the pid from the signal/ nor use the status from the signal when signal-pid/lsited pid's.
 *  As then some child processes will be missed or srong status code is reported.
 */
static void _sigchld_handler(siginfo_t *si, void *ctx)
{
    int rc = 0;
    int status = 0;
    vlist_t *node;
    vsystem_command_t *cmd = NULL;
    vevent_reason_t reason = VEVENT_OCCURED;

    if (vmutex_lock(&g_cmdlist_lock) != 0) {
        vapi_error("Unable to acquire lock!");
    }

    /* Reap all terminated child processes (make sure it does not stay as zombie). */
    vlist_foreach(&g_commands, node) {
        cmd = (vsystem_command_t *)node;
        do {
            rc = waitpid(cmd->pid, &status, WNOHANG);
            if (rc == -1) {
                if (errno == EINTR) {
                    // retry with same pid
                    continue;
                } else if (errno == ECHILD) {
                    // using a pid which is not recognized as child, do just cleanup
                    vapi_warning("waitpid() error for pid %d : %s", cmd->pid, strerror(errno));
                    vlist_delete(&cmd->node);
                    break;
                } else {
                    vapi_warning("waitpid() error for pid %d : %s", cmd->pid, strerror(errno));
                    break;
                }
            } else if (rc == 0) {
                /* as we iterate through all pid's this can happen, that no state change */
                break;
            } else {
                /* the rc == pid */
                /* use status to return */
                if (WIFEXITED(status)) {
                    status = WEXITSTATUS(status);
                    if (status == 127) {   // command failure
                        // this means the program did not get executed at all
                        // so report this as a failure, and not just as exit code
                        reason = VEVENT_FAILURE;
                    } else {
                        reason = VEVENT_OCCURED;
                    }
                } else if (WIFSIGNALED(status)) {
                    status = WTERMSIG(status);
                    reason = VEVENT_FAILURE;
                } else {
                    /* just ignore */
                    rc = 0;
                }
                break;
            }
        } while (1);

        if (rc <= 0) {
            continue;
        }

        vlist_delete(&cmd->node);
        _terminate(cmd, reason, status);
    }

    if (vmutex_unlock(&g_cmdlist_lock) != 0) {
        vapi_error("Unable to release lock!");
    }
}

/**
 * Callback for handling timed-out or canceled child processes.
 */
static void _timeout_handler(vevent_reason_t reason, void *ctx)
{
    vsystem_command_t *cmd = (vsystem_command_t *)ctx;

    /* hat_event is already deleted in vevent after this callback */
    cmd->hat_event = NULL;
    cmd->event_reason = reason;

    if (kill(cmd->pid, SIGKILL) != 0)
        vapi_warning("Could not kill pid %d : '%s'", cmd->pid, strerror(errno));
}

/**
 * libevent callback used to watch the stdout/stderr pipe coming from a child process.
 *
 * Nothing fancy here, we try to read the pipe and call the corresponding callback.
 */
static void _data_handler(evutil_socket_t fd, short what, void *ctx)
{
    char buffer[VSYSTEM_DATA_MAX_LEN] = {0};
    vsystem_command_t *cmd = (vsystem_command_t *)ctx;

    ssize_t count = _read_data_for_pid((int)fd, buffer, sizeof(buffer) - 1, cmd->pid);
    if (count > 0) {
        if (fd == cmd->fd[0]) {
            cmd->on_data(buffer, count, cmd->ctx);
        } else if (fd == cmd->fd_stderr[0]) {
            cmd->on_data_stderr(buffer, count, cmd->ctx);
        }
    } else if (count == 0) {
        if (fd == cmd->fd[0]) {
            event_free(cmd->data_event);
            cmd->data_event = NULL;
        } else if (fd == cmd->fd_stderr[0]) {
            event_free(cmd->data_event_stderr);
            cmd->data_event_stderr = NULL;
        }
    }
}

int vsystem_init(void)
{
    if (g_initialized) return 0;

    vmutex_create(&g_cmdlist_lock);
    vlist_init(&g_commands);

    vsignal_register(SIGCHLD, _sigchld_handler, NULL);

    g_initialized = true;

    return 0;
}

void vsystem_unset_supervisor_env(void)
{
    unsetenv("S6_NOTIFICATION_FD");
}

/**
 * Fork a command then exec it.
 *
 * The file descriptor array given as argument is supposed to have been opened already
 * as a pipe. The autoclose flag must be also set since we won't close it here.
 *
 * STDOUT and STDERR of the child processes are reopened as one end of the pipe, enabling
 * us to read the console output from that child within the parent process.
 */
static int _fork(char **command, pid_t *pid_out, int fd[])
{
    pid_t pid = -1;

    pid = fork();
    if (pid == 0) {
        /* This is the child process. */
        /*printf("fork ok ; child pid %d\n\r", pid);*/
        if (fd) {
            if (fd[1] != -1) {
                /* Redirect output of child process to the given pipe. */
                while ((dup2(fd[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
                while ((dup2(fd[1], STDERR_FILENO) == -1) && (errno == EINTR)) {}
            }
        }
        /* always redirect the stdin, so client cannot interfere with parent STDIN */
        /* added because of client resetting the O_NONBLOCK                        */
        int fd_dummy = open("/dev/null", O_RDWR);
        if (fd_dummy != -1) dup2(fd_dummy, STDIN_FILENO);

        /* Unset variables that should not be inherited */
        vsystem_unset_supervisor_env();
        _mark_all_fds_for_cloexec();

        /* Execute the shell command. */
        if (execvp(command[0], command) < 0) {   /* does not return if successful */
            _exit(127); // command not found, special treatment
        }
    } else if (pid < 0) {
        int the_errno = errno;
        vapi_warning("Fork failed : %s", strerror(the_errno));
        return -1;
    } else {
        /*printf("fork ok ; pid is %d\n\r", pid);*/
        /* Close the child end of the pipe on the parent side. */
        if (fd)
            close(fd[1]);
        *pid_out = pid;
    }
    return 0;
}

vevent_t *vsystem_exec1(char **command, vsystem_terminate_cb_t on_terminate, vsystem_data_cb_t on_data, void *ctx, pid_t *child_pid)
{
    /* Allocate memory for command information, this will be freed by the signal or timeout handler. */
    vsystem_command_t *new_cmd = vmem_calloc(vmem_alloc_default(), sizeof(vsystem_command_t));
    if (new_cmd == NULL) {
        vapi_warning("Failed to initialize memory area for vsystem_call().");
        return NULL;
    }

    new_cmd->on_terminate = on_terminate;
    new_cmd->on_data = on_data;
    new_cmd->ctx = ctx;
    new_cmd->fd[0] = -1;
    new_cmd->fd[1] = -1;
    new_cmd->extend = false;

    if (on_data) {
        /* Open the pipe that we'll use to redirect STDOUT/STDERR from the child process. */
        if (pipe2(new_cmd->fd, O_CLOEXEC | O_NONBLOCK) == -1) {
            vapi_warning("Failed to initialize pipe for vsystem_call() : '%s'", strerror(errno));
            vmem_free(vmem_alloc_default(), new_cmd);
            return NULL;
        }

        /* Create a read event on the parent end of the pipe. */
        new_cmd->data_event = event_new(vloop_get_base(), new_cmd->fd[0], EV_READ | EV_PERSIST, _data_handler, new_cmd);
        if (event_add(new_cmd->data_event, NULL) == -1) {
            vapi_warning("Failed to add data listening event for vsystem_call().");
            vmem_free(vmem_alloc_default(), new_cmd);
            return NULL;
        }
    }

    // if no data callback is given, redirect stdout/stderr to /dev/null
    else {
        new_cmd->fd[1] = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (new_cmd->fd[1] == -1) {
            vapi_warning("Failed to open /dev/null to redirect child traces.");
            vmem_free(vmem_alloc_default(), new_cmd);
            return NULL;
        }
    }

    new_cmd->hat_event = vevent_new(_timeout_handler, new_cmd);
    if (new_cmd->hat_event == NULL) {
        vapi_warning("Failed to create vevent handle.");
        if (on_data) {
            event_free(new_cmd->data_event);
        }
        vmem_free(vmem_alloc_default(), new_cmd);
        return NULL;
    }

    /* Spawn the child process. */
    if (_fork(command, &new_cmd->pid, new_cmd->fd) != 0) {
        if (on_data) {
            event_free(new_cmd->data_event);
        }
        vevent_delete(new_cmd->hat_event);
        vmem_free(vmem_alloc_default(), new_cmd);
        return NULL;
    }

    if (vmutex_lock(&g_cmdlist_lock) != 0)
        vapi_error("Unable to acquire lock!");

    vlist_add_tail(&g_commands, &new_cmd->node);

    if (vmutex_unlock(&g_cmdlist_lock) != 0)
        vapi_error("Unable to release lock!");

    if (child_pid)
        *child_pid = new_cmd->pid;

    return new_cmd->hat_event;
}

vevent_t *vsystem_exec(char **command, vsystem_terminate_cb_t on_terminate, vsystem_data_cb_t on_data, void *ctx)
{
    return vsystem_exec1(command, on_terminate, on_data, ctx, NULL);

}

int vsystem_execvp(const char *file, char *const argv[])
{
    return execvp(file, argv);
}

void vsystem_exit(int status)
{
    if (status != VAPI_SUCCESS)
        vapi_critical("Application exit called with error status: %d", status);

    exit(status);
}


/**
 * Fork a command then exec it with environments. env should end up with NULL
 *
 * The file descriptor array given as argument is supposed to have been opened already
 * as a pipe. The autoclose flag must be also set since we won't close it here.
 *
 * STDOUT and STDERR of the child processes are reopened as one end of the pipe, enabling
 * us to read the console output from that child within the parent process.
 */

int __exec(char **command, char **env, char *work_dir,
           vsystem_cgroup_cb_t cgroup_cb, void *ctx,
           pid_t *pid_out, int fd_stdout, int fd_stderr)
{
    int ret;
    pid_t pid = -1;

    pid = fork();
    if (pid == 0) {
        /* This is the child process. */
        if (fd_stdout != -1) {
            /* Redirect stdout output of child process to the given pipe. */
            while ((dup2(fd_stdout, STDOUT_FILENO) == -1) && (errno == EINTR)) {}
        }

        if (fd_stderr != -1) {
            /* Redirect stderr output of child process to the given pipe. */
            while ((dup2(fd_stderr, STDERR_FILENO) == -1) && (errno == EINTR)) {}
        }

        /* always redirect the stdin, so client cannot interfere with parent STDIN */
        /* added because of client resetting the O_NONBLOCK                        */
        int fd_dummy = open("/dev/null", O_RDWR);
        if (fd_dummy != -1) {
            /* Redirect stdin of child process to /dev/null. */
            while ((dup2(fd_dummy, STDIN_FILENO) == -1) && (errno == EINTR)) {}
        }

        /* Unset variables that should not be inherited */
        vsystem_unset_supervisor_env();
        _mark_all_fds_for_cloexec();

        /* change work directory if the target command need */
        if (work_dir) {
            chdir((const char *)work_dir);
            setenv("PATH", (const char *)work_dir, 1);
        }

        /* do cgroup setings before exec */
        if (cgroup_cb) cgroup_cb(ctx, getpid());

        /* Execute the shell command. */
        if (env) {
            ret = execvpe(command[0], command, env);
        } else {
            ret = execvp(command[0], command);
        }

        if (ret < 0) {   /* exec does not return if successful */
            int the_errno = errno;
            _exit(the_errno);
        }
    } else if (pid < 0) {
        int the_errno = errno;
        vapi_warning("Fork failed : %s", strerror(the_errno));
        return -1;
    } else {
        /* Close the child end of the pipe on the parent side. */
        if (fd_stdout >= 0)
            close(fd_stdout);
        if (fd_stderr >= 0)
            close(fd_stderr);
        *pid_out = pid;
    }
    return 0;
}

/**
 * clear all the resources in cmd on any error occurred
 */
static void vsystem_release_cmd(vsystem_command_t *cmd)
{
    if (!cmd) return;

    if (cmd->fd[0] >= 0)
        close(cmd->fd[0]);

    if (cmd->fd[1] >= 0)
        close(cmd->fd[1]);

    if (cmd->fd_stderr[0] >= 0)
        close(cmd->fd_stderr[0]);

    if (cmd->fd_stderr[1] >= 0)
        close(cmd->fd_stderr[1]);

    if (cmd->data_event)
        event_free(cmd->data_event);

    if (cmd->data_event_stderr)
        event_free(cmd->data_event_stderr);

    if (cmd->hat_event)
        vevent_delete(cmd->hat_event);

    vmem_free(vmem_alloc_default(), cmd);
}

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
                              void *ctx, pid_t *pid_out)
{
    /* Allocate memory for command information, this will be freed by the signal or timeout handler. */
    vsystem_command_t *new_cmd = vmem_calloc(vmem_alloc_default(), sizeof(vsystem_command_t));
    if (new_cmd == NULL) {
        vapi_warning("Failed to initialize memory area for vsystem_call().");
        return NULL;
    }

    new_cmd->on_terminate   = on_terminate;
    new_cmd->on_data        = on_data_stdout;
    new_cmd->on_data_stderr = on_data_stderr;
    new_cmd->ctx   = ctx;
    new_cmd->fd[0] = -1;
    new_cmd->fd[1] = -1;
    new_cmd->fd_stderr[0] = -1;
    new_cmd->fd_stderr[1] = -1;
    new_cmd->extend = true;

    if (on_data_stdout) {
        /* Open the pipe that we'll use to redirect STDOUT/STDERR from the child process. */
        if (pipe2(new_cmd->fd, O_CLOEXEC | O_NONBLOCK) == -1) {
            vapi_warning("Failed to initialize pipe for vsystem_call() : '%s'", strerror(errno));
            vsystem_release_cmd(new_cmd);
            return NULL;
        }

        /* Create a read event on the parent end of the pipe. */
        new_cmd->data_event  = event_new(vloop_get_base(), new_cmd->fd[0],  EV_READ | EV_PERSIST, _data_handler, new_cmd);
        if (event_add(new_cmd->data_event, NULL) == -1) {
            vapi_warning("Failed to add data listening event for vsystem_call().");
            vsystem_release_cmd(new_cmd);
            return NULL;
        }
    }
    else if (redirect_stdout >= 0)
        new_cmd->fd[1] = redirect_stdout;
    else {
        new_cmd->fd[1] = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (new_cmd->fd[1] == -1) {
            vapi_warning("Failed to open /dev/null to redirect child traces.");
            vsystem_release_cmd(new_cmd);
            return NULL;
        }
    }

    if (on_data_stderr) {
        /* Open the pipe that we'll use to redirect STDOUT from the child process. */
        if (pipe2(new_cmd->fd_stderr, O_CLOEXEC | O_NONBLOCK) == -1) {
            vapi_warning("Failed to initialize pipe for vsystem_call() : '%s'", strerror(errno));
            vsystem_release_cmd(new_cmd);
            return NULL;
        }

        /* Create a read event on the parent end of the pipe. */
        new_cmd->data_event_stderr = event_new(vloop_get_base(), new_cmd->fd_stderr[0], EV_READ | EV_PERSIST, _data_handler, new_cmd);
        if (event_add(new_cmd->data_event_stderr, NULL) == -1) {
            vapi_warning("Failed to add data listening event for vsystem_call().");
            vsystem_release_cmd(new_cmd);
            return NULL;
        }
    }
    else if (redirect_stderr >= 0) {
        new_cmd->fd_stderr[1] = redirect_stderr;
    }
    else {
        new_cmd->fd_stderr[1] = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (new_cmd->fd_stderr[1] == -1) {
            vapi_warning("Failed to open /dev/null to redirect child traces.");
            vsystem_release_cmd(new_cmd);
            return NULL;
        }
    }

    new_cmd->hat_event = vevent_new(_timeout_handler, new_cmd);
    if (new_cmd->hat_event == NULL) {
        vapi_warning("Failed to create vevent handle.");
        vsystem_release_cmd(new_cmd);
        return NULL;
    }

    /* Spawn the child process. */
    if (__exec(command, env, work_dir, cgroup_cb, new_cmd->ctx, &new_cmd->pid, new_cmd->fd[1], new_cmd->fd_stderr[1]) != 0) {
        vsystem_release_cmd(new_cmd);
        return NULL;
    }

    *pid_out = new_cmd->pid;

    if (vmutex_lock(&g_cmdlist_lock) != 0) {
        vapi_error("Unable to acquire lock!");
    }
    vlist_add_tail(&g_commands, &new_cmd->node);

    if (vmutex_unlock(&g_cmdlist_lock) != 0) {
        vapi_error("Unable to release lock!");
    }
    return new_cmd->hat_event;
}

/*!
 * \brief terminate child progress with pid.
 *
 * \param pid       IN pid of child progress.
 */
void vsystem_terminate(pid_t pid)
{
    if (kill(pid, SIGKILL) != 0) {
        vapi_warning("Could not kill pid %d : '%s'", pid, strerror(errno));
    }
}

void vsystem_stop_childs()
{
    vlist_t *node;
    vsystem_command_t *cmd = NULL;

    /* Stop all child processes
       No mutex lock as called already from fatal signal handler */
    vlist_foreach(&g_commands, node) {
        cmd = (vsystem_command_t *)node;

        if (cmd->extend) continue;

        if (kill(cmd->pid, SIGTERM) != 0) {
            vapi_warning("Could not stop pid %d : '%s'", cmd->pid, strerror(errno));
        }
    }
}

pid_t vsystem_getpid_by_name(const char *name)
{
    char cmd[64] = {0};
    snprintf(cmd, sizeof(cmd), "pidof %s", name);
    FILE *fp = popen(cmd, "re");
    if (!fp) return -1;

    char line[15] = {0};
    fgets(line, 15, fp);
    int stat = pclose(fp);
    if (stat != 0) return -1;

    pid_t pid = strtoul(line, NULL, 10);
    return pid;
}
