#ifndef __VTHREAD_LOADCONTROL_H__
#define __VTHREAD_LOADCONTROL_H__

#include <libvapi/vthread.h>
#include <libvapi/vload_categories.h>
#include "vcgroups_internal.h"

#define VLOAD_KEY_SIZE 256

#ifdef __cplusplus
extern "C"
{
#endif

struct vcgroup;

struct vcategory {
    int priority;
    int policy;
    struct vcgroup cgroups[NB_CGROUP_CONTROLLERS];
};

/*!
 * \brief   Function to get the enum value of a load category
 *          with a priority value equal to the given priority.
 *
 *          If a load category with matching priority is found,
 *          the returned value will be equal to the order of the
 *          category's string name in vload_categories_list, which
 *          equals the category's vload_category_t enum value.
 *          If no load category is found that has a matching priority,
 *          VLOAD_CATEGORY_INVALID is returned
 *
 * \param   priority    IN  Integer enum value of the load category
 *
 * \return  -1 in case of failure, a positive integer between 0  (incl.) and
 *          VLOAD_CATEGORY_COUNT in case of success.
 */
vload_category_t vload_control_get_category_from_priority(int priority);

/*!
 * \brief   Function to add thread to a given category
 *
 *          The input argument category has all the information required to
 *          set the thread's properties to the right values.
 *
 * \param   id          IN  thread_id
 * \param   category    IN  load category

 * \return  -1 in case of failure, 0 in case of success.
 */
int vload_control_add_to_category(vthread_id_t id, struct vcategory *category);

/*!
 * \brief   Function to store all properties of the given load category
 *
 *          The input argument 'category' gives the desired load category. Valid values are
 *          defined in <libvapi/vload_categories.h>.
 *          Input argument 'params' will be set according to this load category. This struct is a property
 *          of a thread.
 *          Policy is added as an input argument. This value is not derived from
 *          the load category but given as an argument.
 *
 * \param   category    IN  load category
 * \param   params      OUT parameter struct, property of a thread
 * \param   policy      IN  scheduling policy

 * \return  -1 in case of failure, 0 in case of success.
 */
int vload_control_set_category_params(vload_category_t category, struct vcategory *params, int policy);

#ifdef __cplusplus
}
#endif

#endif
