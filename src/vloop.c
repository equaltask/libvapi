#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <regex.h>
#include <pthread.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <getopt.h>
#include <event2/event.h>
#include <event2/util.h>
#include <event2/thread.h>

#include <libvapi/vloop.h>
#include <libvapi/vmem.h>
#include <libvapi/vsystem.h>
#include <libvapi/vfs.h>
//#include <libvapi/yconfig_json.h>
//#include <libvapi/ycov.h>
//#include <libvapi/ytnd_log.h>
#include <libvapi/vtnd.h>
#include <libvapi/vtnd_file.h>
#include <libvapi/vloop_demand_event.h>
#include <libvapi/vthread.h>
#include <libvapi/vtimer.h>

//#include "data/generated/vloop_cmdline.h"
#include "vlog_core.h"
#include "vsignal.h"
#include "vlog_vapi.h"
#include "vtnd_console.h"
#include "vmem_pool.h"
//#include "ynotify_internal.h"
//#include "yipc_internal.h"
//#include "yproto_dbg.h"
//#include "s6_supervision.h"
#include "vloop_internal.h"

#define MAX_BUILD_NAME_LEN 32
#define MAX_EXECUTABLE_NAME_LEN 64
#define MAX_PATH_LEN 512

#ifndef VAPI_DEFAULT_CONFIG
#define VAPI_DEFAULT_CONFIG ""
#endif

#ifndef VAPI_DEFAULT_CMD_FILE
#define VAPI_DEFAULT_CMD_FILE ""
#endif

static int register_dbg_interface(void);
static int vloop_on_demand_callback(vloop_on_demand_event_handle_t event_handle, char *buf, int size, void *ctx);

typedef struct {
    unsigned long loop_cnt;
    unsigned long action_cnt;
} vloop_stats_t;

typedef struct {
    struct event_base *base_loop;
    uint8_t locked;
    pid_t tid;
    vlist_t action_list;
    vloop_on_demand_event_handle_t on_demand_handle;
    char thread_name[32];
    vlist_t node;
    FILE *libevent_logfile;
    vloop_stats_t stats;
    vtimer_t sleep_timer;
} vloop_info_t;

static vthread_mutex_t vloop_list_lock;
static vlist_t vloop_list;

typedef struct {
    vloop_event_cb cb;
    vloop_event_handle_t vloop_handle;
    void *ctx;
} vloop_cb_ctx_t;

static __thread vloop_info_t vloop_info_gt = {0};


int vloop_dbg_init(const char *fixed_pty_path, const char *tnd_client_ipc)
{
    int ret = 0;
    char *yipc_id = NULL;

    ret = vtnd_init();
    if (ret < 0)
        return -1;

    if (fixed_pty_path != NULL)
        ret = vtnd_console_create(fixed_pty_path);
    else
        ret = vtnd_console_create("/dev/tty0");
    if (ret < 0) {
        printf("%s: tty create error\n", __func__);
        return -1;
    }

    /* check ytnd client 
    if (tnd_client_ipc != NULL && strlen(tnd_client_ipc) > 0) {
        yipc_id = (char *)tnd_client_ipc;
    } else if (ymain_opts_g.tnd_client_given != 0) {
        yipc_id = ymain_opts_g.tnd_client_arg;
    }*/

    if (yipc_id) {
        printf("vloop: starting ytnd client ... [%s]\n", yipc_id);
        ret = vtnd_client_start(yipc_id, vloop_get_application_name());
        if (ret != 0)
            printf("vloop: error enabling ytnd client\n");
        /* XXX hard coded vlog_ipc*/
        ret = vtnd_log_client_start("vlog_ipc");
        if (ret != 0)
            printf("vloop: error enabling vlog client\n");
    }

    register_dbg_interface();

    return 0;
}

static void vloop_cb(evutil_socket_t fd, short events, void *arg)
{
    vloop_cb_ctx_t *cb_ctx = (vloop_cb_ctx_t *)arg;
    if (!cb_ctx)
        return;

    char span_name[VLOG_MAX_SPAN_NAME] = {0};
    if (vlog_level_enabled_on_vapi_component(VLOOP_INDEX)) {
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vloop_callback_%p", cb_ctx);

        if ((cb_ctx->vloop_handle->jsonopentracer_context == NULL)
            || (cb_ctx->vloop_handle->jsonopentracer_context_size == 0))
            vlog_start_parent_span(span_name);
        else
            vlog_start_follows_from_span(span_name, cb_ctx->vloop_handle->jsonopentracer_context,
                                         cb_ctx->vloop_handle->jsonopentracer_context_size);
    }

    if (cb_ctx->cb)
        cb_ctx->cb(fd, cb_ctx->vloop_handle, cb_ctx->ctx); //TODO handle protector

    if (vlog_level_enabled_on_vapi_component(YIPC_INDEX))
        vlog_finish_span(span_name);
}

struct event_base *vloop_get_base()
{
    return vloop_info_gt.base_loop;
}

struct event_base *vloop_init(int max_prio)
{
    evthread_use_pthreads();

    struct event_base *base_loop = NULL;
    struct event_config *cfg = event_config_new();
    if (cfg) {
        event_config_set_flag(cfg, EVENT_BASE_FLAG_PRECISE_TIMER);
        //event_config_set_flag(cfg, EVENT_BASE_FLAG_NO_TIMERFD);
        event_config_set_flag(cfg, EVENT_BASE_FLAG_NO_CACHE_TIME);
        base_loop = event_base_new_with_config(cfg);
        event_config_free(cfg);
    }

    if (!base_loop) {
        printf("Error: failed to create event base!\r\n");
        return NULL;
    }

    vloop_info_gt.base_loop = base_loop;
    vlist_init(&vloop_info_gt.action_list);

    int retp = vloop_set_max_prio(max_prio);
    if (retp)
        vapi_error("Failed to set max loop priority to %d errorcode=%i\r\n", max_prio, retp);

    vloop_info_gt.tid = syscall(SYS_gettid);

    vloop_info_gt.on_demand_handle = vloop_add_on_demand_event(vloop_on_demand_callback, 0, NULL);
    strcpy(vloop_info_gt.thread_name, vthread_getselfname());

    vmutex_lock(&vloop_list_lock);
    vlist_add_tail(&vloop_list, &vloop_info_gt.node);
    vmutex_unlock(&vloop_list_lock);

    return base_loop;
}

typedef struct {
    vlist_t node;
    vloop_action_cb func;
    void *ctxt;
} vloop_action;

vloop_action_t vloop_action_create(vloop_action_cb cb, void *ctxt)
{
    vloop_action *action = vmem_malloc(vmem_alloc_default(), sizeof(vloop_action));
    if (action) {
        action->func = cb;
        action->ctxt = ctxt;

        vlist_add_tail(&vloop_info_gt.action_list, &action->node);
    }
    return action;
}

void vloop_action_delete(vloop_action_t action_ctxt)
{
    vloop_action *action = (vloop_action *)action_ctxt;
    if (action) {
        vlist_delete(&action->node);
        vmem_free(vmem_alloc_default(), action);
    }
}

int vloop_action_process(vloop_info_t *vloop)
{
    if (vloop == NULL)
        vloop = &vloop_info_gt;

    vlist_t *action_list = &vloop->action_list;
    if (vlist_is_empty(action_list))
        return 0;

    /* first move all elements from the global list to a local list on the stack.
     * This is to prevent keep processing actions forever when in the context of
     * an action new entries are created.
     */

    vlist_t local_list;
    local_list.next = action_list->next;
    local_list.prev = action_list->prev;

    action_list->next->prev = &local_list;
    action_list->prev->next = &local_list;

    vlist_init(action_list);

    unsigned long cnt = 0;
    while (!vlist_is_empty(&local_list)) {
        vlist_t *node;
        vlist_get_head(&local_list, node);

        vloop_action *action = container_of(vloop_action, node, node);
        vlist_delete(node);

        action->func((vloop_action_t)action, action->ctxt);
        cnt++;
    }

    vloop->stats.action_cnt += cnt;
    if (vlist_is_empty(action_list))
        return 0;
    else
        return 1;
}

struct event_base *vloop_main_init(int argc, char *argv[])
{
    vlog_set_default_threshold(VLOG_CONSOLE_INDEX, VLOG_DEBUG, VLOG_INTERNAL);
    vlog_set_default_threshold(VLOG_LOGFILE_INDEX, VLOG_DEBUG, VLOG_INTERNAL);
    vlog_set_static_tags();

    if (vlog_vapi_init() != 0) {
        return NULL;
    }

    struct event_base *base_loop = vloop_init(4);
    if (!base_loop) {
        return NULL;
    }

    vloop_dbg_init(NULL, NULL);

    if (vlog_init() != 0) {
        return NULL;
    }

#define YINIT_MOD(__mod)  do {                                                            \
          int __rc = __mod ## _init();                                                      \
          if (__rc != 0) {                                                                  \
              vapi_error("Failed to initialize module '" #__mod "' (err=%d)", __rc);        \
              return NULL;                                                                  \
          }                                                                                 \
      } while(0)

    //initialize core systems (beware: as there are dependencies between those modules, order is VERY important)
    YINIT_MOD(vsignal);
    YINIT_MOD(vsystem);
    //YINIT_MOD(yipc_dbg);
    //YINIT_MOD(yproto_dbg);
    //YINIT_MOD(vmem_dbg);
    //YINIT_MOD(ynotify_dbg);
    //YINIT_MOD(ywatchdog);

    //if (yconfig_late_init() != 0)
    //    return NULL;

    //if (ywatchdog_start() != 0)
    //    return NULL;

    if (vmem_trim_start() != 0)
        return NULL;

    return base_loop;
}

int vloop_execute_cmd_file(const char *cmd_file)
{
    if (cmd_file != NULL && strlen(cmd_file) > 0)
        vtnd_file_init(cmd_file);
    //else if (ymain_opts_g.cmd_file_given)
    //    vtnd_file_init(ymain_opts_g.cmd_file_arg);
    else
        vtnd_file_init(VAPI_DEFAULT_CMD_FILE);
    
    return 0;
}

int vloop_set_max_prio(int max_prio)
{
    if (max_prio <= 0)
        return -1;

    if (max_prio > 1) {
        if (!vloop_get_base())
            return -2;

        if (event_base_priority_init(vloop_get_base(), max_prio))
            return -3;
    }

    return 0;
}

int vloop_set_read_event_prio(vloop_event_handle_t vloop_event_handle, int prio_read)
{
    return event_priority_set(vloop_event_handle->read_handle, prio_read);
}

int vloop_set_write_event_prio(vloop_event_handle_t vloop_event_handle, int prio_write)
{
    return event_priority_set(vloop_event_handle->write_handle, prio_write);
}

vloop_event_handle_t vloop_add_fd(int fd, vloop_fd_event_t event_type, vloop_event_cb read_cb,
                                  vloop_event_cb write_cb, void *ctx)
{
    vloop_event_handle_t vloop_handle = vmem_calloc(vmem_alloc_default(), sizeof(*vloop_handle));
    if (!vloop_handle) {
        return NULL;
    }

    vloop_handle->jsonopentracer_context = NULL;
    vloop_handle->jsonopentracer_context_size = 0;

    do {
        if (event_type == VLOOP_FD_READ || event_type == VLOOP_FD_READ_AND_WRITE) {
            vloop_cb_ctx_t *cb_ctx = vmem_malloc(vmem_alloc_default(), sizeof(vloop_cb_ctx_t));
            if (!cb_ctx)
                break;

            cb_ctx->cb = read_cb;
            cb_ctx->vloop_handle = vloop_handle;
            cb_ctx->ctx = ctx;
            vloop_handle->read_handle = event_new(vloop_get_base(), fd, EV_PERSIST | EV_READ, vloop_cb, cb_ctx);
            if (!vloop_handle->read_handle) {
                vmem_free(vmem_alloc_default(), cb_ctx);
                break;
            }
        }

        if (event_type == VLOOP_FD_WRITE || event_type == VLOOP_FD_READ_AND_WRITE) {
            vloop_cb_ctx_t *cb_ctx = vmem_malloc(vmem_alloc_default(), sizeof(vloop_cb_ctx_t));
            if (!cb_ctx)
                break;

            cb_ctx->cb = write_cb;
            cb_ctx->vloop_handle = vloop_handle;
            cb_ctx->ctx = ctx;
            vloop_handle->write_handle = event_new(vloop_get_base(), fd, EV_PERSIST | EV_WRITE, vloop_cb, cb_ctx);
            if (!vloop_handle->write_handle) {
                vmem_free(vmem_alloc_default(), cb_ctx);
                break;
            }
        }
        //TODO handle_protector
        return vloop_handle;
    } while (0);

    //something went wrong, backout
    //TODO handle_protector (an inbetween function will be required for the free
    vloop_remove_fd(vloop_handle);
    return NULL;
}

vloop_event_handle_t vloop_add_fd_ot(int fd, vloop_fd_event_t event_type, vloop_event_cb read_cb,
                                     vloop_event_cb write_cb, void *ctx,
                                     vlog_opentracing_context_ptr jsonopentracer_context,
                                     int jsonopentracer_context_size)
{
    vloop_event_handle_t vloop_handle = vloop_add_fd(fd, event_type, read_cb, write_cb, ctx);

    if (vloop_handle != NULL && jsonopentracer_context != NULL && jsonopentracer_context_size > 0) {
        vloop_handle->jsonopentracer_context = vmem_malloc(vmem_alloc_default(), jsonopentracer_context_size * sizeof(vlog_opentracing_context));
        if (vloop_handle->jsonopentracer_context != NULL) {
            memcpy(vloop_handle->jsonopentracer_context, jsonopentracer_context, jsonopentracer_context_size);
            vloop_handle->jsonopentracer_context_size = jsonopentracer_context_size;
        }
    }

    return vloop_handle;
}

int vloop_enable_cb(vloop_event_handle_t vloop_event_handle, vloop_fd_event_t event_type)
{
    if (!vloop_event_handle) {
        return -1;
    }

    switch (event_type) {
    case VLOOP_FD_READ_AND_WRITE:
        if (!vloop_event_handle->read_handle || !vloop_event_handle->write_handle) {
            return -1;
        }
        break;

    case VLOOP_FD_READ:
        if (!vloop_event_handle->read_handle) {
            return -1;
        }
        break;

    case VLOOP_FD_WRITE:
        if (!vloop_event_handle->write_handle) {
            return -1;
        }
        break;

    default:
        //should never happen
        return -1;
    }

    if (event_type == VLOOP_FD_READ || event_type == VLOOP_FD_READ_AND_WRITE) {
        if (event_add(vloop_event_handle->read_handle, 0) != 0) {
            return -1;
        }
    }

    if (event_type == VLOOP_FD_WRITE || event_type == VLOOP_FD_READ_AND_WRITE) {
        if (event_add(vloop_event_handle->write_handle, 0) != 0) {
            //need to remove the other one too
            if (event_type == VLOOP_FD_READ_AND_WRITE)
                event_del(vloop_event_handle->read_handle);

            return -1;
        }
    }

    return 0;
}

int vloop_disable_cb(vloop_event_handle_t vloop_event_handle, vloop_fd_event_t event_type)
{
    if (!vloop_event_handle) {
        return -1;
    }

    //first check the double, makes cleanup logic easier
    if (event_type == VLOOP_FD_READ_AND_WRITE) {
        if (!vloop_event_handle->read_handle || !vloop_event_handle->write_handle) {
            return -1;
        }
    }

    if (event_type == VLOOP_FD_READ || event_type == VLOOP_FD_READ_AND_WRITE) {
        if (vloop_event_handle->read_handle)
            //don't check return code, if it was never added it is also ok
            event_del(vloop_event_handle->read_handle);
        else
            return -1;
    }

    if (event_type == VLOOP_FD_WRITE || event_type == VLOOP_FD_READ_AND_WRITE) {
        if (vloop_event_handle->write_handle)
            //don't check return code, if it was never added it is also ok
            event_del(vloop_event_handle->write_handle);
        else
            return -1;
    }

    return 0;
}

int vloop_remove_fd(vloop_event_handle_t vloop_event_handle)
{
    if (!vloop_event_handle) {
        return -1;
    }

    if (vloop_event_handle->read_handle) {
        event_del(vloop_event_handle->read_handle);
        void *cb_ctx = event_get_callback_arg(vloop_event_handle->read_handle);
        if (cb_ctx != NULL)
            vmem_free(vmem_alloc_default(), cb_ctx);

        event_free(vloop_event_handle->read_handle);
    }

    if (vloop_event_handle->write_handle) {
        event_del(vloop_event_handle->write_handle);
        void *cb_ctx = event_get_callback_arg(vloop_event_handle->write_handle);
        if (cb_ctx != NULL)
            vmem_free(vmem_alloc_default(), cb_ctx);

        event_free(vloop_event_handle->write_handle);
    }

    if (vloop_event_handle->jsonopentracer_context)
        vmem_free(vmem_alloc_default(), vloop_event_handle->jsonopentracer_context);

    vmem_free(vmem_alloc_default(), vloop_event_handle);
    return 0;
}

void vloop_init_global_list()
{
    static int initialized = 0;
    if (initialized)
        return;

    initialized = 1;
    vmutex_create(&vloop_list_lock);
    vlist_init(&vloop_list);
}

int event_process_loop(struct event_base *base_loop)
{
    int rc = 0;

    vloop_info_t *vloop = &vloop_info_gt;
    do {
        int flags = EVLOOP_ONCE;

        // If in the context of the action callbacks a new callback is registered,
        // we call the eventloop in a non-blocking way.
        if (vloop_action_process(vloop)) {
            flags |= EVLOOP_NONBLOCK;
        }
        vloop->stats.loop_cnt++;
        rc = event_base_loop(base_loop, flags);
    } while (rc == 0);

    return rc;
}

int vloop_main(int argc, char *argv[])
{
    int rc = EXIT_FAILURE;
    int argc_init = argc;
    char **argv_init = argv;
    struct event_base *base_loop;

    vloop_init_global_list();

    base_loop = vloop_main_init(argc, argv);

    //TODO error record?
    if (!base_loop)
        return EXIT_FAILURE;

    /* reset internal libc getopt variables */
    optind = 0;
    opterr = 0;

    rc = vloop_app_init(argc_init, argv_init);
    if (rc != VAPI_SUCCESS) {
        vapi_critical("Application init returned %d, exiting", rc);
        goto cleanup;
    }

    vloop_execute_cmd_file(NULL);

    rc = event_process_loop(base_loop);
    if (rc == 0 || rc == 1) {
        vapi_warning("Event loop exited: rc=%d", rc);
        rc = EXIT_SUCCESS;
    } else {
        vapi_critical("Event loop failure: rc=%d", rc);
        rc = EXIT_FAILURE;
    }

cleanup:
    //make sure stdin is restored as before
    vtnd_console_reset();
    return rc;
}

////////////////////// libevent logging ////////////////////////////

static  int timestamped_log;

static void write_to_file_cb(int severity, const char *msg)
{
    const char *s;
    if (!vloop_info_gt.libevent_logfile)
        return;

    switch (severity) {
    case _EVENT_LOG_DEBUG:
        s = "debug";
        break;
    case _EVENT_LOG_MSG:
        s = "msg";
        break;
    case _EVENT_LOG_WARN:
        s = "warn";
        break;
    case _EVENT_LOG_ERR:
        s = "error";
        break;
    default:
        s = "?";
        break; /* never reached */
    }
    if (timestamped_log) {
        struct timespec time;
        clock_gettime(CLOCK_MONOTONIC, &time);
        fprintf(vloop_info_gt.libevent_logfile, "%08ld:%09ld [%s] %s\n", time.tv_sec, time.tv_nsec, s, msg);
    } else {
        fprintf(vloop_info_gt.libevent_logfile, "[%s] %s\n",  s, msg);
    }
}

static void vloop_libevent_logging(int on)
{
    vlist_t *node = NULL;
    vlist_foreach(&vloop_list, node) {
        vloop_info_t *vloop = container_of(vloop_info_t, node, node);

        if (on) {
            if (vloop->libevent_logfile == NULL) {
                char logfilename[128];
                sprintf(logfilename, "../log/libevent_%d", vloop->tid);
                vloop->libevent_logfile = fopen(logfilename, "w");
            }
        } else {
            if (vloop->libevent_logfile) {
                fclose(vloop->libevent_logfile);
                vloop->libevent_logfile = NULL;
            }
        }
    }
    if (on) {
        event_set_log_callback(write_to_file_cb);
        event_enable_debug_logging(EVENT_DBG_ALL);
    } else {
        event_enable_debug_logging(EVENT_DBG_NONE);
        event_set_log_callback(NULL);
    }
}

////////////////////// debug interface ////////////////////////////

static void sleep_timer(vtimer_t timer, void *ctx)
{
    int sleeptime = *(int *)ctx;
    usleep(sleeptime * 1000);
}

static void vloop_execute_command(char *cmd)
{
    if (cmd[0] == 't') {
        vtimer_dump_all();
    } else if (cmd[0] == 'e') {
        struct event_base *base = vloop_get_base();
        struct timeval now;

        event_base_gettimeofday_cached(base, &now);
        printf("\ncached_time : %ld:%06ld\n\n", now.tv_sec, now.tv_usec);
        event_base_dump_events(base, stdout);
    } else if (cmd[0] == 's') {
        int period, time;
        int nrargs = vdbg_scan_args(cmd, "s %d %d", &period, &time);
        if (nrargs == 2) {
            if (vloop_info_gt.sleep_timer) {
                vtimer_delete(vloop_info_gt.sleep_timer);
                vloop_info_gt.sleep_timer = NULL;
            }
            if (period) {
                vloop_info_gt.sleep_timer = vtimer_start_periodic(sleep_timer, period, (void *)&time);
            }
        }
    }
}

static int vloop_on_demand_callback(vloop_on_demand_event_handle_t event_handle, char *buf, int size, void *ctx)
{
    char command[64];
    if ((unsigned)size >= sizeof(command))
        return -1;

    memcpy(command, buf, size);
    command[size] = '\0';
    vloop_execute_command(command);

    return 0;
}

static void vloop_dbg_help(void *ctx)
{
    vdbg_printf("vloop debug Help:\n");
    vdbg_printf("------------------\n");
    vdbg_printf("Available commands:\n");
    vdbg_printf("* set                      : set vloop parameters\n");
    vdbg_printf("* get                      : get vloop parameters\n");
    vdbg_printf("* show ctxt [clear]        : show all vloop context and optionally clear stats.\n");
    vdbg_printf("* show events [threadname] : show all libevent data.\n");
    vdbg_printf("* show timers [threadname] : show all ytimer contexts.\n");
    vdbg_printf("\n");
}

static void set_help(void *ctx)
{
    vdbg_printf("vloop debug Help: set\n");
    vdbg_printf("---------------------\n");
    vdbg_printf("\n");
    vdbg_printf("vloop set libevent_trace <0/1>\n");
    vdbg_printf("vloop set sleep <threadname> <period ms> <sleeptime ms>\n");
    vdbg_printf("                   use 'vloop set sleep <threadname> 0 0' to cancel the timer\n");
    vdbg_printf("\n");
}

static void get_help(void *ctx)
{
    vdbg_printf("vloop debug Help: get\n");
    vdbg_printf("---------------------\n");
    vdbg_printf("\n");
    vdbg_printf("Usage: vloop get libevent_trace\n");
    vdbg_printf("\n");
}

static vloop_info_t *get_loop(char *threadname)
{
    vlist_t *node = NULL;
    vloop_info_t *retval = NULL;

    vmutex_lock(&vloop_list_lock);

    vlist_foreach(&vloop_list, node) {
        vloop_info_t *vloop = container_of(vloop_info_t, node, node);
        if (strcmp(vloop->thread_name, threadname) == 0) {
            retval = vloop;
            break;
        }
    }
    vmutex_unlock(&vloop_list_lock);

    return retval;
}

static int vloop_dbg_cmd_show_helper(char *args)
{
    vloop_info_t *vloop = NULL;

    char threadname[64] = {'\0'};
    char command_str[64] = {'\0'};
    char command[2];

    int nrargs = vdbg_scan_args(args, "%63s %63s", command_str, threadname);

    if (strncmp(command_str, "timers", 6) == 0)
        command[0] = 't';
    else if (strncmp(command_str, "events", 6) == 0)
        command[0] = 'e';

    command[1] = '\0';

    if (nrargs == 2)
        vloop = get_loop(threadname);

    if (vloop == NULL)
        vloop_execute_command(command);
    else
        vloop_trigger_on_demand_event(vloop->on_demand_handle, command, strlen(command));

    return 0;
}

static int vloop_dbg_cmd_show(char *cmd, char *args, void *ctx)
{
    if (strncmp(args, "ctxt", 4) == 0) {
        int clear_stats = 0;
        vlist_t *node = NULL;

        if (strstr(args, "clear") != NULL)
            clear_stats = 1;

        vmutex_lock(&vloop_list_lock);

        vlist_foreach(&vloop_list, node) {
            vloop_info_t *vloop = container_of(vloop_info_t, node, node);
            vdbg_printf("vloop %p\n", vloop);
            vdbg_printf("\tthreadname : %s\n", vloop->thread_name);
            vdbg_printf("\tevent_base : %p\n", vloop->base_loop);
            vdbg_printf("\ttid        : %d\n", vloop->tid);
            vdbg_printf("\tstats:\n");
            vdbg_printf("\t\t#loops     : %ld\n", vloop->stats.loop_cnt);
            vdbg_printf("\t\t#actions   : %ld\n", vloop->stats.action_cnt);
            vdbg_printf("\n");

            if (clear_stats)
                memset(&vloop->stats, 0, sizeof(vloop->stats));
        }
        vmutex_unlock(&vloop_list_lock);
    } else if ((strncmp(args, "timers", 6) == 0) || (strncmp(args, "events", 6) == 0)) {
        return vloop_dbg_cmd_show_helper(args);
    } else if (strncmp(args, "dump", 4) == 0) {
        for (int i = 0; i < 1000000; i++) {
            vdbg_printf("%10d TEST TEST TEST TEST TEST TEST TEST \n", i);
        }
    } else
        return -1;

    return 0;
}

static int set_cmd(char *cmd, char *args, void *ctx)
{
    char command_str[64] = {'\0'};

    if (strncmp(args, "sleep", 5) == 0) {
        char threadname[64] = {'\0'};
        int period, time;

        int nrargs = vdbg_scan_args(args, "%63s %63s %d %d", command_str, threadname, &period, &time);
        if (nrargs != 4)
            return -1;

        vloop_info_t *vloop = get_loop(threadname);
        if (vloop) {
            snprintf(command_str, sizeof(command_str), "s %d %d", period, time);
            vloop_trigger_on_demand_event(vloop->on_demand_handle, command_str, strlen(command_str));
        } else {
            vdbg_printf("Threadname not found\n");
            return -1;
        }
    } else if (strncmp(args, "libevent_trace", 14) == 0) {
        int enabled;
        int nrargs = vdbg_scan_args(args, "%63s %d", command_str, &enabled);
        if (nrargs != 2)
            return -1;
        vloop_libevent_logging(enabled);
    } else {
        vdbg_printf("unknown command\n");
    }
    return 0;
}

static int get_cmd(char *cmd, char *args, void *ctx)
{
    char command_str[64] = {'\0'};

    int nrargs = vdbg_scan_args(args, "%63s", command_str);
    if ((nrargs != 1) || (strcmp(command_str, "libevent_trace") != 0))
        return -1;

    if (vloop_info_gt.libevent_logfile)
        vdbg_printf("logging enabled\n");
    else
        vdbg_printf("logging disabled\n");

    return 0;
}

static int register_dbg_interface(void)
{
    vdbg_link_module("vloop", vloop_dbg_help, NULL);
    vdbg_link_cmd("vloop", "show", vloop_dbg_help, vloop_dbg_cmd_show, NULL);
    vdbg_link_cmd("vloop", "get", get_help, get_cmd, NULL);
    vdbg_link_cmd("vloop", "set", set_help, set_cmd, NULL);
    return 0;
}
