
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <libvapi/vloop.h>
#include <libvapi/vtnd_log.h>

#include "vlog_core.h"
#include "vlog_vapi.h"
#include "verror.h"


#define VTND_LOG_MSG_DELIM      '#'
#define VTND_LOG_MSG_LEN_MAX    2048
#define VTND_LOG_SOCK_TRACES    "@tndd.traces"


static struct vtnd_log_client {
    int initialized;
    int sock;
    char *id;
    struct sockaddr_un tndd_un_name;
    vloop_event_handle_t *wr_handle;
} vtnd_log_client = {0};


static struct vtnd_log_server {
    int sock;
    vtnd_log_serv_cb recv_cb;
    vtnd_log_cleanup_cb clean_cb;
} vtnd_log_server = {0};


// TODO convert to generic internal vlog error handling mechanism for log infrastructure
/* offload generating error records to prevent recursion in vtnd_log_write */
#define vtnd_log_file_error_printf(fmt, ...) \
     __vtnd_log_file_error_printf(__FILE__, __LINE__, fmt, ##__VA_ARGS__)


static void __vtnd_log_file_error_printf(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    char error_record[VERROR_MAX_RECORD_SIZE];

    va_start(args, fmt);
    verror_vstr(error_record, VLOG_ERROR, 0, "vtnd_log", file, line, 1, fmt, args);
    va_end(args);

    vlog_print_error_asyncsignalsafe(error_record);
}


static int vtnd_log_server_read_cb(int fd, vloop_event_handle_t event_handle, void* ctx)
{
    int len;
    char buffer[VTND_LOG_MSG_LEN_MAX];
    char *log_msg_id = NULL;
    char *log_msg_string = NULL;
    int log_msg_level = -1;
    int log_msg_error_record = 0;
    void *tmp;
    void *next;

    while (1) {
        len = recv(fd, buffer, VTND_LOG_MSG_LEN_MAX, 0);
        if (len <= 0) {
            if (errno != EAGAIN)
                vtnd_log_file_error_printf("vtnd_log error recv msg, ret [%d] [%s]", len, strerror(errno));
            break;
        }

        buffer[len] = '\0';

        /* get log id */
        next= buffer;
        tmp = memchr(next, VTND_LOG_MSG_DELIM, 100);
        if (tmp != NULL) {
            *((char *)tmp) = '\0';
            log_msg_id = next;
            next = tmp + 1;
        } else {
            goto server_read_cb_error_parse;
        }

        /* get log level */
        tmp = memchr(next, VTND_LOG_MSG_DELIM, 100);
        if (tmp != NULL) {
            *((char *)tmp) = '\0';
            log_msg_level = atoi(next);
            next = tmp + 1;
        } else {
            goto server_read_cb_error_parse;
        }

        /* get log error record */
        tmp = memchr(next, VTND_LOG_MSG_DELIM, 100);
        if (tmp != NULL) {
            *((char *)tmp) = '\0';
            log_msg_error_record = atoi(next);
            next = tmp + 1;
        } else {
            goto server_read_cb_error_parse;
        }

        /* get log error record */
        log_msg_string = next;

        vtnd_log_server.recv_cb(log_msg_id, log_msg_level, log_msg_string, log_msg_error_record);
    } /* end while */

    return 0;

server_read_cb_error_parse:
    vtnd_log_file_error_printf("vtnd_log error parse msg");
    return -1;
}


int vtnd_log_server_start(const char *service_name, vtnd_log_serv_cb recv_cb, vtnd_log_cleanup_cb clean_cb)
{
    int ret = 0;
    int sock = -1;
    int name_len = 0;
    struct sockaddr_un name = {0};
    vloop_event_handle_t tnd_log_ev = NULL;

    // XXX currently use static name VTND_LOG_SOCK_TRACES
    (void)(service_name);

    if (recv_cb == NULL) {
        vapi_error("vtnd_log error param cb [NULL]");
        return -1;
    }

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        vapi_error("vtnd_log error get socket");
        return -1;
    }

    strcpy(name.sun_path, VTND_LOG_SOCK_TRACES);
    name.sun_family = AF_UNIX;
    name.sun_path[0] = '\0';

    name_len = sizeof(sa_family_t) + strlen(VTND_LOG_SOCK_TRACES);

    ret = bind(sock, (struct sockaddr *) &name, name_len);
    if (ret != 0) {
        vapi_error("vtnd_log error bind socket [%s] [%s]", (name.sun_path + 1), strerror(errno));
        close(sock);
        return -1;
    }

    ret = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (ret < 0) {
        vapi_error("error fcntl [%s]", strerror(errno));
        close(sock);
        return -1;
    }

    tnd_log_ev = vloop_add_fd(sock, VLOOP_FD_READ, vtnd_log_server_read_cb, NULL, NULL);
    if (tnd_log_ev == NULL) {
        vapi_error("vtnd_log error register tnd_log_ev");
        close(sock);
        return -1;
    }

    ret = vloop_enable_cb(tnd_log_ev, VLOOP_FD_READ);
    if (ret != 0) {
        vapi_error("vtnd_log error enable tnd_log_ev");
        vloop_remove_fd(tnd_log_ev);
        close(sock);
        return -1;
    }

    vtnd_log_server.sock = sock;
    vtnd_log_server.recv_cb = recv_cb;
    vtnd_log_server.clean_cb = clean_cb;

    return 0;
}


/**********************************************************************************************************************/

int vtnd_log_client_start(const char *service_name)
{
    int ret = 0;
    int sock = -1;

    // XXX currently use static name VTND_LOG_SOCK_TRACES
    (void)(service_name);

    vtnd_log_client.id = (char *) vloop_get_application_name();

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        vapi_error("vtnd_log error get socket [%s]", strerror(errno));
        return -1;
    }

    ret = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (ret < 0) {
        vapi_error("error fcntl [%s]", strerror(errno));
        close(sock);
        return -1;
    }

    strcpy(vtnd_log_client.tndd_un_name.sun_path, VTND_LOG_SOCK_TRACES);
    vtnd_log_client.tndd_un_name.sun_family = AF_UNIX;
    vtnd_log_client.tndd_un_name.sun_path[0] = '\0';

    vlog_output_set_status(VLOG_TNDD_INDEX, 1);
    ret = vlog_output_set_loglevel(VLOG_TNDD_INDEX, VLOG_ERROR);
    if (ret < 0)
        vapi_error("warning: could not set tnd log output level");
    vtnd_log_client.sock = sock;

    /* enable writing */
    vtnd_log_client.initialized = 1;

    return 0;
}


static int __vtnd_log_write(char *output, int level, int error)
{
    int ret = 0;
    int name_len = 0;
    int errno_bak = 0;
    char buffer[VTND_LOG_MSG_LEN_MAX] = "";

    if (vtnd_log_client.initialized == 0)
        return 0;

    ret = snprintf(buffer, VTND_LOG_MSG_LEN_MAX, "%s#%d#%d#%s", vtnd_log_client.id, level, error, output);
    if (ret < 0)
        return -1;

    name_len = sizeof(sa_family_t) + strlen(VTND_LOG_SOCK_TRACES);

    ret = sendto(vtnd_log_client.sock, buffer, strlen(buffer), MSG_DONTWAIT,
            (struct sockaddr *) &(vtnd_log_client.tndd_un_name), name_len);
    errno_bak = errno;

    if (ret < 0 && errno_bak != EAGAIN && errno_bak != ECONNREFUSED) {
        vapi_debug("vtnd_log error sendto [%s]\n", strerror(errno));
        ret = -1;
    } else if (ret < 0 && errno_bak == ECONNREFUSED) {
        /* XXX ignore */
        ret = 0;
    } else if (ret < 0 && errno_bak == EAGAIN) {
        vapi_debug("vtnd_log error sendto [EAGAIN]");
        ret = 0;
    }

    return ret;
}

int vtnd_log_write(char *output, int level)
{
    return __vtnd_log_write(output, level, 0);
}

int vtnd_log_write_error(char *output, int level)
{
    return __vtnd_log_write(output, level, 1);
}

int vtnd_log_client_is_enabled(void)
{
    return ((vtnd_log_client.initialized) ? 1 : 0);
}

void vtnd_log_server_cleanup(int id)
{
    if (vtnd_log_server.clean_cb != NULL) {
        if (id == VLOG_LOGFILE_INDEX) {
            vtnd_log_server.clean_cb(0);
        } else if (id == VLOG_ERRORFILE_INDEX) {
            vtnd_log_server.clean_cb(1);
        }
    }
}
