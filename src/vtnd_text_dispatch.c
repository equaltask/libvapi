
#include <string.h>
#include <errno.h>

#include <libvapi/vtypes.h>
#include <libvapi/vlist.h>
#include <libvapi/vdbg.h>
#include <libvapi/vtnd.h>

#include "vtnd_text_dispatch.h"
#include "vlog_vapi.h"


static int insert_tnd_cmd_dispatch(struct vtnd_module *mod, struct tnd_cmd_dispatch *new)
{
    if (mod->len != VTND_TEXT_DISPATCH)
        return -1;

    vlist_init(&(new->node));
    vlist_add_tail(&(mod->cmd_list),&(new->node));

    return 0;
}


static int module_cmd_dispatch_execute(struct vtnd_module *mod, char *input, int args, char *cmd_name)
{
    int ret = 0;
    struct tnd_cmd_dispatch *tmp = NULL;
    struct vlist *nodep = NULL;

    vlist_foreach(&(mod->cmd_list),nodep) {
        tmp = container_of(struct tnd_cmd_dispatch,node,nodep);
        ret = tmp->disp_proc(input, args, cmd_name, tmp->data);
        if (ret > 0)
            break;
    }

    return ret;
}


int vtnd_text_module_cmd_dispatch_help(struct vtnd_module *mod)
{
    struct tnd_cmd_dispatch *tmp = NULL;
    struct vlist *nodep = NULL;

    vlist_foreach(&(mod->cmd_list),nodep) {
        tmp = container_of(struct tnd_cmd_dispatch,node,nodep);
        tmp->help_proc(tmp->data);
    }

    return 0;
}


#define DISPATCH_LEN_NAME   128


int vtnd_text_forward_module_cmd_dispatch(struct vtnd_module *mod, const char *input)
{
    int args = 0;
    char cmd_name[DISPATCH_LEN_NAME] = "";
    char arg1[DISPATCH_LEN_NAME] = "";
    char arg2[DISPATCH_LEN_NAME] = "";

    if (mod->len != VTND_TEXT_DISPATCH)
        return -1;

    get_substring(input,arg1,1,DISPATCH_LEN_NAME);
    get_substring(input,arg2,2,DISPATCH_LEN_NAME);

    /* check if first part is module name */
    if (strcmp(arg1,mod->name) == 0) {
        /* case: <module> <cmd> [args] */
        strcpy(cmd_name,arg2);
        args = 2;
    } else {
        /* case: <cmd> [args] */
        strcpy(cmd_name,arg1);
        args = 1;
    }

    if (strcmp(cmd_name,"help") == 0 && mod->flag_fwd == 0)
        return vtnd_text_module_cmd_dispatch_help(mod);
    else
        return module_cmd_dispatch_execute(mod,(char *)input,args,cmd_name);
}


int vdbg_link_module_dispatch(char *module, vdbg_help_cb help_proc, vdbg_text_dispatch_cb dispatch_proc, void *data)
{
    int ret = -1;
    struct vtnd_module *tmp = NULL;
    struct vtnd_ctx *local_tnd_ctx = NULL;
    struct tnd_cmd_dispatch *new = NULL;

    ret = vtnd_get_base(&local_tnd_ctx);

    /* sanity checks */
    if (ret != 0 || local_tnd_ctx == NULL) {
        vapi_error("vtnd not initialized");
        ret = -1;
        goto exit_link_module_dispatch;
    } else if (module == NULL || strlen(module) == 0) {
        vapi_error("param [module]");
        ret = -1;
        goto exit_link_module_dispatch;
    } else if (dispatch_proc == NULL) {
        vapi_error("param [dispatch_proc]");
        ret = -1;
        goto exit_link_module_dispatch;
    }

    /* get module pointer */
    ret = vtnd_module_get_by_name(module, &tmp);
    if (ret != 0) {
        /* create module if non existing */
        ret = vdbg_link_module(module, NULL, data);
        if (ret != 0)
            goto exit_link_module_dispatch;
        vtnd_module_get_by_name(module, &tmp);
        tmp->len = VTND_TEXT_DISPATCH;
        tmp->help_proc = NULL;
        tmp->data = data;
    } else if (tmp->len != VTND_TEXT_DISPATCH) {
        vapi_error("module [%s] not found", module);
        ret = -1;
        goto exit_link_module_dispatch;
    }

    /* create new cmd object */
    new = calloc(sizeof(struct tnd_cmd_dispatch),1);
    if (new == NULL) {
        vapi_error("malloc [%s]",strerror(errno));
        ret = -1;
    } else {
        new->help_proc = help_proc;
        new->disp_proc = dispatch_proc;
        new->data = data;
        insert_tnd_cmd_dispatch(tmp,new);
        ret = 0;
    }

exit_link_module_dispatch:
    return ret;
}
