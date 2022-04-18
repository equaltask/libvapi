#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <libvapi/vmutex.h>
//#include <libvapi/ysocket.h>
#include <libvapi/vfs.h>

#include "vlog_syslog.h"
#include "vlog_vapi.h"
#include "vlog_core.h"

// global variable, that becomes set to  1 when central syslog server is listening, it prevents sendig messages that will be thrown away
static int g_syslog_enabled = 0;

static void on_log_watch_event_cb(vevent_reason_t reason, uint32_t event_mask, const char *filename, void *cookie)
{
    if (reason != VEVENT_OCCURED) return;

    if (event_mask & VFS_EVENT_DELETE) {
        g_syslog_enabled = 0;
    } else if (event_mask & VFS_EVENT_CREATE) {
        g_syslog_enabled = 1;
    } else {
        vapi_error("getting unknown event %i ", event_mask)
    }
}

static int vlog_syslog_open(vlog_syslog_type_t *handle)
{
    int ret = 0;
    int fd = -1;

    fd = ysocket_opendgram_client_fd(AF_INET);

    ret = ysocket_bind_client_fd(fd, handle->local_addr);
    if (ret < 0) {
        vapi_error("error binding client socket to addr %s", handle->local_addr);
        return -1;
    }

    int sndbuf = 256 * 1024;
    ret = ysocket_setsockopt_fd(fd, SOL_SOCKET, SO_SNDBUF, (void *)&sndbuf, sizeof(sndbuf));
    if (ret < 0) {
        vapi_warning("setsockopt error [%s]", strerror(errno));
    }

    (void)vmutex_lock(&(handle->mutex));
    handle->fd = fd;
    (void)vmutex_unlock(&(handle->mutex));

    return 0;
}

void create_log_watch()
{
    //create directory -if it would not exist- so we can install watch on it
    if (mkdir("/var/run/syslog_state", 0755) != 0 && (errno != EEXIST)) {
        vapi_error("error creating /var/run/syslog_state");
        g_syslog_enabled = 1;
    }
    // install watch on the directory
    else if (!vfs_monitor("/var/run/syslog_state", VFS_EVENT_CREATE | VFS_EVENT_DELETE, on_log_watch_event_cb, NULL))  {
        vapi_error("error opening yfs_monitor for /var/run/syslog_state");
        g_syslog_enabled = 1;
    }
    // read the file that is created/deleted, this code to catch case that file exist before being watched
    FILE *fin = fopen("/var/run/syslog_state/enable", "r");
    if (fin) {
        g_syslog_enabled = 1;
        fclose(fin);
    }
}

int vlog_syslog_init(vlog_syslog_type_t *handle)
{
    int ret = 0;

    if (handle == NULL)
        return -1;

    if (handle->fd != 0) {
        vapi_error("vlog_syslog_open handle possibly in use, fd != 0 [%d]", handle->fd);
        return -1;
    }

    ret = vmutex_create(&(handle->mutex));
    if (ret != 0) {
        vapi_error("vlog_syslog_open error create mutex [%s]", strerror(errno));
        return -1;
    }

    create_log_watch();

    return vlog_syslog_open(handle);
}

int vlog_syslog_close(vlog_syslog_type_t *handle)
{
    if (handle == NULL)
        return -1;

    (void)vmutex_lock(&(handle->mutex));
    if (handle->fd > 0) {
        close(handle->fd);
        handle->fd = -1;
    }
    (void)vmutex_unlock(&(handle->mutex));

    return 0;
}

int vlog_syslog_print(vlog_syslog_type_t *handle, int syslog_pri, const char *msg)
{
    // prevent sending messages when syslog relay is not listening.
    if (! g_syslog_enabled) return 0;

    size_t nleft, len_buffer;
    int len, nw;
    char buffer[VLOG_MAX_SYSLOG_MSG_SIZE] = {0};
    char *pdata;
    static const char *trunc = "<TRUNC>";

    if (handle == NULL || handle->fd <= 0 || msg == NULL)
        return -1;

    len = strlen(msg);
    if (msg[len] == '\n')
        len--;

    /* the syslog rfc requires the priority to be a combination of:
     *      * facility code (5 bits)
     *      * severity code (3 bits) (aka log level)
     */
    if (len > 0) {
        int tmp_len = snprintf(buffer, VLOG_MAX_SYSLOG_MSG_SIZE, "<%d>%s", syslog_pri, msg);
        if (tmp_len < 0) {
            return -1;
        } else {
            len_buffer = tmp_len;
        }

        if (len > VLOG_MAX_SYSLOG_MSG_SIZE - 1) {
            sprintf(&buffer[VLOG_MAX_SYSLOG_MSG_SIZE - strlen(trunc) - 1], "%s", trunc);
            len_buffer = VLOG_MAX_SYSLOG_MSG_SIZE;
        }

        /* send down socket */
        nleft = len_buffer;
        pdata = buffer;

        while (nleft > 0) {
            len_buffer = nleft;

            do {
                nw = ysocket_sendto_fd(handle->fd, pdata, &len_buffer, handle->local_addr, handle->local_port);
            } while ((nw < 0) && (errno == EINTR));

            if (nw < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return 0;
                } else {
                    vlog_syslog_close(handle);
                    return vlog_syslog_open(handle);
                }
            }

            nleft -= len_buffer;
            pdata += len_buffer;
        }
    }

    return 0;
}
