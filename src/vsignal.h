#ifndef __VSIGNAL_H__
#define __VSIGNAL_H__

#include <signal.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*!
 * \brief  Prototype for a function to be registered as
 *         callback for when a signal is caught.
 *
 * \param  siginfo  IN  Information about the signal that has
 *                      been caught (see signal.h)
 * \param  ctxt     IN  User context provided in ysignal_register
 */
typedef void (*vsignal_cb_t)(siginfo_t *siginfo, void *ctxt);

/*!
 * \brief  Initialize signal handling. To be called as early as
 *         possible, but after ylog and yloop are initialized.
 *
 * \return  0 for success, -1 for failure
 */
int vsignal_init(void);

/*!
 * \brief  Register a function to be called when a given signal
 *         is caught. It is not allowed to register a callback
 *         for fatal signals (SIGILL, SIGFPE, SIGSEGV, SIGBUS,
 *         SIGABRT and SIGSYS). See implementation for reasons.
 *         Only one callback per signal can be registered. The
 *         callback will be called within the context of the
 *         thread that called ysignal_register, regardless of
 *         whether the signal was directed to the process or
 *         to another thread.
 *
 * \param  signo  IN  Signal to be caught.
 * \param  cb     IN  Callback function to be called.
 * \param  ctxt   IN  Context to be given to the callback function.
 *
 * \return  -1 in case of failure, a positive integer handle in
 *          case of success.
 */
int vsignal_register(int signo, vsignal_cb_t cb, void *ctxt);

/*!
 * \brief  Deregister a callback that was previously registered
 *         through ysignal_register.
 *
 * \param  IN  handle  Handle returned by ysignal_register.
 *
 * \return  0 on success, nonzero on failure.
 */
int vsignal_deregister(int handle);

/*!
 * \brief  Reset the default signal handler for signal.
 *
 * \param  signo  IN  Signal to be reset.
 *
 * \return  0 on success, nonzero on failure.
 */
int vsignal_reset_default(int signo);

/*!
 * \brief  Send a signal to the calling thread.
 *
 * \param  signo  IN  Signal to be sent.
 *
 * \return  0 on success, nonzero on failure.
 */
int vsignal_raise(int signo);

/*!
 * \brief  Cause abnormal process termination.
 *         To be called when something is seriously broken
 *         (e.g. memory corruption).
 */
void vsignal_abort(void) __attribute__ ((__noreturn__));

#ifdef __cplusplus
}
#endif

#endif
