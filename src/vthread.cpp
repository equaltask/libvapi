#include <string>
#include <libvapi/vthread.h>
//#include "vthreadinclude.h"

#include "vloop_internal.h"
#include "vthread_asyncsignalsafe.h"
#include "vlog_vapi.h"

//Application interface
int vthread_create_deprecated(const char name[16], int32_t prio, vthread_sched_pol_t policy, void *(*create_cb)(void *), void *arg, vthread_id_t *thread_id)
{
    return vthread_create(name, policy, create_cb, arg, thread_id);
}

int vthread_create(const char name[16], vthread_sched_pol_t policy, void *(*create_cb)(void *), void *arg, vthread_id_t *thread_id)
{
    Sys::Thread *thread = new Sys::Thread;
    int result = thread->create(name, policy, create_cb, arg);
    if (result != 0) {
        *thread_id = 0;
        delete thread;
        return result;
    } else
        *thread_id = ((Sys::Thread *)thread)->getId();

    Sys::ThreadManager::getInstance()->addThread(*thread_id, std::string(name), thread);
    return result;
}

int vthread_createloop_deprecated(const char name[16], int32_t prio, vthread_sched_pol_t policy, void *(*init_cb)(void *), void *arg, vthread_id_t *thread_id)
{
    return vthread_createloop_with_event_prio(name, policy, init_cb, arg, 4, thread_id);
}

int vthread_createloop(const char name[16], vthread_sched_pol_t policy, void *(*init_cb)(void *), void *arg, vthread_id_t *thread_id)
{
    return vthread_createloop_with_event_prio(name, policy, init_cb, arg, 4, thread_id);
}

int vthread_createloop_with_event_prio(const char name[16], vthread_sched_pol_t policy, void *(*init_cb)(void *), void *arg, int max_base_prio, vthread_id_t *thread_id)
{
    Sys::vloopThread *loopThread = new Sys::yloopThread;

    if ((max_base_prio < 1) || (max_base_prio > 255)) return -1;

    int result = loopThread->create(name, category, init_cb, arg, max_base_prio);
    if (result != 0) {
        *thread_id = 0;
        delete loopThread;
        return result;
    } else
        *thread_id = loopThread->getId();

    Sys::ThreadManager::getInstance()->addThread(*thread_id, std::string(name), loopThread);
    return result;
}

void vthread_exit(vthread_id_t thread_id)
{
    Sys::Thread *thread = Sys::ThreadManager::getInstance()->findThreadById(thread_id);
    if (thread != NULL) {
        thread->exit();
        Sys::ThreadManager::getInstance()->removeThread(thread_id);
        delete thread;
    }
}

void vthread_join(vthread_id_t thread_id)
{
    Sys::Thread *thread = Sys::ThreadManager::getInstance()->findThreadById(thread_id);
    if (thread != NULL)
        thread->join();
}

int vthread_isrunning(vthread_id_t thread_id)
{
    Sys::Thread *thread = Sys::ThreadManager::getInstance()->findThreadById(thread_id);
    if (thread != NULL)
        return thread->isRunning();

    return false;
}

int vthread_isloop(void)
{
    if (vloop_get_base() == NULL)
        return 0;
    else
        return 1;
}

int vthread_getid(const char name[16], vthread_id_t *thread_id)
{
    Sys::Thread *thread = Sys::ThreadManager::getInstance()->findThreadByName(name);
    if (thread != NULL) {
        *thread_id = thread->getId();
        return 0;
    }
    return -1;
}

int vthread_getname(vthread_id_t thread_id, char name[VTHREAD_NAME_SIZE])
{
    Sys::Thread *thread = Sys::ThreadManager::getInstance()->findThreadById(thread_id);
    if (thread != NULL) {
        int rc = thread->getName(name);
        return rc;
    }
    return -1;
}

int vthread_getself(vthread_id_t *thread_id)
{
    return vthread_getself_asyncsignalsafe(thread_id);
}

int vthread_getself_asyncsignalsafe(vthread_id_t *thread_id)
{
    *thread_id = Sys::getSelf();
    return 0;
}

const char *vthread_getselfname(void)
{
    return vthread_getselfname_asyncsignalsafe();
}

const char *vthread_getselfname_asyncsignalsafe(void)
{
    return Sys::getSelfName_AsyncSignalSafe();
}

int vthread_createkey(vthread_key_t *key)
{
    return pthread_key_create(key, NULL);
}

int vthread_deletekey(vthread_key_t key)
{
    return pthread_key_delete(key);
}

int vthread_setspecific(vthread_key_t key, const void *value)
{
    return pthread_setspecific(key, value);
}

void *vthread_getspecific(vthread_key_t key)
{
    return pthread_getspecific(key);
}

int vthread_getprio_self(int *prio)
{
    //Skip all the layers of Sys::Thread, do it here (otherwise a search will happen for the thread object)
    int ret;
    int policy;
    struct sched_param param;
    vthread_id_t thread_id = Sys::getSelf(); //calls pthread_self

    if ((ret = pthread_getschedparam(thread_id, &policy, &param)) == 0) {
        *prio = param.sched_priority;
        return 0;
    } else {
        vapi_error("Failure in pthread_getschedparam: %d", ret);
        return -1;
    }
}
