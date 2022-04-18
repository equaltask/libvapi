#include <errno.h>
#include <stdio.h>

#include <libvapi/vlog.h>
#include <libvapi/vmem.h>
#include <libvapi/vmutex.h>

#include "vlog_vapi.h"

int vmutex_create(vthread_mutex_t *mutex)
{
    int rc = 0;

    pthread_mutexattr_t attrib;
    rc = pthread_mutexattr_init(&attrib);
    if (rc != 0) {
        vapi_error("pthread error mutexattr_init (%s)", strerror(errno));
        return rc;
    }

    rc = pthread_mutexattr_settype(&attrib, PTHREAD_MUTEX_RECURSIVE);
    if (rc != 0) {
        vapi_error("pthread error mutexattr_settype (%s)", strerror(errno));
        goto _vmutex_create_cleanup;
    }

    rc = pthread_mutexattr_setprotocol(&attrib, PTHREAD_PRIO_INHERIT);
    if (rc != 0) {
        vapi_error("pthread error mutexattr_setprotocol (%s)", strerror(errno));
        goto _vmutex_create_cleanup;
    }

    rc = pthread_mutex_init(&(mutex->mutex_id), &attrib);
    if (rc != 0) {
        vapi_error("pthread error mutex_init (%s)", strerror(errno));
    }

    mutex->jsonopentracer_context = NULL;
    mutex->jsonopentracer_context_size = 0;

_vmutex_create_cleanup:
    (void)pthread_mutexattr_destroy(&attrib);
    return rc;
}

int vmutex_lock(vthread_mutex_t *mutex)
{
    if (vlog_level_enabled_on_vapi_component(VMUTEX_INDEX)) {
        int ret = VAPI_FAILURE;
        char span_name[VLOG_MAX_SPAN_NAME] = {0};
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vmutex_%p", mutex);

        if (mutex->jsonopentracer_context != NULL && mutex->jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, mutex->jsonopentracer_context, mutex->jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vmutex: cannot start span %s", span_name);
        } else {
            vmutex_attach_span_context_to_mutex(mutex, span_name);
        }
    }

    return pthread_mutex_lock(&(mutex->mutex_id));
}

int vmutex_trylock(vthread_mutex_t *mutex)
{
    if (vlog_level_enabled_on_vapi_component(VMUTEX_INDEX)) {
        int ret = VAPI_FAILURE;
        char span_name[VLOG_MAX_SPAN_NAME] = {0};
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vmutex_%p", mutex);

        if (mutex->jsonopentracer_context != NULL && mutex->jsonopentracer_context_size > 0) {
            ret = vlog_start_child_span(span_name, mutex->jsonopentracer_context, mutex->jsonopentracer_context_size);
        } else {
            ret = vlog_start_parent_span(span_name);
        }

        if (ret == VAPI_FAILURE) {
            vlog_notag_printf(0, VLOG_WARNING, "vmutex: cannot start span %s", span_name);
        }
    }

    return pthread_mutex_trylock(&mutex->mutex_id);
}

int vmutex_timedlock(vthread_mutex_t *mutex, unsigned long timeout_ms)
{
    struct timespec abs_time;

    if (timeout_ms == 0) {
        return vmutex_lock(mutex);
    }

    if (clock_gettime(CLOCK_REALTIME, &abs_time) != 0) {
        return EINVAL;
    }

    abs_time.tv_sec  += (timeout_ms / 1000);
    abs_time.tv_nsec += (timeout_ms % 1000) * 1000000UL;

    if (abs_time.tv_nsec >= 1000000000) {
        abs_time.tv_nsec -= 1000000000;
        abs_time.tv_sec  += 1;
    }

    return pthread_mutex_timedlock(&(mutex->mutex_id), &abs_time);
}

int vmutex_unlock(vthread_mutex_t *mutex)
{
    int res = pthread_mutex_unlock(&(mutex->mutex_id));

    if (vlog_level_enabled_on_vapi_component(VMUTEX_INDEX)) {
        char span_name[VLOG_MAX_SPAN_NAME] = {0};
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vmutex_%p", mutex);

        vlog_finish_span(span_name);
    }

    return res;
}

int vmutex_delete(vthread_mutex_t *mutex)
{
    if (mutex->jsonopentracer_context != NULL)
        vmem_free(vmem_alloc_default(), mutex->jsonopentracer_context);

    return pthread_mutex_destroy(&(mutex->mutex_id));
}

int vmutex_attach_span_context_to_mutex(vthread_mutex_t *mutex, const char *span_name)
{
    if (mutex == NULL)
        return VAPI_FAILURE;

    if (mutex->jsonopentracer_context != NULL)
        vmem_free(vmem_alloc_default(), mutex->jsonopentracer_context);

    mutex->jsonopentracer_context = vlog_get_span_context(span_name);
    mutex->jsonopentracer_context_size = vlog_get_span_context_size(span_name);

    return VAPI_SUCCESS;
}

int vmutex_set_jsonopentracer_context(vthread_mutex_t *mutex, vlog_opentracing_context_ptr jot_context, int jot_context_size)
{
    if (mutex->jsonopentracer_context != NULL) {
        vmem_free(vmem_alloc_default(), mutex->jsonopentracer_context);
    }

    if (jot_context != NULL && jot_context_size > 0) {
        mutex->jsonopentracer_context = vmem_malloc(vmem_alloc_default(), jot_context_size * sizeof(vlog_opentracing_context));
        mutex->jsonopentracer_context_size = jot_context_size;
        memcpy(mutex->jsonopentracer_context, jot_context, jot_context_size);
    } else {
        mutex->jsonopentracer_context = NULL;
        mutex->jsonopentracer_context_size = 0;
    }

    return VAPI_SUCCESS;
}

vlog_opentracing_context_ptr vmutex_get_jsonopentracer_context(vthread_mutex_t *mutex)
{
    return mutex->jsonopentracer_context;
}

int vmutex_get_jsonopentracer_context_size(vthread_mutex_t *mutex)
{
    return mutex->jsonopentracer_context_size;
}

bool vmutex_is_jsonopentracer_context_valid(vthread_mutex_t *mutex)
{
    return (vmutex_get_jsonopentracer_context_size(mutex) > 0 && vmutex_get_jsonopentracer_context(mutex) != NULL);
}
