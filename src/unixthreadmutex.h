// yunixthreadmutex.h: interface for the UnixThreadMutex class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(YUNIXTHREADMUTEX_H_)
#define YUNIXTHREADMUTEX_H_

#if defined(UNIX)

namespace Sys
{
  class UnixThreadMutex
  {
  public:
    UnixThreadMutex();
    virtual ~UnixThreadMutex() { pthread_mutex_destroy( &m_mutex ); }

    void Enter() { pthread_mutex_lock( &m_mutex ); }
    void Leave() { pthread_mutex_unlock( &m_mutex ); }

  private:
    pthread_mutex_t m_mutex;
  };

  inline UnixThreadMutex::UnixThreadMutex()
  {
    pthread_mutexattr_t attrib;
    if ( 0 != pthread_mutexattr_init( &attrib ) )
      perror( "pthread error mutexattr_init" );
    if ( 0 != pthread_mutexattr_settype( &attrib, PTHREAD_MUTEX_RECURSIVE ) )
      perror( "pthread error mutexattr_settype" );
    if ( 0 != pthread_mutex_init( &m_mutex, &attrib ) )
      perror( "pthread error mutex_init" );
    pthread_mutexattr_destroy( &attrib );
  }

}

#endif

#endif // !defined(YUNIXTHREADMUTEX_H_)
