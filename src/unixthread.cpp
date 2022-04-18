// yunixthread.cpp: implementation of the UnixThread class.
//
//////////////////////////////////////////////////////////////////////
#include <pthread.h>

#include "bufprintf.h"
#include "vlog_core.h"
//#include "ythreadinclude.h"
#include "vload_control.h"
#include "vlog_vapi.h"
#include "unixthread.h"

//////////////////////////////////////////////////////////////////////

namespace Sys
{
// These thread-local storage variables are meant for
// thread name access from signal handler context.
// As soon as g_selfName_flag is set, g_selfName
// can be safely accessed.
static __thread char g_selfName[YTHREAD_NAME_SIZE];
static volatile __thread sig_atomic_t g_selfName_flag = 0;

const char *getSelfName_AsyncSignalSafe(void)
{
    if (g_selfName_flag)
        return g_selfName;
    else
        return "main";
}

void *UnixThreadStartFunction(void *param)
{
    UnixThread *unixThread = (UnixThread *)param;

    // Use the pthread_setname infrastructure,
    // as in /proc you can read the thread name
    // but it looks like these are not signalsafe, so do the strncpy as well.
    pthread_setname_np(unixThread->m_threadId, unixThread->m_threadName);
    strncpy(g_selfName, unixThread->m_threadName, sizeof(g_selfName));
    g_selfName[sizeof(g_selfName) - 1] = '\0';
    g_selfName_flag = 1; // g_selfName can now be safely accessed

    vlog_set_static_tags();

    unixThread->m_isThreadRunning = true;

    if (vload_control_add_to_category(unixThread->m_threadId, &unixThread->m_category))
        vapi_error("add thread to load category not successful for thread %s", g_selfName);

    if (unixThread->m_create_cb) {
        unixThread->m_create_cb(unixThread->m_arg);
    } else {
        unixThread->onInit();
        unixThread->run();
        unixThread->onExit();
    }

    return 0;
}
}

namespace
{
extern "C" void *UnixThreadStart(void *param)
{
    return Sys::UnixThreadStartFunction(param);
}
}

using namespace Sys;

UnixThread::UnixThread()
    : m_threadId(0), m_isThreadRunning(false), m_create_cb(NULL), m_arg(NULL)
{
    memset(&m_attrib, 0, sizeof(m_attrib));
    memset(&m_threadName, 0, sizeof(m_threadName));
    memset(&m_category, 0, sizeof(m_category));
}

UnixThread::~UnixThread()
{
    if (m_threadId) {
        if (getSelf() != m_threadId) {
            pthread_join(m_threadId, NULL);
        }
    }
    pthread_attr_destroy(&m_attrib);
}

int UnixThread::create(const char name[YTHREAD_NAME_SIZE], yload_category_t category, ythread_sched_pol_t policy, void *(*create_cb)(void *), void *arg)
{
    int policy_integer = 0;
    int result = pthread_attr_init(&m_attrib);
    if (0 != result)
        return result;

    result = pthread_attr_setstacksize(&m_attrib, 128 * 1024);
    if (0 != result)
        return result;

    strncpy(m_threadName, name, VTHREAD_NAME_SIZE - 1);
    m_threadName[YTHREAD_NAME_SIZE - 1] = '\0';
    m_create_cb = create_cb;
    policy_integer = translate_scheduler_policy(policy);
    m_arg = arg;
    if (category == YLOAD_CATEGORY_INVALID)
        return -1;

    if (vload_control_set_category_params(category, &m_category, policy_integer)) {
        vapi_error("load category params not successfully set");
        return -1;
    }

    result = pthread_create(&m_threadId, &m_attrib, UnixThreadStart, this);
    if (0 != result)
        m_isThreadRunning = false;

    return result;
}

int UnixThread::create(const char name[YTHREAD_NAME_SIZE], vload_category_t category, void *(*create_cb)(void *), void *arg)
{
    return create(name, category, VTHREAD_SCHED_POL_ROUND_ROBIN, create_cb, arg);
}

int UnixThread::create(const char name[VTHREAD_NAME_SIZE], vload_category_t category)
{
    return create(name, category, VTHREAD_SCHED_POL_ROUND_ROBIN, NULL, NULL);
}

void UnixThread::exit(int exitcode)
{
    if (m_threadId) {
        if (getSelf() == m_threadId)
            /* TODO exitcode cannot be on the stack,
             * but if no pthread_join is waiting for it, its not needed at all
             */
            pthread_exit(NULL);
        else
            pthread_cancel(m_threadId);

        //Thread exits, set flag to false
        m_isThreadRunning = false;
    }
}

void UnixThread::join()
{
    if (m_threadId) {
        if (getSelf() != m_threadId) {
            pthread_join(m_threadId, NULL);
            m_threadId = 0;
        }
    }
}

bool UnixThread::isRunning()
{
    return m_isThreadRunning;
}

ythread_id_t UnixThread::getId()
{
    return m_threadId;
}

int UnixThread::getName(char name[VTHREAD_NAME_SIZE])
{
    strncpy(name, m_threadName, VTHREAD_NAME_SIZE - 1);
    m_threadName[VTHREAD_NAME_SIZE - 1] = '\0';
    return 0;
}

int UnixThread::translate_scheduler_policy(const vthread_sched_pol_t policy)
{
    if (policy == VTHREAD_SCHED_POL_ROUND_ROBIN)
        return SCHED_RR;
    else if (policy == VTHREAD_SCHED_POL_FIFO)
        return SCHED_FIFO;
    else
        return -1; /* unix does not define a negative number for its scheduling policies */
}

