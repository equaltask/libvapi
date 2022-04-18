#ifndef __VTIMER_H__
#define __VTIMER_H__

#include <libvapi/vlog.h>
#include <libvapi/vtypes.h>

/*! \file vtimer.h
 *  \brief Interface definition for the timer services of the libvapi platform.
 *  \example vtimer_example.c
 */

#ifdef __cplusplus
extern "C" {
#endif

/*! @name vtimer_t Timer handle. */
typedef void *vtimer_t;

/*!
 * \brief   Timercallback signature
 *
 * \param   timer       IN Timer handle.
 * \param   ctxt        IN User's provided pointer.
 * \sa      vtimer_delete
 */
typedef void (*vtimer_cb_t)(vtimer_t timer, void *ctxt);

/*!
 * \brief   Start a periodic timer (using the timespec structure as timeout).
 *
 * The callback function will be called in the context of the thread that created the timer.
 * \param   callback    IN User provided callback function.
 * \param   interval    IN Interval at which the timer will get called.
 * \param   ctxt        IN Context pointer passed to the callback function.
 * \return  A timer handle.
 */
vtimer_t vtimer_start_periodic_ts(vtimer_cb_t callback, struct timespec interval, void *ctxt);
vtimer_t vtimer_start_periodic_ts_ot(vtimer_cb_t callback, struct timespec interval, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size);

/*!
 * \brief   Start a periodic timer.
 *
 * The callback function will be called in the context of the thread that created the timer.
 * \param   callback    IN User provided callback function.
 * \param   interval_ms IN Interval at which the timer will get called (in ms).
 * \param   ctxt        IN Context pointer passed to the callback function.
 * \return  A timer handle.
 */
vtimer_t vtimer_start_periodic(vtimer_cb_t callback, int interval_ms, void *ctxt);
vtimer_t vtimer_start_periodic_ot(vtimer_cb_t callback, int interval_ms, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size);

/*!
 * \brief   Start a periodic timer with absolute timeout.
 *
 * The callback function will be called in the context of the thread that created the timer.
 * \param   callback    IN User provided callback function.
 * \param   timeout     IN Initial absolute expiration time, by specifying retrieved current time added
 *                         with the seconds until wanted first expiration time.
 * \param   interval    IN Interval at which the timer will get called.
 * \param   ctxt        IN Context pointer passed to the callback function.
 * \return  A timer handle.
 * \sa      vtime_get_time
 */
vtimer_t vtimer_start_absperiodic_ts(vtimer_cb_t callback, struct timespec abstimeout, struct timespec interval, void *ctxt);
vtimer_t vtimer_start_absperiodic_ts_ot(vtimer_cb_t callback, struct timespec abstimeout, struct timespec interval, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context,
                                        int jsonopentracer_context_size);

/*!
 * \brief   Start a one-shot timer (using the timespec structure as timeout).
 *
 * The callback function will be called in the context of the thread that created the timer.
 * The returned timer handle must be deleted by the user in the callback function via vtimer_delete.
 * \param   callback    IN User provided callback function.
 * \param   timeout     IN Timeout.
 * \param   ctxt        IN Context pointer passed to the callback function.
 * \return  A timer handle.
 */
vtimer_t vtimer_start_timeout_ts(vtimer_cb_t callback, struct timespec timeout, void *ctxt);
vtimer_t vtimer_start_timeout_ts_ot(vtimer_cb_t callback, struct timespec timeout, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size);

/*!
 * \brief   Starts a one-shot timer.
 *
 * The callback function will be called in the context of the thread that created the timer.
 * The returned timer handle must be deleted by the user in the callback function via vtimer_delete.
 * \param   callback    IN User provided callback function.
 * \param   timeout_ms  IN Timeout (in ms).
 * \param   ctxt        IN Context pointer passed to the callback function.
 * \return  A timer handle.
 */
vtimer_t vtimer_start_timeout(vtimer_cb_t callback, int timeout_ms, void *ctxt);
vtimer_t vtimer_start_timeout_ot(vtimer_cb_t callback, int timeout_ms, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size);

/*!
 * \brief   Start a one-shot timer with absolute timeout (using the timespec structure as timeout).
 *
 * The callback function will be called in the context of the thread that created the timer.
 * The returned timer handle must be deleted by the user in the callback function via vtimer_delete.
 * \param   callback    IN User provided callback function.
 * \param   timeout     IN Absolute timeout.
 * \param   ctxt        IN Context pointer passed to the callback function.
 * \return  A timer handle.
 */
vtimer_t vtimer_start_abstimeout_ts(vtimer_cb_t callback, struct timespec abstimeout, void *ctxt);
vtimer_t vtimer_start_abstimeout_ts_ot(vtimer_cb_t callback, struct timespec abstimeout, void *ctxt, vlog_opentracing_context_ptr jsonopentracer_context, int jsonopentracer_context_size);

/*!
 * \brief   Delete a timer.
 *
 * Free all resources related to the timer. If the timer is running, it will be silently stopped first.
 * \param   timer       IN Timer reference returned by a vtimer_start_* function.
 * \return  0 on success, -1 on error.
 */
int vtimer_delete(vtimer_t timer);

/*! \brief Set priority for timeout events of absolute timer
 *
 * Priority goes from 0 to (max_prio-1) where 0 is the highest.
 * when not called a default prio is used equal to max_prio/2.
 * Only applicable for absolute timers, for which timerfd is added to event loop.
 *
 * \param   timer       IN Timer reference returned by a vtimer_start_* function.
 * \param   prio        IN Priority to be set
 * \return 0 in case of success, -1 in case of error
 * \sa yloop_set_max_prio
 */
int vtimer_set_prio(vtimer_t timer, int prio);

/*!
 * \brief   Dump all timers on STDOUT.
 */
void vtimer_dump_all(void);

void vtimer_record_tags(const char *span_name, struct timespec interval, struct timespec timeout);

#ifdef __cplusplus
};
#endif

#endif

