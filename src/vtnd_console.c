
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>

#include <libvapi/vlog.h>
#include <libvapi/vloop.h>
#include <libvapi/vtnd.h>

#include "vlog_vapi.h"

#include <event2/bufferevent.h>
#include "vloop_internal.h"

static struct vtnd_console_ctx {
    int fd_term;
    int fd_slave;
    char pty_path[LEN_PATH];        /* path of the created pty */
    struct tnd_connection conn;
} vtnd_console_ctx;



static int tnd_req_cb_console(struct vtnd_req *req, int result)
{
    /* only print prompt, console is always interactive */
    vtnd_print_prompt(req->conn);
    return 0;
}


static int vtnd_console_create_tnd_req(char *cmd)
{
    int len = 0;
    struct vtnd_req new;

    /* strip leading spaces */
    while (isspace(*cmd))
        cmd++;

    len = strlen(cmd);
    if (len == 0) {
        vtnd_print_prompt(&(vtnd_console_ctx.conn));
        return 0;
    } else if (len >= VDBG_MAX_CMD_LEN) {
        vapi_warning("tnd command console too long");
        return -1;
    }

    new.type = VTND_TXT;
    snprintf(new.req_txt.cmd, VDBG_MAX_CMD_LEN, "%s", cmd);
    new.req_txt.done_cb = tnd_req_cb_console;
    new.req_txt.conn = &(vtnd_console_ctx.conn);

    new.conn = &(vtnd_console_ctx.conn);

    return vtnd_req_enqueue(&new);
}


#define YTND_CONSOLE_BUFFER_LEN     1024

static int vtnd_console_read_cb(int fd, vloop_event_handle_t event_handle, void *ctx)
{
    int ret = 0;
    static unsigned offset = 0;
    static char buffer[YTND_CONSOLE_BUFFER_LEN] = "";

    ret = read(fd, (buffer+offset), (YTND_CONSOLE_BUFFER_LEN-offset));
    if (ret < 0 && errno == EAGAIN) {
        /* ignore */
        goto exit_cb_console;
    } else if (ret < 0) {
        vapi_debug("read failed [%d] [%s]", ret, strerror(errno));
        goto exit_cb_console;
    }

    if (offset == 0 && (buffer[0] == '\0' || buffer[0] == '\n')) {
        vtnd_print_prompt(&(vtnd_console_ctx.conn));
        goto exit_cb_console;
    }

    /* process data */
    if (buffer[offset+ret-1] == '\n') {
        buffer[offset+ret-1] = '\0';
        char *line, *saveline = NULL;
        for(line = strtok_r(buffer, "\r\n", &saveline); line; line=strtok_r(NULL, "\r\n", &saveline)) {
            vtnd_console_create_tnd_req(line);
        }
        offset = 0;
    } else {
        offset += ret;
    }

    if (offset == YTND_CONSOLE_BUFFER_LEN) {
        vtnd_printf_connection(&(vtnd_console_ctx.conn), "Input too long, drop it.\r\n");
        offset = 0;
    }

exit_cb_console:
    return 0;
}


/**********************************************************************************************************************/


static int vtnd_console_create_pty(const char *path)
{
#ifdef _XOPEN_SOURCE
    int ret = 0;
    int fdm = -1;
    char pts_str[128];

    fdm = posix_openpt(O_RDWR);
    if (fdm < 0) {
        vapi_error("posix_openpt failed [%s]", strerror(errno));
        return -1;
    }

    ret = grantpt(fdm);
    if (ret != 0) {
        vapi_error("grantpt failed [%s]", strerror(errno));
        close(fdm);
        return -1;
    }

    ret = unlockpt(fdm);
    if (ret != 0) {
        vapi_error("unlockpt failed [%s]", strerror(errno));
        close(fdm);
        return -1;
    }

    /* retrieve path of pty */
    ptsname_r(fdm,pts_str,128);

    /* create symlink at desired location */
    ret = remove(path);
    if (ret == 0)
        vapi_debug("remove old pty [%s]", path);

    ret = symlink(pts_str,path);
    if (ret != 0) {
        vapi_error("symlink failed [%s]", strerror(errno));
    }

    /* keep slave fd, otherwise EIO occurs when calamares disconnects */
    vtnd_console_ctx.fd_slave = open(pts_str, O_RDWR);
    vtnd_console_ctx.fd_term = fdm;
    snprintf(vtnd_console_ctx.pty_path, LEN_PATH, "%s", path);

    return 0;
#else
    return -1;
#endif
}


static int vtnd_console_attach_to_loop(int fd)
{
    int ret = -1;
    vloop_event_handle_t dbg_ev = NULL;

    dbg_ev = vloop_add_fd(fd, VLOOP_FD_READ,
            vtnd_console_read_cb, NULL, NULL);
    if (dbg_ev == NULL) {
        printf("vtnd_console: error add dbg fd\n");
        return -1;
    }

    ret = vloop_enable_cb(dbg_ev, VLOOP_FD_READ);
    if (ret != 0) {
        printf("vtnd_console: error enabling dbg cb\n");
        return -1;
    }

    ret = vtnd_req_queue_init(&(vtnd_console_ctx.conn));
    if (ret != 0) {
        printf("vtnd_console: error init queue\n");
        return -1;
    }

    return 0;
}

static int original_flags = -1;

static int vtnd_console_init_termios(int fd)
{
    int ret = 0;
    int flags = 0;
    struct termios new_term_settings;

    if (fd != STDIN_FILENO) {
        /* re-direct stdio,stdout,stderr */
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
    }

    if (fd != STDIN_FILENO) {
        ret = tcgetattr(fd, &new_term_settings);
        if (ret != 0) {
            vapi_error("tcgetattr failed [%s]", strerror(errno));
            return -1;
        }

        /* set raw terminal settings (see manpage) */
        cfmakeraw(&new_term_settings);

        ret = tcsetattr(fd, TCSANOW, &new_term_settings);
        if (ret != 0) {
            vapi_error("tcsetattr failed [%s]", strerror(errno));
            return -1;
        }
    }

    /* discard all buffered input */
    tcflush(STDIN_FILENO, TCIFLUSH);

    /* set tty non-blocking */
    original_flags = fcntl(fd, F_GETFL);
    flags = original_flags | FNDELAY;
    ret = fcntl(fd, F_SETFL, flags);
    if (ret != 0) {
        vapi_error("fcntl failed [%s]", strerror(errno));
        return -1;
    } else {
        return 0;
    }
}

void vtnd_console_reset(void)
{
    if(original_flags != -1)
        (void)fcntl(STDIN_FILENO, F_SETFL, original_flags);
}


/**********************************************************************************************************************/


int vtnd_console_create(const char *pty_path)
{
    int ret = 0;

    memset(&vtnd_console_ctx, 0, sizeof(struct vtnd_console_ctx));

    /* some default values */
    vtnd_console_ctx.fd_term = STDIN_FILENO;
    vtnd_console_ctx.fd_slave = -1;
    vtnd_console_ctx.conn.type = VTND_CONSOLE;
    vtnd_console_ctx.conn.fd_out = STDIN_FILENO;

    vtnd_console_ctx.conn.output_bev = bufferevent_socket_new(vloop_get_base(), vtnd_console_ctx.conn.fd_out, BEV_OPT_CLOSE_ON_FREE);

    if (pty_path != NULL && strlen(pty_path) > 0) {
        /* initialize passed pty as console */
        ret = vtnd_console_create_pty(pty_path);
        if (ret != 0)
            return -1;
    }

    ret = vtnd_console_attach_to_loop(vtnd_console_ctx.fd_term);
    if (ret != 0)
        return -1;

    /* set console config */
    ret = vtnd_console_init_termios(vtnd_console_ctx.fd_term);
    if (ret != 0)
        return -1;

    return 0;
}


int vtnd_console_get_connection(struct tnd_connection **ref)
{
    if (ref == NULL) {
        return -1;
    } else {
        *ref = &(vtnd_console_ctx.conn);
        return 0;
    }
}


int vtnd_console_get_fd(void)
{
    return vtnd_console_ctx.fd_term;
}
