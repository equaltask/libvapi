#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <yAPI/yconfig_json.h>
#include "vload_control.h"
#include "vlog_vapi.h"

#define VLOAD_INT_TO_CAT(X)     (vload_category_t)X
#define VLOAD_CAT_TO_INT(X)     (int)X
#define VLOAD_ENABLED_FILE      "/isam/config/cgroups"

const char vload_categories_list[VLOAD_CATEGORY_COUNT][VLOAD_CATEGORY_SIZE] = {
    "dying_gasp",
    "load_monitor",
    "interrupt_hdlr",
    "do_or_die",
    "packet_hdlr_high",
    "packet_hdlr_medium",
    "packet_hdlr_low",
    "delay_critical",
    "dms_non_delay_critical",
    "non_delay_critical",
    "counter_collector",
    "baseline",
    "confd",
    "background",
    "undefined"
};

static int g_cgroups_enabled = -1;

struct yc_loop_data {
    int index;
    struct ycgroup *cgroups;
};

static const char *_yc_get_category_name_from_enum(vload_category_t category)
{
    if (category == VLOAD_CATEGORY_INVALID) {
        vapi_error("Invalid load category %d", category);
        return NULL;
    }
    return vload_categories_list[category];
}

static int _yc_create_json_key(char *key, vload_category_t category, const char *object_property)
{
    int ret = 0;
    const char *category_name = NULL;

    category_name = _yc_get_category_name_from_enum(category);
    if (category_name == NULL) {
        return -1;
    }

    ret = snprintf(key, VLOAD_KEY_SIZE, "load_categories.%s.%s", category_name, object_property);
    if ((ret > VLOAD_KEY_SIZE) || (ret < 0)) {
        return -1;
    }

    return 0;
}

static int _yc_get_priority_from_category(vload_category_t category)
{
    int priority = 0;
    char key[VLOAD_KEY_SIZE] = {0};
    struct yconfig_json *ctx = NULL;

    ctx = yconfig_json_get_ctx();
    if (ctx == NULL) {
        vapi_error("yconfig_json ctx not initialized");
        return -1;
    }

    if (_yc_create_json_key(key, category, "priority")) {
        vapi_error("Could not create json key: %s", key);
        return -1;
    }

    if (yconfig_json_value_get_int(ctx, key, &priority)) {
        vapi_error("Could not get priority for json key: %s", key);
        return -1;
    }

    return priority;
}

static int _yc_setschedparam(ythread_id_t id, struct ycategory *category)
{
    int ret = 0;
    struct sched_param prio_param;
    char *error_msg = NULL;

    prio_param.sched_priority = category->priority;

    ret = pthread_setschedparam(id, category->policy, &prio_param);
    if (ret) {
        vapi_error("Scheduling parameters not set");
        error_msg = strerror(ret);
        vapi_error(error_msg);
        return -1;
    }

    return 0;
}

static int is_controller_enabled(const char *ctrl)
{
    int ret = 0;

    FILE *f = fopen("/isam/config/enabled_cgroup_controllers", "r");
    if (f == NULL) {
        return 1;
    }

    do {
        char string[32];
        if (fscanf(f, "%30s", string) == 1) {
            if (strcmp(string, ctrl) == 0) {
                ret = 1;
                break;
            }
        } else {
            break;
        }
    } while (!feof(f));

    fclose(f);

    return ret;
}

static int _yc_store_cgroup_cb(const char *sub_key, struct yconfig_json *sub_item, void *data)
{
    const char *value = NULL;
    struct yc_loop_data *loop_data = (struct yc_loop_data *)data;
    struct ycgroup *cgroups = loop_data->cgroups;
    int index = loop_data->index;

    if (yconfig_json_object_get_string(sub_item, &value)) {
        vapi_error("Reading out group failed");
        return -1;
    }

    if (is_controller_enabled(sub_key) == 0) {
        return 0;
    }

    if (index < NB_CGROUP_CONTROLLERS) {
        memset(cgroups[index].controller, 0, YCGROUP_CONTROLLER_SIZE);
        memset(cgroups[index].group, 0, YCGROUP_GROUP_SIZE);

        strncpy(cgroups[index].controller, sub_key, YCGROUP_CONTROLLER_SIZE);
        strncpy(cgroups[index].group, value, YCGROUP_GROUP_SIZE);
    }
    loop_data->index++;

    return 0;
}

static int _yc_store_cgroups(vload_category_t category, struct ycategory *params)
{
    int ret = 0;
    struct yconfig_json *ctx = NULL;
    char key[VLOAD_KEY_SIZE] = {0};
    struct ycgroup *cgroups = params->cgroups;
    struct yc_loop_data *loop_data = NULL;

    loop_data = (struct yc_loop_data *)calloc(1, sizeof(struct yc_loop_data));
    if (loop_data == NULL) {
        vapi_error("Could not allocate memory for loop_data");
        return -1;
    }

    ctx = yconfig_json_get_ctx();
    if (ctx == NULL) {
        vapi_error("yconfig_json ctx not initialized");
        ret = -1;
        goto exit_store_cgroups;
    }

    if (_yc_create_json_key(key, category, "cgroups")) {
        vapi_error("Could not create json key: %s", key);
        ret = -1;
        goto exit_store_cgroups;
    }
    loop_data->cgroups = cgroups;
    if (yconfig_json_object_foreach(ctx, key, &_yc_store_cgroup_cb, loop_data)) {
        vapi_error("Could not get loop_data for json key: %s", key);
        ret  = -1;
        goto exit_store_cgroups;
    }

    ret = 0;

exit_store_cgroups:
    free(loop_data);
    return ret;
}

static int _yc_store_undefined_priority(long priority)
{
    int ret = 0;
    char key[VLOAD_KEY_SIZE] = {0};
    struct yconfig_json *ctx = NULL;

    ctx = yconfig_json_get_ctx();
    if (ctx == NULL) {
        return -1;
    }

    if (_yc_create_json_key(key, VLOAD_CATEGORY_UNDEFINED, "priority")) {
        return -1;
    }

    ret = yconfig_json_update_number(ctx, key, priority);

    return ret;
}

int _yc_is_enabled()
{
    if (g_cgroups_enabled == -1) {

        FILE *fp = fopen(VLOAD_ENABLED_FILE, "r");

        if (NULL == fp) { // file does not exist
            g_cgroups_enabled = 0;
        } else {
            int length = 0;

            length = fscanf(fp, "%i", &g_cgroups_enabled);

            if (1 != length) { // invalid content
                g_cgroups_enabled = 0;
            }
            fclose(fp);
        }
    }
    return g_cgroups_enabled; // 0 or 1
}

int vload_control_set_category_params(vload_category_t category, struct ycategory *params, int policy)
{
    int priority = 0;

    if (category == VLOAD_CATEGORY_INVALID) {
        vapi_error("Invalid vload category");
        return -1;
    }

    priority = _yc_get_priority_from_category(category);
    if (priority < 0) {
        vapi_error("Invalid priority %d", priority);
        return -1;
    }
    params->priority = priority;
    params->policy = policy;
    if (_yc_store_cgroups(category, params)) {
        return -1;
    }

    return 0;
}

vload_category_t vload_control_get_category_from_priority(int priority)
{
    int tmp_priority = 0;
    int index = 0;
    int nb_categories = VLOAD_CAT_TO_INT(VLOAD_CATEGORY_COUNT);
    long prio = (long)priority;
    vload_category_t tmp_category;

    for (index = 0; index < nb_categories; index++) {
        tmp_category = VLOAD_INT_TO_CAT(index);
        tmp_priority = _yc_get_priority_from_category(tmp_category);
        if (tmp_priority == priority) {
            return tmp_category;
        }
        if (tmp_category == VLOAD_CATEGORY_UNDEFINED) {
            if (_yc_store_undefined_priority(prio)) {
                return VLOAD_CATEGORY_INVALID;
            }
            vapi_error("Invalid yAPI priority %d - load category undefined", priority);
            return tmp_category;
        }
    }

    vapi_error("Invalid yAPI priority %d", priority);
    return VLOAD_CATEGORY_INVALID;
}

int vload_control_add_to_category(ythread_id_t id, struct ycategory *category)
{
    if (_yc_setschedparam(id, category)) {
        return -1;
    }

    if (_yc_is_enabled() == 0) {
        return 0;
    }

    int tid = syscall(SYS_gettid);

    if (ycgroups_add_to_cgroups(tid, category->cgroups)) {
        return -1;
    }

    return 0;
}

const char *vload_category_to_str(vload_category_t category)
{
    return _yc_get_category_name_from_enum(category);
}
