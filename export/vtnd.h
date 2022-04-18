#ifndef __VTND_H__
#define __VTND_H__

#include <libvapi/vdbg.h>
#include <libvapi/vlist.h>

#define LEN_NAME    64
#define LEN_PROMPT  128
#define LEN_SVC     256
#define LEN_PATH    512
#define LEN_PRINT   1024
#define LEN_PAGE    4096

#ifdef __cplusplus
extern "C"
{
#endif


/****************************************************/
/****************************************************/
struct vtnd_proto {
    void *data;
};

typedef int (*vdbg_raw_cb)(struct vtnd_proto *handle, const char *module, const void *msg, void *ctx);

int vdbg_link_proto(const char *module, vdbg_raw_cb api_cb, vdbg_raw_cb tnd_cb, void *ctx);

/****************************************************/
/****************************************************/


struct vtnd_req;
typedef int (*vtnd_req_cb)(struct vtnd_req *req, int result);

enum vtnd_req_type {
    VTND_TXT = 1,
    VTND_JSON,
    VTND_RAW
};


// XXX IDEA split up here already in module, cmd, args
struct vtnd_cmd_req_txt {
    char cmd[VDBG_MAX_CMD_LEN];     /* cmd string */
    uint32_t id;                    /* optional identifier */
    struct tnd_connection *conn;    /* connection origin of request XXX DELME */
    vtnd_req_cb done_cb;            /* callback to be called after cmd execution */
};


struct vtnd_cmd_req_raw {
    char service[LEN_NAME];         /* dest service */
    char module[LEN_NAME];          /* dest module */
    vtnd_req_cb done_cb;            /* callback to be called after cmd execution */
    int data_len;                   /* encapsulated data length */
    unsigned char *data;            /* encapsulated data */
};


struct vtnd_req {
    struct tnd_connection *conn;                /* connection origin of request */
    //yipc_message_handle_t nr;                   /* yipc reply id */
    bool async;
    enum vtnd_req_type type;                    /* request type identifier */
    union {
        struct vtnd_cmd_req_txt req_txt;        /* handle for type text */
        struct vtnd_cmd_req_raw req_raw;        /* handle for type raw */
    };
};


int vtnd_req_queue_init(struct tnd_connection *conn);
int vtnd_req_enqueue(struct vtnd_req *req);


enum vtnd_src {
    VTND_SRC_UNKOWN = 0,
    VTND_CONSOLE,
    VTND_FILE,
    VTND_SOCK,
    VTND_YIPC,
    VTND_SRC_MAX
};


struct tnd_connection {
    enum vtnd_src type;             /* type of connection */
    int fd_out;                     /* output fd for connection */
    int fd_in;                      /* input fd for connection */
    struct vtnd_module *current;    /* pointer to store current module for connection */
    struct bufferevent *output_bev; /* used for output buffering on local console */

    char *outbuf;                   /* Used to buffering the output when connected */
    uint32_t outbuf_cap;            /* via the vtnd_client. */
    uint32_t outbuf_len;            /* we use a simple string buffer instead of a pipe */
};                                  /* to have a larger capacity available. */


struct vtnd_module {
    char name[LEN_NAME];            /* module name */
    char prompt[LEN_PROMPT];        /* custom prompt */
    int flag_fwd;                   /* do not process special commands */
    int len;                        /* number of registered commands */
    struct vlist node;              /* module node for mod_list in vtnd_ctx */
    struct vlist cmd_list;          /* list head of tnd_cmd's */
    vdbg_help_cb help_proc;         /* 'help' function callback */
    void *data;                     /* generic data pointer */
// -----------------------------------------------------------------------------------
    vdbg_raw_cb raw_cb_tnd;
    vdbg_raw_cb raw_cb_api;
    void *raw_ctx;
// -----------------------------------------------------------------------------------
};


struct tnd_cmd {
    char name[LEN_NAME];            /* command name */
    vdbg_cmd_cb cmd_proc;           /* command function callback */
    vdbg_help_cb help_proc;         /* 'help' function callback */
    struct vlist node;              /* command node for cmd_list in vtnd_module */
    void *data;                     /* generic data pointer */
};


struct vtnd_ctx {
    int len;                        /* number of modules registered */
    struct vlist mod_list;          /* list of modules registered */
    struct vtnd_req *cur_req;       /* current handling tnd request */
    struct vlist proxy_list;        /* list head of proxies */
};


int vtnd_init(void);
int vtnd_get_base(struct vtnd_ctx **ctx);
int vtnd_module_new(const char *name, struct vtnd_module **mod);
int vtnd_module_remove(struct vtnd_module *del);
int vtnd_module_get_by_name(const char *name, struct vtnd_module **mod);
int vtnd_module_set_current(struct tnd_connection *conn, const char *mod_name);
int vtnd_printf_msg_text(struct tnd_connection *conn, const char *txt);
int vtnd_printf_connection(struct tnd_connection *conn, const char *fmt, ...);
int vtnd_print_prompt(struct tnd_connection *conn);

int vtnd_text_req_process(struct vtnd_req *wrapper_req);
int vtnd_text_module_remove_cmds(struct vtnd_module *mod);
int vtnd_text_module_is_dispatch(struct vtnd_module *mod);

int get_substring(const char *src, char *dest, int nr, int dest_len);
int get_args_str(const char *src, char *dest, int from_arg, int dest_len);

int vdbg_is_initialized(void);
int vdbg_unlink_cmd(char *module, char *cmd);

int vtnd_server_init_module(const char *service_name);
int vtnd_server_update_statistics(const char *service_name, const char *client_id, const int error_level);
void vtnd_server_reset_statistics(const char *service_name);
void vtnd_server_print_statistics(const char *server_name);
void vtnd_server_print_total_stats(const char *server_name);
void vtnd_server_log_cleanup(int id);

int vtnd_client_start(const char *service_name, const char *client_id);
int vtnd_client_is_enabled(void);

int vdbg_unlink_module(const char *name);

int delete_all_cpp_callbacks();  // C++ function in vdbg.cpp

/*! \brief Get vtnd debug context.
 *
 * This function returns the vtnd context and put the vtnd in async mode, meaning that
 * all additional input will be suspended until the vtnd_finish_context operation is called.
 * As a last step before leaving the debug function, the context needs to be set to NULL using
 * vtnd_set_context. When the application wants to proceed with the async output, the context
 * retrieved via this operation needs to be set again. Once the command is completed, the
 * vtnd_finish_context operation needs to be called, again passing the same pointer and the
 * result of the command.
 *
 * \return The debug context.
 * \sa vtnd_set_context, vtnd_finish_context
 */
void *vtnd_get_context();

/*! \brief Set vtnd debug context.
 *
 * This function needs to be called to re-enter an earlier debug context of which the pointer
 * was retrieved using the vtnd_get_context operation.
 *
 * \param ctx  IN Debug context to be restored.
 * \sa vtnd_get_context, vtnd_finish_context
 */
void vtnd_set_context(void *ctx);

/*! \brief Finish the async debug context.
 *
 * This function has to be called to finish as earlier started async debug context.
 *
 * \param ctx    IN Debug context to close.
 * \param result IN Final result code of the debug command.
 * \sa vtnd_set_context, vtnd_get_context
 */
void vtnd_finish_context(void *ctx, int result);

#ifdef __cplusplus
}
#endif


#endif
