#ifndef __VTND_LOG_HDR__
#define __VTND_LOG_HDR__

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vtnd_log_serv_cb)(const char *log_id, int log_level, const char *log_string, int error_record);
typedef void (*vtnd_log_cleanup_cb)(int id);

int vtnd_log_server_start(const char *service_name, vtnd_log_serv_cb recv_cb, vtnd_log_cleanup_cb clean_cb);
int vtnd_log_client_start(const char *service_name);
int vtnd_log_client_is_enabled(void);
int vtnd_log_write(char *output, int level);
int vtnd_log_write_error(char *output, int level);
void vtnd_log_server_cleanup(int id);

#ifdef __cplusplus
}
#endif

#endif
