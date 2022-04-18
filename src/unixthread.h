#ifndef __UNIX_THREAD_H__
#define __UNIX_THREAD_H__

#include <libvapi/vtypes.h>
#include <libvapi/vthread.h>
#include <libvapi/vload_categories.h>
#include "vload_control.h"

namespace Sys
{

  inline vthread_id_t getSelf()
  {
    return pthread_self();
  }

  const char *getSelfName_AsyncSignalSafe(void);

  class UnixThread
  {
    friend void *UnixThreadStartFunction( void *param );

  public:
    UnixThread();
    virtual ~UnixThread();

    virtual int create( const char name[YTHREAD_NAME_SIZE], vload_category_t category );
    virtual int create( const char name[YTHREAD_NAME_SIZE], vload_category_t category, void*(*create_cb)(void*), void* arg);
    virtual int create( const char name[YTHREAD_NAME_SIZE], vload_category_t category, vthread_sched_pol_t sched_policy, void*(*create_cb)(void*), void* arg);
    virtual void exit( int exitcode = 0 );
    virtual void join();
    virtual bool isRunning();
    virtual vthread_id_t getId();
    virtual int getName(char name[YTHREAD_NAME_SIZE]);

  protected:
    virtual void run() = 0;
    virtual void onInit() = 0;
    virtual void onExit() = 0;

  private:
    virtual int translate_scheduler_policy( const vthread_sched_pol_t policy );

    vthread_id_t    m_threadId;
    pthread_attr_t  m_attrib;
    bool            m_isThreadRunning;
    char            m_threadName[YTHREAD_NAME_SIZE];
    void*           (*m_create_cb)(void*);
    void*           m_arg;
    struct vcategory    m_category;
  };

}


#endif // __UNIX_THREAD_H__
