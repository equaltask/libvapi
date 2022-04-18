#ifndef __VTHREAD_H__
#define __VTHREAD_H__

#include <libvapi/vtypes.h>
//#include <libvapi/vload_categories.h>
#include <pthread.h>

#define VTHREAD_NAME_SIZE 16

#define VPRIO_INTERRUPT_HDLR           80 /* priority for user space interrupt handling, this context should only be active for a couple of milliseconds */
#define VPRIO_DO_OR_DIE                65 /* priority for code that should be executed, otherwise service is critically impacted, a protocol that requires another
                                              packet does not fall in this category. Typical users are: LACP (if not ok = no traffic), RPF (if not ok = no power) */
#define VPRIO_PKT_HDLR_HIGH            55 /* priority (do or die) for packet handler of switch task (both RX and TX) */
#define VPRIO_DELAY_CRITICAL           50 /* priority for delay critical protocols: IGMP, time critical CFM sub protocols, etc. */
#define VPRIO_PKT_HDLR_MED             45 /* priority (delay critical) for packet handler of switch task (both RX and TX) */
#define VPRIO_DMS_NON_DELAY_CRIT       42 /* priority for DMS (OHM,extODB,etc) servicing non-delay critical protocols: DHCP, PPPoE, 8021.x, etc. */
#define VPRIO_NON_DELAY_CRITICAL       40 /* priority for non-delay critical protocols: DHCP, PPPoE, 8021.x, etc. */
#define VPRIO_PKT_HDLR_LOW             35 /* priority (non-delay critical) for packet handler of switch task (both RX and TX) */
#define VPRIO_COUNTER_COLLECTOR        32 /* priority for time-sensitive statistics counter collection from HW */
#define VPRIO_BASELINE                 30 /* base line priority to be used by most applications: DMS, ConfD, alarms, OAM, most protocols */
#define VPRIO_BACKGROUND               20 /* priority for background tasks: Tnd, PM counters, IPFIX, software upgrade, cpe mgnt */

/*! \file vthread.h
 *  \brief Interface definition for the threading interface of the libvapi platform.
 */

#ifdef __cplusplus
extern "C"
{
#endif

typedef pthread_t      vthread_id_t;
typedef pthread_key_t  vthread_key_t;

typedef enum thread_sched_pol {
    VTHREAD_SCHED_POL_ROUND_ROBIN = 0, /*!< RT priority with time slicing, advised to use this one */
    VTHREAD_SCHED_POL_FIFO, /*!< RT priority without time slicing, will not unschedule untill explicit yield happens */
    VTHREAD_COUNT
} vthread_sched_pol_t;

/*!
 * \brief   Create/start a thread.
 *
 * Create/start a thread with specified name and priority.
 *
 * \param  name         IN  Thread name
 * \param  prio         IN  Thread priority from RT priority range 1-99
 * \param  policy       IN  Thread scheduling policy
 * \param  create_cb    IN  Callback function called when thread starts
 * \param  arg          IN  Callback argument passed together with cb function
 * \param  thread_id    OUT Id of created thread
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_create_deprecated(const char name[VTHREAD_NAME_SIZE], int32_t prio, vthread_sched_pol_t policy, void *(*create_cb)(void *), void *arg, vthread_id_t *thread_id);

/*!
 * \brief   Create/start a thread.
 *
 * Create/start a thread with specified name and load category.
 *
 * \param  name         IN  Thread name
 * \param  category     IN  Thread load category defined in <libvapi/vload_categories.h>
 * \param  policy       IN  Thread scheduling policy
 * \param  create_cb    IN  Callback function called when thread starts
 * \param  arg          IN  Callback argument passed together with cb function
 * \param  thread_id    OUT Id of created thread
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_create(const char name[VTHREAD_NAME_SIZE], vthread_sched_pol_t policy, void *(*create_cb)(void *), void *arg, vthread_id_t *thread_id);

/*!
 * \brief   Create/start a thread having an event loop.
 *
 * Create/start a thread with specified name and priority, running an event loop.
 *
 * \param  name         IN  Thread name
 * \param  prio         IN  Thread priority from RT priority range 1-99
 * \param  policy       IN  Thread scheduling policy
 * \param  init_cb      IN  Init callback function, where minimum one event must be initialized in
 *                          the event loop. After returning from this function, thread will be started.
 *                          If no events were initialized, thread will do nothing and exit.
 * \param  arg          IN  Callback argument passed together with cb function
 * \param  thread_id    OUT Id of created thread
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_createloop_deprecated(const char name[VTHREAD_NAME_SIZE], int32_t prio, vthread_sched_pol_t policy, void *(*init_cb)(void *), void *arg, vthread_id_t *thread_id);


/*!
 * \brief   Create/start a thread having an event loop.
 *
 * Create/start a thread with specified name and priority, running an event loop.
 *
 * \param  name         IN  Thread name
 * \param  category     IN  Thread load category defined in <libvapi/vload_categories.h>
 * \param  policy       IN  Thread scheduling policy
 * \param  init_cb      IN  Init callback function, where minimum one event must be initialized in
 *                          the event loop. After returning from this function, thread will be started.
 *                          If no events were initialized, thread will do nothing and exit.
 * \param  arg          IN  Callback argument passed together with cb function
 * \param  arg          IN  Number of event priorities that must be supported
 * \param  thread_id    OUT Id of created thread
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_createloop(const char name[VTHREAD_NAME_SIZE], vthread_sched_pol_t policy, void *(*init_cb)(void *), void *arg, vthread_id_t *thread_id);
int vthread_createloop_with_event_prio(const char name[VTHREAD_NAME_SIZE], vthread_sched_pol_t policy, void *(*init_cb)(void *), void *arg, int max_event_prio, vthread_id_t *thread_id);


/*!
 * \brief   Terminate thread.
 *
 * \param  thread_id    IN  Thread id
 */
void vthread_exit(vthread_id_t thread_id);


/*!
 * \brief   Join with a terminated thread.
 *
 * \param  thread_id    IN  Thread id
 */
void vthread_join(vthread_id_t thread_id);


/*!
 * \brief   Check if thread is running.
 *
 * \param  thread_id    IN  Thread id
 *
 * \return 1 in case thread is running, 0 if not
 */
int vthread_isrunning(vthread_id_t thread_id);


/*!
 * \brief Check if thread with event loop
 *
 * Check if calling thread is thread running an event loop
 *
 * \return 1 in case of event loop thread, 0 in case not.
 */
int vthread_isloop(void);


/*!
 * \brief   Retrieve the thread id based on specified name.
 *
 * \param  name         IN  Thread name
 * \param  thread_id    OUT Thread id
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_getid(const char name[VTHREAD_NAME_SIZE], vthread_id_t *thread_id);


/*!
 * \brief   Retrieve the thread name based on specified thread Id.
 *
 * \param  thread_id    IN  Thread id
 * \param  name         OUT Thread name
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_getname(vthread_id_t thread_id, char name[VTHREAD_NAME_SIZE]);


/*!
 * \brief   Retrieve the thread id of the calling thread.
 *
 * \param  thread_id    OUT  Thread id
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_getself(vthread_id_t *thread_id);


/*!
 * \brief   Retrieve the thread name of the calling thread.
 *
 * \return  Pointer the name of the calling thread
 */
const char *vthread_getselfname(void);


/*!
 * \brief   Create thread-specific data key.
 *
 * \param  key    OUT  Thread key
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_createkey(vthread_key_t *key);


/*!
 * \brief   Delete thread-specific data key.
 *
 * \param  key    IN  Thread key
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_deletekey(vthread_key_t key);


/*!
 * \brief   Set thread-specific data associated with key.
 *
 * \param  key    IN  Thread key
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_setspecific(vthread_key_t key, const void *value);


/*!
 * \brief   Get thread-specific data associated with key.
 *
 * \param  key    IN  Thread key
 *
 * \return 0 in case of success, -1 in case of error
 */
void *vthread_getspecific(vthread_key_t key);

/*!
 * \brief   Get priority (realtime) of current thread.
 *
 * \param  prio    OUT  RT priority (0-100)
 *
 * \return 0 in case of success, -1 in case of error
 */
int vthread_getprio_self(int *prio);

#ifdef __cplusplus
}
#endif

#endif /* !defined(_VTHREAD_H_) */
