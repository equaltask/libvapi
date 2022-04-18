#ifndef __VLOOP_INTERNAL_H__
#define __VLOOP_INTERNAL_H__

#include <event2/event.h>
#include <libvapi/vloop.h>
#include <libvapi/vlist.h>

#if defined(__cplusplus)
extern "C" {
#endif

//get/set loop's event_base pointer
struct event_base *vloop_get_base(void);
void vloop_init_global_list();
void vloop_set_base(struct event_base *base);
pid_t vloop_get_tid();

int vloop_dbg_init(const char *pty_path, const char *tnd_client_ipd);
struct event_base* vloop_main_init(int argc, char *argv[]);
struct event_base* vloop_init(int max_prio);
void vloop_set_application_name(const char *app_name);
void vloop_set_app_instance_id(const char* app_inst);
void vloop_set_build_info(const char *path_name);
int vloop_execute_cmd_file(const char *cmd_file);
int event_process_loop(struct event_base* base_loop);

#if defined(__cplusplus)
};
#endif
#endif
