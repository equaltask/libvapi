
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <libvapi/vlist.h>
#include <libvapi/vtnd.h>

#include "vtnd_text_dispatch.h"
#include "vlog_vapi.h"


static void main_help(void *ctx)
{
    vdbg_printf("vdbg help info\n");
    vdbg_printf("--------------\n");
    vdbg_printf("help                   Show this help\n");
    vdbg_printf("list                   List all available modules\n");
    vdbg_printf("<module> help          Show help of module\n");
    vdbg_printf("<module> list          List all commands of module\n");
    vdbg_printf("<module> <cmd> help    Show help of command in module\n");
    vdbg_printf("<module> <cmd> [args]  Execute command in module\n");
    vdbg_printf("<module>               Go into module prompt (help and list are also available here)\n");
    vdbg_printf("exit                   Exit module prompt\n");
}


/* get substring "nr" string (space, new line, tab or carriage return separated) */
int get_substring(const char *src, char *dest, int nr, int dest_len)
{
    int i = 1;
    int len = 0;
    int ret = 0;
    char *saveptr = NULL;
    char *tmp = NULL;
    char *copy = NULL;
    const char *delim = " \n\t\r";

    strcpy(dest,"");
    copy = strdup(src);

    tmp = strtok_r(copy,delim,&saveptr);
    while (tmp != NULL && i < nr) {
        tmp = strtok_r(NULL,delim,&saveptr);
        if (tmp != NULL && !isspace(*tmp))
            i++;
    } /* end while */

    if (tmp == NULL || (len = strlen(tmp)) >= dest_len) {
        ret = -1;
    } else {
        strncpy(dest,tmp,dest_len);
        ret = len;
    }

    free(copy);
    return ret;
}


/* strip of first "from_arg" arguments (space separated) */
int get_args_str(const char *src, char *dest, int from_arg, int dest_len)
{
    int i = 0;
    int len = 0;
    int ret = 0;
    char *tmp = NULL;
    char prev = 0;

    strcpy(dest,"");
    tmp = (char *) src;

    /* strip leading spaces */
    while (isspace(*tmp))
        tmp++;

    /* get to 'from_arg' parameter */
    for (i = 0; *tmp != '\0' && i < from_arg; tmp++) {
        if (isspace(*tmp) && !isspace(prev))
            i++;
        prev = *tmp;
    }

    if (*tmp == '\0') {
        ret = 0;
    } else if ((len = strlen(tmp)) >= dest_len) {
        ret = -1;
    } else {
        strncpy(dest,tmp,dest_len);
        ret = len;
    }

    return ret;
}


static int get_cmd_by_name(struct vtnd_module *mod, const char *name, struct tnd_cmd **cmd)
{
    int ret = -1;
    struct tnd_cmd *tmp = NULL;
    struct vlist *nodep = NULL;

    if (cmd != NULL)
        *cmd = NULL;

    vlist_foreach(&(mod->cmd_list), nodep) {
        tmp = container_of(struct tnd_cmd, node, nodep);
        if (strcmp(name, tmp->name) == 0) {
            ret = 0;
            if (cmd != NULL)
                *cmd = tmp;
            break;
        }
    }

    return ret;
}


static int insert_tnd_cmd(struct vtnd_module *mod, struct tnd_cmd *new)
{
    vlist_init(&(new->node));
    vlist_add_tail(&(mod->cmd_list), &(new->node));
    mod->len++;
    return 0;
}


static int delete_tnd_cmd(struct vtnd_module *mod, char *cmd)
{
    int ret = 0;
    struct tnd_cmd *del = NULL;

    /* check if cmd already registered */
    ret = get_cmd_by_name(mod,cmd,&del);
    if (ret != 0) {
        vapi_error("%s >> error cmd [%s] not found",mod->name,cmd);
        return -1;
    } else {
        vlist_delete(&(del->node));
        free(del);
        mod->len--;
        return 0;
    }
}


static void list_all_modules(void)
{
    int rc = -1;
    struct vlist *node = NULL;
    struct vtnd_module *tmp = NULL;
    struct vtnd_ctx *tnd_base = NULL;

    rc = vtnd_get_base(&tnd_base);
    if (rc != 0) {
        vapi_error("vtnd not initialized");
        return;
    }

    vdbg_printf("list registered modules:\n");

    vlist_foreach(&(tnd_base->mod_list), node) {
        tmp = container_of(struct vtnd_module, node, node);
        vdbg_printf("%s\n", tmp->name);
    } /* end foreach */
}


static void list_all_cmds(const char *mod_str)
{
    int ret = 0;
    struct vlist *node = NULL;
    struct tnd_cmd *tmp = NULL;
    struct vtnd_module *mod = NULL;

    ret = vtnd_module_get_by_name(mod_str, &mod);
    if (ret != 0 || mod == NULL)
        return;

    if (mod->len == VTND_TEXT_DISPATCH) {
        vtnd_text_forward_module_cmd_dispatch(mod, "list");
        return;
    }

    vdbg_printf("list registered commands for module [%s]:\n", mod->name);

    vlist_foreach(&(mod->cmd_list), node) {
        tmp = container_of(struct tnd_cmd, node, node);
        vdbg_printf("%s\n", tmp->name);
    } /* end foreach */
}


static void execute_cmd_list(struct vtnd_cmd_req_txt *req)
{
    char *cmd = NULL;
    char arg1[LEN_NAME] = "";
    char arg2[LEN_NAME] = "";

    cmd = req->cmd;
    get_substring(cmd,arg1,1,LEN_NAME);
    get_substring(cmd,arg2,2,LEN_NAME);

    if (strcmp(arg1,"list") == 0) {
        /* make list of current state (module set or not) */
        if (req->conn->current == NULL) {
            /* list all modules */
            list_all_modules();
        } else {
            /* list all cmds of current module*/
            list_all_cmds(req->conn->current->name);
        }
    } else if (strcmp(arg2, "list") == 0) {
        /* do list of arg1 */
        list_all_cmds(arg1);
    }
}


static void execute_cmd_help(struct vtnd_module *mod)
{
    if (mod->len != VTND_TEXT_DISPATCH) {
        if (mod->help_proc != NULL)
            mod->help_proc(mod->data);
    } else {
        vtnd_text_module_cmd_dispatch_help(mod);
    }
}


/* check first part of incoming command on special case */
static int cmd_parse_special(struct vtnd_cmd_req_txt *req)
{
    char *cmd = NULL;
    char arg1[LEN_NAME] = "";

    if (req->conn->current != NULL && req->conn->current->flag_fwd > 0)
        return 1;

    cmd = req->cmd;
    get_substring(cmd,arg1,1,LEN_NAME);

    if (strcmp(arg1,"help") == 0) {
        /* print help of current module if set */
        if (req->conn->current != NULL)
            execute_cmd_help(req->conn->current);
        else
            main_help(NULL);
    } else if (strcmp(arg1,"exit") == 0) {
        /* exit current module environment if set */
        if (req->conn->current != NULL) {
            vdbg_printf("exit module [%s]\n", req->conn->current->name);
            req->conn->current = NULL;
        }
    } else if (strcmp(arg1, "list") == 0) {
        /* re-direct list command */
        execute_cmd_list(req);
    } else {
        return -1;
    }

    return 0;
}


/* execute command in module */
static int execute_module_cmd(struct vtnd_cmd_req_txt *req)
{
    int result = 0;
    char *cmd = NULL;
    char cmd_name[LEN_NAME] = "";
    char cmd_args[VDBG_MAX_CMD_LEN] = "";
    char arg1[LEN_NAME] = "";
    char arg2[LEN_NAME] = "";
    char arg3[LEN_NAME] = "";
    struct tnd_cmd *tmp = NULL;
    struct vtnd_module *mod = NULL;

    cmd = req->cmd;
    mod = req->conn->current;
    get_substring(cmd, arg1, 1, LEN_NAME);
    get_substring(cmd, arg2, 2, LEN_NAME);

    if (mod->len == VTND_TEXT_DISPATCH)
        return vtnd_text_forward_module_cmd_dispatch(mod, cmd);

    /* check if first part is module name */
    if (strcmp(arg1, mod->name) == 0) {
        /* case: <module> <cmd> [args] */
        strcpy(cmd_name, arg2);
        get_args_str(cmd, cmd_args, 2, VDBG_MAX_CMD_LEN);
        get_substring(cmd, arg3, 3, LEN_NAME);
    } else {
        /* case: <cmd> [args] */
        strcpy(cmd_name, arg1);
        get_args_str(cmd, cmd_args, 1, VDBG_MAX_CMD_LEN);
        get_substring(cmd, arg3, 2, LEN_NAME);
    }

    /* check if command is special case */
    if (strcmp(cmd_name, "help") == 0) {
        if (mod->help_proc != NULL)
            mod->help_proc(mod->data);
        return 0;
    } else if (strcmp(cmd_name, "list") == 0) {
        list_all_cmds(mod->name);
        return 0;
    }

    get_cmd_by_name(mod, cmd_name, &tmp);
    if (tmp != NULL) {
        /* check if help for command, otherwise execute */
        if (strcmp(arg3, "help") == 0)
            tmp->help_proc(tmp->data);
        else
            result = tmp->cmd_proc(cmd_name, cmd_args, tmp->data);
    } else {
        vdbg_printf("%s >> error: command [%s] not registered\n", mod->name, cmd_name);
        result = -1;
    }

    return result;
}


/******************************************************************************/


int vtnd_text_module_remove_cmds(struct vtnd_module *mod)
{
    struct tnd_cmd *tmp = NULL;
    struct vlist *nodep = NULL;

    vlist_foreach(&(mod->cmd_list), nodep) {
        tmp = container_of(struct tnd_cmd, node, nodep);
        vlist_delete(nodep);
        mod->len--;
        free(tmp);
    }

    return 0;
}


/******************************************************************************/


int vtnd_text_module_is_dispatch(struct vtnd_module *mod)
{
    if (mod->len == VTND_TEXT_DISPATCH)
        return 1;
    else
        return 0;
}


int vtnd_text_req_process(struct vtnd_req *wrapper_req)
{
    int ret = 0;
    int result = 0;
    char *cmd = NULL;
    char arg1[LEN_NAME] = "";
    char arg2[LEN_NAME] = "";
    struct vtnd_module *mod = NULL;
    struct vtnd_cmd_req_txt *req = NULL;

    if (wrapper_req == NULL || wrapper_req->type != VTND_TXT) {
        vapi_error("param wrapper request");
        ret = -1;
        goto exit_tnd_req_process;
    } else {
        req = &(wrapper_req->req_txt);
    }

    /* sanity checks */
    if (req == NULL || strlen(req->cmd) == 0) {
        vapi_error("param cmd");
        ret = -1;
        goto exit_tnd_req_process;
    }

    cmd = req->cmd;

    /* get first argument, cannot be empty */
    ret = get_substring(cmd,arg1,1,LEN_NAME);
    if (ret <= 0) {
        vapi_error("getting first arg of cmd [%s]", cmd);
        ret = -1;
        goto exit_tnd_req_process;
    } else {
        get_substring(cmd,arg2,2,LEN_NAME);
    }

    /* first check top level cmds */
    ret = cmd_parse_special(req);
    if (ret == 0)
        goto exit_tnd_req_process;

    /* current module set, search for cmd from arg1 */
    if (req->conn->current != NULL) {
        result = execute_module_cmd(req);
        ret = 0;
        goto exit_tnd_req_process;
    }

    /* no current module set, first get module */
    ret = vtnd_module_get_by_name(arg1,&mod);
    if (ret < 0) {
        vdbg_printf("error: module [%s] not found\n",arg1);
        ret = -1;
        goto exit_tnd_req_process;
    }

    if (strlen(arg2) == 0) {
        /* if no other args, set module and exit */
        req->conn->current = mod;
    } else {
        /* forward cmd execution */
        req->conn->current = mod;
        result = execute_module_cmd(req);
        req->conn->current = NULL;
    }

    ret = 0;

exit_tnd_req_process:
    if ((wrapper_req && (wrapper_req->async == false)) && (req != NULL) && (req->done_cb != NULL)) {
        req->done_cb(wrapper_req, result);
    }
    return ret;
}


/******************************************************************************/

static int is_valid_name(const char *text)
{
    if (text == NULL || *text == '\0' || strnlen(text, LEN_NAME) == LEN_NAME)
        return 0;

    do {
        if (isspace(*text))
            return 0;
        ++text;
    } while (*text != '\0');

    return 1;
}

int vdbg_link_module(const char *name, vdbg_help_cb help_proc, void *data)
{
    int ret = 0;
    struct vtnd_module *new = NULL;

    /* sanity checks */
    if (!is_valid_name(name)) {
        vapi_error("param name");
        ret = -1;
        goto exit_link_module;
    }

    ret = vtnd_module_get_by_name(name, &new);
    if (ret != 0) {
        /* create new module object first */
        ret = vtnd_module_new(name, &new);
        if (ret != 0) {
            ret = -1;
            goto exit_link_module;
        }
    }

    new->help_proc = help_proc;
    new->data = data;
    ret = 0;

exit_link_module:
    return ret;
}


int vdbg_link_cmd(const char *module, const char *cmd, vdbg_help_cb help_proc, vdbg_cmd_cb cmd_proc, void *data)
{
    int ret = 0;
    struct vtnd_module *mod = NULL;
    struct tnd_cmd *new = NULL;

    /* sanity checks */
    if (!is_valid_name(module)) {
        vapi_error("no param module");
        ret = -1;
        goto exit_link_cmd;
    } else if (!is_valid_name(cmd)) {
        vapi_error("no param cmd");
        ret = -1;
        goto exit_link_cmd;
    } else if (help_proc == NULL) {
        vapi_error("no param help_proc");
        ret = -1;
        goto exit_link_cmd;
    } else if (cmd_proc == NULL) {
        vapi_error("no param cmd_proc");
        ret = -1;
        goto exit_link_cmd;
    }

    /* check if module already registered and get module pointer */
    ret = vtnd_module_get_by_name(module,&mod);
    if (ret != 0) {
        vapi_error("get module [%s]", module);
        ret = -1;
        goto exit_link_cmd;
    }

    /* check if cmd already registered */
    ret = get_cmd_by_name(mod,cmd,NULL);
    if (ret == 0) {
        vapi_warning("module [%s] cmd [%s] already registered", mod->name, cmd);
        ret = -1;
        goto exit_link_cmd;
    }

    /* create new cmd object */
    new = calloc(sizeof(struct tnd_cmd),1);
    if (new == NULL) {
        vapi_error("malloc [%s]", strerror(errno));
        ret = -1;
    } else {
        strncpy(new->name,cmd,LEN_NAME-1);
        new->help_proc = help_proc;
        new->cmd_proc = cmd_proc;
        new->data = data;
        insert_tnd_cmd(mod,new);
        ret = 0;
    }

exit_link_cmd:
    return ret;
}


int vdbg_unlink_cmd(char *module, char *cmd)
{
    int ret = 0;
    struct vtnd_module *mod = NULL;

    /* sanity checks */
    if (!is_valid_name(module)) {
        vapi_error("param module");
        ret = -1;
        goto exit_unlink_cmd;
    } else if (!is_valid_name(cmd)) {
        vapi_error("param cmd");
        ret = -1;
        goto exit_unlink_cmd;
    }

    /* check if module already registered and get module pointer */
    ret = vtnd_module_get_by_name(module,&mod);
    if (ret != 0) {
        vapi_error("get module [%s]", module);
        ret = -1;
        goto exit_unlink_cmd;
    }

    /* delete command from module */
    ret = delete_tnd_cmd(mod, cmd);

exit_unlink_cmd:
    return ret;
}


int vdbg_unlink_module(const char *name)
{
    struct vtnd_module *mod = NULL;
    int ret = vtnd_module_get_by_name(name, &mod);
    if (ret != 0) {
        vapi_error("get module [%s]", name);
        return -1;
    }

    vtnd_text_module_remove_cmds(mod);
    vtnd_module_remove(mod);
    delete_all_cpp_callbacks();
    return 0;
}
