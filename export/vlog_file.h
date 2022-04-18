#ifndef __VLOG_FILE_HDR__
#define __VLOG_FILE_HDR__

#include <stdio.h>
#include <stdint.h>

#include <libvapi/vmutex.h>
#include <libvapi/vlist.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rotate_states {
    rotate_normal = 0,
    rotate_hwm_reached,
    rotate_lwm_reached
};

struct vlog_file_type;

typedef int (*vlog_file_rotate_cb)(struct vlog_file_type *handle);

typedef struct vlog_file_type {
    FILE      *file;
    char      pathname[VLOG_MAX_FILENAME];
    char      filename[VLOG_MAX_FILENAME];
    int       force_rotate;
    int       nb_logfiles;
    int       nb_logfiles_max;
    int       maxentries;
    int       currententries;
    int       maxtime;
    int       currenttime;
    int       flushtime;
    int       lastflushtime;
    int       fs_size_hwm;
    int       fs_size_lwm;
    enum rotate_states rstate;
    vthread_mutex_t mutex;
    vlog_file_rotate_cb cb;
    struct vlist filelist;
    void      *data;
} vlog_file_type_t;

int vlog_file_open(vlog_file_type_t *handle);
int vlog_file_flush(vlog_file_type_t *handle, time_t now);
int vlog_file_write(vlog_file_type_t *handle, char *line);
int vlog_file_close(vlog_file_type_t *handle);
int vlog_file_get_fd(vlog_file_type_t *handle);
int vlog_file_cleanup(vlog_file_type_t *handle);

#ifdef __cplusplus
}
#endif

#endif
