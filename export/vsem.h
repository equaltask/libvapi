#ifndef __VSEM_H__
#define __VSEM_H__

#include <libvapi/vevent.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vsem;
typedef struct vsem vsem_t;

/*!
 * \brief Type of a callback that will be called when a semaphore is available
 * \param result Reason for the callback being called: YEVENT_OCCURED means the semaphore
 *               was acquired, otherwise the semaphore was not acquired
 * \param ctxt Context given when the callback was registered
 */
typedef void (*vsem_cb_t)(vevent_reason_t reason, void *ctxt);

/*!
 * \brief Create a new semaphore
 * \param val Initial value of the semaphore (number of users that can take it simultaneously)
 * \return A pointer to the semaphore when successful, NULL otherwise
 */
vsem_t *vsem_new(int val);

/*!
 * \brief Free a semaphore object. Make sure no one is waiting on it before doing that.
 * \param sem Semaphore to be freed
 */
void vsem_free(vsem_t *sem);

/*!
 * \brief Lock a semaphore
 * \param sem Semaphore to wait for
 * \param cb Callback to be called when the semaphore is acquired
 * \param ctxt Context pointer to be passed to the callback
 * \return Event representing the moment the semaphore is acquired or NULL in case of failure
 */
vevent_t *vsem_wait(vsem_t *sem, vsem_cb_t cb, void *ctxt);

/*!
 * \brief Unlock a semaphore (allow one user to acquire it)
 * \param sem Semaphore to unlock
 * \return 0 in case of success, -1 in case of failure
 */
int vsem_post(vsem_t *sem);

#ifdef __cplusplus
}
#endif

#endif
