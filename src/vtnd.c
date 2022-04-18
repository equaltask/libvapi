#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <event2/event-config.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>
#include <event2/buffer.h>

#include <libvapi/vtnd.h>
#include <libvapi/vmem.h>
#include <libvapi/vtimer.h>

#include "vloop_internal.h"
#include "vlog_vapi.h"

static struct vtnd_ctx *local_tnd_ctx = NULL;
static struct bufferevent *vdbg_bev[2];


/******************************************************************************/

static uint32_t max_outbuf_len = 64 * 1024;

static int vdbg_bufferevent_write(struct bufferevent *bev, const char *str, int len)
{
    if (evbuffer_get_length(bufferevent_get_output(bev)) > max_outbuf_len) {
        return -1;
    }
    int ret = bufferevent_write(bev, str, len);
    if (ret == 0) {
        ret = len;
    }
    return ret;
}

static int vdbg_outbuf_write(struct tnd_connection *conn, const char *str, int len)
{
    if (conn->outbuf_len > max_outbuf_len) {
        return -1;
    }
    if ((conn->outbuf_len + len) > conn->outbuf_cap) {
        conn->outbuf_cap += ((len / 4096) + 1) * 4096;
        conn->outbuf = (char *)vmem_realloc(vmem_alloc_default(), conn->outbuf, conn->outbuf_cap);
    }
    memcpy(conn->outbuf + conn->outbuf_len, str, len);
    conn->outbuf_len += len;

    return len;
}

int vtnd_printf_msg_text(struct tnd_connection *conn, const char *txt)
{
    int ret = 0;
    char *error_str = NULL;

    if (local_tnd_ctx == NULL || conn == NULL)
        return -1;

    int len = strlen(txt);
    if (conn->output_bev) {
        error_str = "bev write failed";
        ret = vdbg_bufferevent_write(conn->output_bev, txt, len);
    } else if (conn->outbuf) {
        error_str = "outbuf write failed";
        ret = vdbg_outbuf_write(conn, txt, len);
    } else {
        error_str = "fd write failed";
        ret = write(conn->fd_out, txt, len);
    }
    if (ret < 0) {
        vapi_error("msg write failed");
        return -1;
    } else {
        return ret;
    }
}


int vtnd_printf_connection(struct tnd_connection *conn, const char *fmt, ...)
{
    va_list arg;
    char str[LEN_PRINT] = "";
    int len;
    char *ptr = str;

    if (local_tnd_ctx == NULL || conn == NULL)
        return -1;

    va_start(arg, fmt);
    len = vsnprintf(ptr, LEN_PRINT, fmt, arg);
    va_end(arg);

    if (len > LEN_PRINT) {
        len += 1;
        ptr = vmem_malloc(vmem_alloc_default(), len);
        va_start(arg, fmt);
        len = vsnprintf(ptr, len, fmt, arg);
        va_end(arg);
    }

    int ret;
    if (conn->output_bev)
        ret = vdbg_bufferevent_write(conn->output_bev, ptr, len);
    else if (conn->outbuf)
        ret = vdbg_outbuf_write(conn, ptr, len);
    else
        ret = write(conn->fd_out, ptr, len);

    if (ptr != str) {
        vmem_free(vmem_alloc_default(), ptr);
    }

    return ret;
}


int vtnd_print_prompt(struct tnd_connection *conn)
{
    int ret = 0;

    if (conn->current != NULL) {
        if (strlen(conn->current->prompt) > 0)
            ret = vtnd_printf_connection(conn, "%s >> ", conn->current->prompt);
        else
            ret = vtnd_printf_connection(conn, "%s >> ", conn->current->name);
    } else {
        ret = vtnd_printf_connection(conn, ">> ");
    }

    return ret;
}


/******************************************************************************/

int vdbg_vprintf(const char *fmt, va_list args)
{
    int ret = 0;
    struct tnd_connection *conn = NULL;

    if (!vdbg_is_initialized()) {
        vapi_error("vtnd not initialized");
        return -1;
    }

    if (local_tnd_ctx->cur_req == NULL) {
        vapi_error("not allowed to call outside of tnd request");
        return -1;
    } else {
        conn = local_tnd_ctx->cur_req->conn;
    }

    if (conn->output_bev || conn->outbuf) {
        char str[LEN_PRINT] = "";
        char *ptr = str;
        va_list args_copy;

        /* do a copy in case vsnprintf second call */
        va_copy(args_copy, args);

        int len = vsnprintf(ptr, LEN_PRINT, fmt, args);
        if (len > LEN_PRINT) {
            len += 1;
            ptr = vmem_malloc(vmem_alloc_default(), len);
            /* due to above vsnprintf call has consumed the 'args',
             * so here we should use 'args_copy' */
            len = vsnprintf(ptr, len, fmt, args_copy);
        }

        va_end(args_copy);

        if (conn->output_bev) {
            ret = vdbg_bufferevent_write(conn->output_bev, ptr, len);
        } else if (conn->outbuf) {
            ret = vdbg_outbuf_write(conn, ptr, len);
        }
        if (ptr != str) {
            vmem_free(vmem_alloc_default(), ptr);
        }
    } else {
        ret = vdprintf(conn->fd_out, fmt, args);
    }

    return ret;
}


int vdbg_printf(const char *fmt, ...)
{
    int ret = 0;
    va_list arg;

    va_start(arg, fmt);
    ret = vdbg_vprintf(fmt, arg);
    va_end(arg);

    return ret;
}


int vdbg_scan_args(const char *buf, const char *fmt, ...)
{
    int i;
    va_list args;

    va_start(args, fmt);
    i = vsscanf(buf, fmt, args);
    va_end(args);

    return i;
}

int vdbg_hexdump(void *mem, size_t len)
{
    char *buffer = NULL;
    int ret = -1;

    ret = hexdump(mem, len, &buffer);
    if (ret == -1) {
        vdbg_printf("vdbg_hexdump:failed\n");
        if (buffer != NULL) {
            free(buffer);
        }
        return -1;
    }

    vdbg_printf("%s\n", buffer);
    free(buffer);

    return ret;
}

/******************************************************************************/


int vtnd_module_new(const char *name, struct vtnd_module **mod)
{
    int ret = -1;
    struct vtnd_module *tmp = NULL;

    assert(mod != NULL);

    if (!vdbg_is_initialized()) {
        vapi_error("vtnd not initialized");
        return -1;
    }

    /* create new module object */
    tmp = calloc(1, sizeof(struct vtnd_module));
    if (tmp == NULL) {
        vapi_error("malloc [%s]", strerror(errno));
        ret = -1;
    } else {
        snprintf(tmp->name, LEN_NAME, "%s", name);
        vlist_init(&(tmp->cmd_list));
        vlist_init(&(tmp->node));
        vlist_add_tail(&(local_tnd_ctx->mod_list), &(tmp->node));
        local_tnd_ctx->len++;
        ret = 0;
        *mod = tmp;
    }

    return ret;
}


int vtnd_module_remove(struct vtnd_module *del)
{
    if (!vdbg_is_initialized()) {
        vapi_error("vtnd not initialized");
        return -1;
    }

    if (del == NULL) {
        return -1;
    } else {
        vlist_delete(&(del->node));
        free(del);
        local_tnd_ctx->len--;
        return 0;
    }
}


int vtnd_module_get_by_name(const char *name, struct vtnd_module **mod)
{
    int ret = -1;
    struct vtnd_module *tmp = NULL;
    struct vlist *nodep = NULL;

    if (!vdbg_is_initialized()) {
        vapi_error("vtnd not initialized");
        return -1;
    }

    if (mod != NULL)
        *mod = NULL;

    vlist_foreach(&(local_tnd_ctx->mod_list), nodep) {
        tmp = container_of(struct vtnd_module, node, nodep);
        if (strcmp(name, tmp->name) == 0) {
            ret = 0;
            if (mod != NULL)
                *mod = tmp;
            break;
        }
    }

    return ret;
}


int vtnd_module_set_current(struct tnd_connection *conn, const char *mod_name)
{
    struct vtnd_module *mod = NULL;

    if (conn == NULL) {
        vapi_error("connection required to set current module!");
        return -1;
    }

    /* reset module */
    if (mod_name == NULL || strlen(mod_name) == 0) {
        conn->current = NULL;
        return 0;
    }

    if (vtnd_module_get_by_name(mod_name, &mod) < 0) {
        vapi_error("module [%s] not found", mod_name);
        return -1;
    }

    conn->current = mod;
    return 0;
}


int vtnd_get_base(struct vtnd_ctx **ctx)
{
    if (local_tnd_ctx == NULL) {
        vapi_error("vtnd not initialized");
        return -1;
    } else if (ctx == NULL) {
        vapi_error("ctx pointer null");
        return -1;
    } else {
        *ctx = local_tnd_ctx;
        return 0;
    }
}

/******************************************************************************/

static void vtnd_dbg_help(void *ctx)
{
    vdbg_printf("vtnd debug Help:\n");
    vdbg_printf("----------------\n");
    vdbg_printf("\n");
    vdbg_printf("Available commands:\n");
    vdbg_printf("* get          getting log parameters\n");
    vdbg_printf("* set          setting log parameters\n");
}

static void get_help(void *ctx)
{
    vdbg_printf("vtnd debug Help: get\n");
    vdbg_printf("--------------------\n");
    vdbg_printf("\n");
    vdbg_printf("Usage: vtnd get <params>\n");
    vdbg_printf("\n");
    vdbg_printf("Parameters:\n");
    vdbg_printf("* output_buffer_size                 get the size of the output buffer in bytes.\n");
}

static void set_help(void *ctx)
{
    vdbg_printf("vtnd debug Help: set\n");
    vdbg_printf("--------------------\n");
    vdbg_printf("\n");
    vdbg_printf("Usage: vtnd set <params>\n");
    vdbg_printf("\n");
    vdbg_printf("Parameters:\n");
    vdbg_printf("* output_buffer_size <bytes>         set the size of the output buffer in bytes.\n");
}

static int set_cmd(char *cmd, char *args, void *ctx)
{
    char param1[VDBG_MAX_CMD_LEN] = "";
    uint32_t value;

    if (vdbg_scan_args(args, "%s %u", param1, &value) != 2) {
        vdbg_printf("error: invalid input\n");
        return 0;
    }

    if (strncmp("output_buffer_size", param1, VDBG_MAX_CMD_LEN) == 0) {
        max_outbuf_len = value;
        vdbg_printf("output size set to %u\n", max_outbuf_len);
    } else {
        return -1;
    }
    return 0;
}

static int get_cmd(char *cmd, char *args, void *ctx)
{
    char param1[VDBG_MAX_CMD_LEN] = "";

    if (vdbg_scan_args(args, "%s", param1) != 1) {
        vdbg_printf("error: invalid input\n");
        return 0;
    }

    if (strncmp("output_buffer_size", param1, VDBG_MAX_CMD_LEN) == 0) {
        vdbg_printf("output size is %u bytes\n", max_outbuf_len);
    } else {
        return -1;
    }
    return 0;
}

static void vtnd_dbg_init(void)
{
    vdbg_link_module("vtnd", vtnd_dbg_help, NULL);
    vdbg_link_cmd("vtnd", "get", get_help, get_cmd, NULL);
    vdbg_link_cmd("vtnd", "set", set_help, set_cmd, NULL);
}

int vtnd_init(void)
{
    struct vtnd_ctx *new = NULL;

    if (vdbg_is_initialized()) {
        vapi_error("vtnd already initialized");
        return -1;
    }

    /* create new instance for vtnd_ctx */
    new = calloc(sizeof(struct vtnd_ctx), 1);
    if (new == NULL) {
        vapi_error("malloc [%s]", strerror(errno));
        return -1;
    } else {
        local_tnd_ctx = new;
        vlist_init(&(local_tnd_ctx->mod_list));
        vlist_init(&(new->proxy_list));
    }

    vtnd_dbg_init();

    return 0;
}


int vtnd_deinit(void)
{
    int ret = 0;
    struct vtnd_module *tmp = NULL;
    struct vlist *nodep = NULL;

    if (!vdbg_is_initialized()) {
        vapi_error("vtnd not initialized");
        return -1;
    }

    /* deinit tnd text modules */
    vlist_foreach(&(local_tnd_ctx->mod_list), nodep) {
        tmp = container_of(struct vtnd_module, node, nodep);
        vtnd_text_module_remove_cmds(tmp);
        vtnd_module_remove(tmp);
    }

    if (local_tnd_ctx->len == 0)
        ret = 0;
    else
        ret = -1;

    free(local_tnd_ctx);
    local_tnd_ctx = NULL;

    return ret;
}


int vdbg_is_initialized(void)
{
    return (local_tnd_ctx != NULL);
}


/******************************************************************************/

static void buffer_event_cb_read_helper(struct bufferevent *bev, void *ctx);
static vloop_action_t action_pending;
static vtimer_t overall_timeout_timer;

static void proceed_next_cmd(vloop_action_t action, void *ctx)
{
    vloop_action_delete(action);
    action_pending = NULL;

    buffer_event_cb_read_helper(vdbg_bev[1], NULL);
}

static void overall_timeout(vtimer_t timer, void *ctx)
{
    vtimer_delete(timer);
    overall_timeout_timer = NULL;

    vapi_error("timeout on async dbg command\n");

    vtnd_finish_context(ctx, 0);
}

void *vtnd_get_context()
{
    if (local_tnd_ctx->cur_req) {
        local_tnd_ctx->cur_req->async = true;
        bufferevent_disable(vdbg_bev[1], EV_READ);
        overall_timeout_timer = vtimer_start_timeout(overall_timeout, 30000, local_tnd_ctx->cur_req);
    }
    return local_tnd_ctx->cur_req;
}

void vtnd_set_context(void *ctx)
{
    if (ctx) {
        assert(local_tnd_ctx->cur_req == NULL);
    }
    local_tnd_ctx->cur_req = (struct vtnd_req *)ctx;
}

void vtnd_finish_context(void *ctx, int result)
{
    struct vtnd_req *wrapper_req = (struct vtnd_req *)ctx;
    if (wrapper_req == NULL) {
        return;
    }

    struct vtnd_cmd_req_txt *req = &(wrapper_req->req_txt);;
    if (req->done_cb != NULL) {
        req->done_cb(wrapper_req, result);
    }

    vmem_free(vmem_alloc_default(), wrapper_req);
    local_tnd_ctx->cur_req = NULL;

    /* We are ready to process a new request now so enabled in the input.
     * If events would already be pending in the queue, the rx callback will
     * not be triggered by libevent so let's create an action to check the
     * bufferevent as well.
     */
    bufferevent_enable(vdbg_bev[1], EV_READ | EV_PERSIST);
    if (action_pending == NULL) {
        action_pending = vloop_action_create(proceed_next_cmd, NULL);
    }

    if (overall_timeout_timer) {
        vtimer_delete(overall_timeout_timer);
        overall_timeout_timer = NULL;
    }
}

static int vtnd_req_process(struct vtnd_req *input)
{
    int ret = 0;

    if (!vdbg_is_initialized()) {
        vapi_error("vtnd not initialized");
        return -1;
    }

    if (input == NULL) {
        vapi_error("input NULL");
        return -1;
    }

    if (input->type == VTND_TXT) {
        local_tnd_ctx->cur_req = input;
        input->async = false;
        ret = vtnd_text_req_process(input);
        local_tnd_ctx->cur_req = NULL;
    } else {
        vapi_error("tnd input type [%d]", input->type);
        return -1;
    }

    return ret;
}


static void buffer_event_cb_event(struct bufferevent *bev, short what, void *ctx)
{
    vapi_debug("%s: trace: [%d]", __func__, what);
    return;
}


static void buffer_event_cb_read_helper(struct bufferevent *bev, void *ctx)
{
    while (1) {
        struct vtnd_req *in = vmem_malloc(vmem_alloc_default(), sizeof(struct vtnd_req));
        if (in == NULL) {
            vapi_error("malloc failed");
            return;
        }

        int ret = bufferevent_read(bev, in, sizeof(struct vtnd_req));
        if (ret <= 0) {
            vmem_free(vmem_alloc_default(), in);
            return;
        }
        if (ret != sizeof(struct vtnd_req)) {
            vapi_error("input len");
            vmem_free(vmem_alloc_default(), in);
        } else {
            int err = vtnd_req_process(in);
            // in case of async processing stop the loop here.
            if (in->async) {
                break;
            }
            if (err || (in->async == false)) {
                vmem_free(vmem_alloc_default(), in);
            }
        }
    }
}


static void buffer_event_cb_read(struct bufferevent *bev, void *ctx)
{
    /* If an action is still pending here, make sure to cancel it since
     * libevent triggered us and we have to make sure that only 1 command
     * is processed at the same time.
     */
    if (action_pending) {
        vloop_action_delete(action_pending);
        action_pending = NULL;
    }
    buffer_event_cb_read_helper(bev, ctx);
}


int vtnd_req_queue_init(struct tnd_connection *conn)
{
    int ret  = 0;
    struct event_base *base = vloop_get_base();

    if (vdbg_bev[0] == NULL) {
        ret = bufferevent_pair_new(base, BEV_OPT_CLOSE_ON_FREE, vdbg_bev);
        if (ret < 0) {
            vapi_error("bufferevent_pair_new");
            return -1;
        }
        bufferevent_setcb(vdbg_bev[1], buffer_event_cb_read, NULL, buffer_event_cb_event, NULL);
        bufferevent_enable(vdbg_bev[1], EV_READ | EV_PERSIST);
    }
    return 0;
}


int vtnd_req_enqueue(struct vtnd_req *req)
{
    int ret = 0;

    ret = bufferevent_write(vdbg_bev[0], req, sizeof(struct vtnd_req));
    if (ret < 0) {
        vapi_error("queueing tnd request");
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}
