
#ifndef __VTND_TEXT_DISPATCH_H__
#define __VTND_TEXT_DISPATCH_H__

#include <libvapi/vtnd.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define VTND_TEXT_DISPATCH   -1

typedef int (*vdbg_text_dispatch_cb)(char *input, int args, char *cmd, void *data);

struct tnd_cmd_dispatch {
    vdbg_help_cb help_proc;         /* 'help' function callback */
    vdbg_text_dispatch_cb disp_proc;/* 'dispatch' function callback */
    void *data;                     /* generic data pointer */
    struct vlist node;              /* command node for cmd_list in vtnd_module */
};

/* internal use for vtnd */
int vtnd_text_forward_module_cmd_dispatch(struct vtnd_module *mod, const char *input);
int vtnd_text_module_cmd_dispatch_help(struct vtnd_module *mod);

/* public function */
int vdbg_link_module_dispatch(char *module, vdbg_help_cb help_proc, vdbg_text_dispatch_cb dispatch_proc, void *data);

#ifdef __cplusplus
}
#endif

#endif
