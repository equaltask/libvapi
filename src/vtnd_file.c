
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <libvapi/vlog.h>
#include <libvapi/vtnd.h>

#include "vlog_vapi.h"

static struct tnd_connection conn_file = {0};

static int vtnd_file_create_tnd_req(struct tnd_connection *conn, char *cmd)
{
    struct vtnd_req new;

    new.type = VTND_TXT;
    snprintf(new.req_txt.cmd, VDBG_MAX_CMD_LEN, "%s", cmd);
    new.req_txt.done_cb = NULL;
    new.req_txt.conn = conn;

    new.conn = conn;

    return vtnd_req_enqueue(&new);
}

static int vtnd_conn_file_init(void)
{
    int ret = -1;

    memset(&conn_file, 0, sizeof(struct tnd_connection));

    conn_file.type = VTND_FILE;
    conn_file.fd_out = STDOUT_FILENO;

    ret = vtnd_req_queue_init(&conn_file);
    if (ret != 0) {
        vapi_error("init queue");
        return -1;
    }

    return 0;
}

int vtnd_file_init(const char *path)
{
    char buffer[VDBG_MAX_CMD_LEN];
    FILE* fp = NULL;

    if (path == NULL || strlen(path) == 0)
        return -1;

    if (conn_file.type == VTND_SRC_UNKOWN)
        vtnd_conn_file_init();

    fp = fopen(path, "r");
    if (fp == NULL) {
        vapi_error("fopen %s [%s]", path, strerror(errno));
        return -1;
    } else {
        setvbuf(fp, NULL, _IONBF, 0);
    }

    while (!feof(fp)) {
        if (fgets(buffer, VDBG_MAX_CMD_LEN-1, fp) != NULL && strlen(buffer) > 1) {
            vtnd_file_create_tnd_req(&conn_file, buffer);
        }
    } /* end while */

    fclose(fp);
    return 0;
}

int vtnd_file_run_cmd(const char *path, const char *mod, int len, const char *filter)
{
    char buffer[VDBG_MAX_CMD_LEN];
    FILE* fp = NULL;

    if (path == NULL || strlen(path) == 0 || mod == NULL || strlen(mod) == 0)
        return -1;

    fp = fopen(path, "r");
    if (fp == NULL) {
        vapi_error("fopen %s [%s]", path, strerror(errno));
        return -1;
    } else {
        setvbuf(fp, NULL, _IONBF, 0);
    }

    while (!feof(fp)) {
        if (fgets(buffer, VDBG_MAX_CMD_LEN-1, fp) != NULL && strlen(buffer) > 1) {
            if ((filter == NULL && !strncmp(buffer, mod, len)) ||
                (filter != NULL && strstr(buffer, filter) && strstr(buffer, mod)))
                vtnd_file_create_tnd_req(&conn_file, buffer);
        }
    } /* end while */

    fclose(fp);
    return 0;
}
