#include <libvapi/vdbg.h>
#include <libvapi/vtnd.h>
#include <vector>
#include <string>

/**
 * \note Of type help_cb_t
 */
void helpLinkModuleCallback(void *ctx)
{
    auto *help_cb = static_cast<helpCallback_t *>(ctx);

    if (!ctx) return;

    (*help_cb)();
}

struct Callbacks {
    helpCallback_t helpCallback;
    commandCallback_t cmdCallback;
};

/**
 * \note Of type cmd_cb_t
 */
int linkCommandCallback(char *cmd, char *args, void *ctx)
{
    if (!ctx) return VAPI_FAILURE;

    Callbacks *callbacks = static_cast<Callbacks *>(ctx);

    auto cmd_cb = callbacks->cmdCallback;
    return cmd_cb(cmd, args);
}

/**
 * \note Of type ydbg_help_cb
 */
void helpLinkCommandCallback(void *ctx)
{
    if (!ctx) return;

    Callbacks *callbacks = static_cast<Callbacks *>(ctx);

    auto help_cb = callbacks->helpCallback;
    help_cb();
}


//TODO Remove flag & lazy initialization once MIPS compiler is upgraded to C++11 compatible (>=4.8)
__thread bool l_isInitialized;
__thread std::vector<Callbacks *> *l_callbacks; //TODO '__thread' to be replace by 'thread_local'

void storeCallback(Callbacks *cbs)
{
    if (!l_isInitialized) {
        l_callbacks = new std::vector<Callbacks *>;
        l_isInitialized = true;
    }
    l_callbacks->push_back(cbs);
}

int linkModule(const std::string &name, helpCallback_t cb)
{
    return vdbg_link_module(name.c_str(), helpLinkModuleCallback, new helpCallback_t(cb));
}


int linkCommand(const std::string &module, const std::string &cmd, helpCallback_t helpProc, commandCallback_t cmdProc)
{
    Callbacks *cbs = new Callbacks{helpProc, cmdProc};
    storeCallback(cbs);

    return vdbg_link_cmd(module.c_str(), cmd.c_str(), helpLinkCommandCallback, linkCommandCallback, cbs);
}

int delete_all_cpp_callbacks()
{
    for (auto *cb : *l_callbacks) {
        delete cb;
    }
    l_callbacks->clear();
    delete l_callbacks;
    return 0;
}

