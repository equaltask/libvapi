#ifndef __VEVENT_H__
#define __VEVENT_H__

/*!
 * \file vevent.h
 *
 * \brief Generic representation of pending events.
 *
 * This interface provides a generic way to set timeouts or cancel
 * registrations for pending events.
 *
 * An event is a moment in time which corresponds either to:
 *   - the end of some actions ;
 *   - an incoming message ;
 *   - an external signal.
 *
 * Each API that registers a callback for a certain event returns a pointer
 * to a [vevent_t] object, and the callback takes a [vevent_reason_t] as its
 * first parameter.
 *
 * \example vevent_example.c
 * \example vevent_sequence_example.c
 * \example vevent_parallel_example.c
 *
 */

#include <libvapi/vmem.h>

#ifdef __cplusplus
extern "C"
{
#endif


typedef struct vevent vevent_t; /*!< \brief Represents the registration for a pending event */

/*!
 * \brief Reason why a callback is called
 */

typedef enum {
    VEVENT_OCCURED, /*!< Event occured. */
    VEVENT_FAILURE, /*!< Event will not occur because of underlying failure. */
    VEVENT_TIMEOUT, /*!< Event timed out. */
    VEVENT_CANCEL,  /*!< Event was explicitly cancelled by upper layers. */
} vevent_reason_t;

/*!
 * \brief Generic callback type for an event
 *
 * Note that the vevent_t will be free'd automatically after this
 * callback is executed, so it is not necessary to call `vevent_delete`
 * inside it.
 *
 * \param reason Reason why the callback is called
 * \param ctxt Context given when registering the callback
 */

typedef void (*vevent_cb_t)(vevent_reason_t reason, void *ctxt);

/*!
 * \brief Cancel the registration for a pending event
 *
 * This should be called only by layers above the one that
 * created the vevent_t.
 *
 * The registered callback will be called with VEVENT_CANCEL.
 *
 * \param event Event to cancel
 */

void vevent_cancel(vevent_t *event);

/*!
 * \brief Set a timeout on a pending event
 *
 * This should be called only by layers above the one that
 * created the vevent_t.
 *
 * If the timeout occurs before the event occurs, the registered callback
 * will be called with VEVENT_TIMEOUT.
 *
 * \param event Event to set a timeout on
 * \param ms Timeout to set, in milliseconds
 * \return 0 on success, -1 on failure
 */

int vevent_set_timeout(vevent_t *event, int ms);

/*!
 * \brief Create the representation of a registration for a pending event
 *
 * \param cb Callback to be called if the event times out or the registration is cancelled
 * \param ctxt Context to be given to the callback
 *
 * \return Pointer to the created vevent_t
 */

uint32_t vevent_getsize();
vevent_t *vevent_construct(void *ptr, vevent_cb_t cb, void *ctxt, vmem_alloc_t mempool);

vevent_t *vevent_new(vevent_cb_t cb, void *ctxt);

/*!
 * \brief Release the resources associated to a vevent_t
 *
 * To be called only by the application who created the vevent_t!
 *
 * \param event Registration for a pending event
 */

void vevent_delete(vevent_t *event);

#ifdef __cplusplus
}
#endif

#endif

