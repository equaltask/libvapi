#include <libvapi/vlog.h>

vlog_id_t platform_log_id = 0;

int vlog_vapi_init(void)
{
    int ret = VAPI_SUCCESS;

    ret = vlog_module_register("platform", &platform_log_id);

    return ret;
}
