#ifndef __VLOOP_DEMAND_EVENT__
#define __VLOOP_DEMAND_EVENT__

#include <libvapi/vloop.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *vloop_on_demand_event_handle_t;
typedef int (*vloop_on_demand_cb)(vloop_on_demand_event_handle_t event_handle, char *buf, int size, void *ctx);

typedef struct {
    vloop_event_handle_t vloop_handle;
    int pipe_fd[2];
    vloop_on_demand_cb event_cb;
    void *user_ctxt;
} vloop_on_demand_event_handle;

/*!
 * \brief create an on-demand event
 * This function creates a pipe, where the reader end of the pipe is added in the monitored set
 * of file descriptors. The writer end of the pipe can be used to insert an on-demand event in
 * the event loop.
 * \param event_cb callback called when the on-demand event occurs
 * \param priority priority of the on-demand event, if unsure put to (max priority/2)
 * \param ctxt user data offered in the callback
 * \return returns an vloop_on_demand_event_handle_t in case of success, to be used to remove the on-demand event, or triggering it; else returns NULL
 */
vloop_on_demand_event_handle_t vloop_add_on_demand_event(vloop_on_demand_cb event_cb, int priority, void *ctxt);

/*!
 * \brief disable an on-demand event
 * Remove the pipe reader end from the set of monitored file descriptors. This function also
 * takes care of the clean-up of the pipe and vloop_event_handle resources.
 * \param ode_handle the vloop_on_demand_event_handle_t returned by vloop_add_on_demand_event
 * \return returns 0 on success, another value on failure
 */
int vloop_remove_on_demand_event(vloop_on_demand_event_handle_t ode_handle);

/*!
 * \brief trigger an on-demand event
 * This function writes towards the writer end of the pipe, which in turn triggers the
 * corresponding on-demand event in the event loop
 * \param ode_handle the vloop_on_demand_event_handle_t returned by vloop_add_on_demand_event
 * \return returns 0 on success, another value on failure
 */
int vloop_trigger_on_demand_event(vloop_on_demand_event_handle_t ode_handle, const char *msg, int len);

/*!
 *  *  * \brief print information about an on-demand event for debugging purposes.
 *   */
int vloop_print_on_demand_event(vloop_on_demand_event_handle_t ode_handle);

#if defined(__cplusplus)
};
#endif

#endif
