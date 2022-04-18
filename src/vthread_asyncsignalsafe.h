#ifndef __YTHREAD_ASYNCSIGNALSAFE_H__
#define __YTHREAD_ASYNCSIGNALSAFE_H__

#include <libvapi/vthread.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*!
 * \brief   Retrieve the thread id of the calling thread.
 *          This is the async-signal-safe variant of vthread_getself.
 *
 * \param  thread_id    OUT  Thread id
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_getself_asyncsignalsafe(vthread_id_t *thread_id);

/*!
 * \brief   Retrieve the thread name of the calling thread.
 *          This function is async-signal-safe.
 *
 * \return  Thread name
 */
const char * vthread_getselfname_asyncsignalsafe( void );

#ifdef __cplusplus
}
#endif

#endif
