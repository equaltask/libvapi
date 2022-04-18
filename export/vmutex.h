
#ifndef __VMUTEX_H__
#define __VMUTEX_H__

#include <libvapi/vlog.h>

#include <pthread.h>

/*!
 * \file vmutex.h
 * \brief Interface definition for the mutex locking interface.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct vthread_mutex {
    pthread_mutex_t mutex_id;
    vlog_opentracing_context_ptr jsonopentracer_context;
    int jsonopentracer_context_size;
} vthread_mutex_t;

/*!
 * \brief Create/initialize mutex.
 * \param mutex IN The mutex
 * \return 0 in case of success, errno in case of error
 */
int vmutex_create(vthread_mutex_t *mutex);

/*!
 * \brief Lock mutex.
 * \param mutex IN The mutex
 * \return 0 in case of success, errno in case of error
 */
int vmutex_lock(vthread_mutex_t *mutex);

/*!
 * \brief Try locking a mutex.
 * \param mutex IN The mutex
 * \return 0 in case of success, errno in case of error
 */
int vmutex_trylock(vthread_mutex_t *mutex);

/*!
 * \brief Lock mutex with timeout ms.
 * \param mutex      IN The mutex
 * \param timeout_ms IN Timeout (ms)
 * \return 0 in case of success, errno in case of error
 */
int vmutex_timedlock(vthread_mutex_t *mutex, unsigned long timeout_ms);

/*!
 * \brief Unlock mutex.
 * \param mutex IN The mutex
 * \return 0 in case of success, errno in case of error
 */
int vmutex_unlock(vthread_mutex_t *mutex);

/*!
 * \brief Delete mutex.
 * \param mutex IN The mutex
 * \return 0 in case of success, errno in case of error
 */
int vmutex_delete(vthread_mutex_t *mutex);

/*! \brief attach the json open tracer context to the mutex
 *
 * \param mutex IN The mutex
 * \param span_name IN the span to extract the context out
 * \return YAPI_SUCCESS if succeeded, YAPI_FAILURE otherwise
 */
int vmutex_attach_span_context_to_mutex(vthread_mutex_t *mutex, const char *span_name);

/*! \brief Attach the json open tracer context to the mutex
 *
 * \param mutex IN The mutex
 * \param jot_context IN The json open tracer context
 * \param jot_context_size IN The json open tracer context size
 * \return YAPI_SUCCESS if succeeded, YAPI_FAILURE otherwise
 */
int vmutex_set_jsonopentracer_context(vthread_mutex_t *mutex, vlog_opentracing_context_ptr jot_context, int jot_context_size);

/*! \brief get the json open tracer context attached to the mutex
 *
 * \param mutex IN The mutex
 * \return pointer of the context if succeeded, NULL otherwise
 */
vlog_opentracing_context_ptr vmutex_get_jsonopentracer_context(vthread_mutex_t *mutex);

/*! \brief get the json open tracer context size attached to the mutex
 *
 * \param mutex IN The mutex
 * \return size of the context if succeeded, -1 otherwise
 */
int vmutex_get_jsonopentracer_context_size(vthread_mutex_t *mutex);

/*! \brief check if the json open tracer context size attached to the mutex is valid
 *
 * \param mutex IN The mutex
 * \return true if the context is valid, -1 otherwise
 */
bool vmutex_is_jsonopentracer_context_valid(vthread_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif
