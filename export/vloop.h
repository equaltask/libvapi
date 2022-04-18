#ifndef __VLOOP__
#define __VLOOP__

#include <libvapi/vlog.h>
#include <libvapi/vtypes.h>

/*! \file vloop.h
 *  \brief Main event loop to be used by application code
 *
 *  This is the interface towards the applications, so not the internal one, no file descriptors are returned,
 *  as they are immmediatly added to the internal eventloop
 *
 *
 *  structure of the vloop library's 'hidden' main:
 *
 *  \code{.c}
 *  int main (int argc, char* argv[])
 *  {
 *      int ret;
 *
 *      <internal vloop initialisation>
 *
 *      if((ret = vloop_app_init(argc, argv)) != YAPI_SUCCESS)
 *          return ret;
 *
 *      ret = start_loop(); //will only return if loop has nothing todo anymore
 *
 *      return ret;
 *  }
 *  \endcode
 *
 *  \example vloop_example.c
 *
 */

#define MAX_APP_NAME_LEN 64

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *vloop_event_object_t;

typedef struct vloop_event_handle_ {
    vloop_event_object_t ytimer_obj;
    struct event *read_handle;
    struct event *write_handle;
    vlog_opentracing_context_ptr jsonopentracer_context;
    int jsonopentracer_context_size;
} vloop_event_handle, *vloop_event_handle_t;

typedef int (*vloop_event_cb)(int fd, vloop_event_handle_t event_handle, void *ctx);

/** fd event types */
typedef enum vloop_fd_event {
    VLOOP_FD_READ, /**< trigger cb when fd is readable */
    VLOOP_FD_WRITE, /**< trigger cb when fd is writable */
    VLOOP_FD_READ_AND_WRITE, /**< trigger cb when fd is readable/writable */
} vloop_fd_event_t;

typedef enum instance_type {
    INST_TYPE_NONE,
    INST_TYPE_NT,
    INST_TYPE_LT,
    INST_TYPE_IWF,
    INST_TYPE_VCE,
    INST_TYPE_ONU,
    INST_TYPE_OLT
} instance_type_t;

typedef struct instance_id {
    instance_type_t type;
    uint16_t id;
} instance_id_t;

/*! \brief Entry point of the application.
 *
 * This function is meant to be overridden by the application. It is called
 * after libvapi has done its initialization and after the global constructors
 * have been called.
 *
 * TODO define weak macro
 *
 * \return YAPI_SUCCESS in case of success,
 *         YAPI_FAILURE in case of unrecoverable error
 */
int vloop_app_init(int argc, char *argv[]) __attribute__((weak));

/*! \brief add a file descriptor directly to the eventloop
 *
 * read_cb/write_cb will be called when fd is readable/writable after cb has been enabled via vloop_enable_cb
 *
 * \param fd            IN fd to listen on
 * \param event_type    IN fd event types to receive callbacks for
 * \param read_cb       IN cb to call on VLOOP_FD_READ event
 * \param write_cb      IN cb to call on VLOOP_FD_WRITE event
 * \param arg           IN user's context, provided back with every callback
 * \return returns an vloop_event_handle_t in case of success, to be used to remove fd again, NULL in case of unrecoverable error
 * \sa vloop_enable_cb, vloop_disable_cb
 */
vloop_event_handle_t vloop_add_fd(int fd, vloop_fd_event_t event_type, vloop_event_cb read_cb, vloop_event_cb write_cb, void *ctx);
vloop_event_handle_t vloop_add_fd_ot(int fd, vloop_fd_event_t event_type, vloop_event_cb read_cb, vloop_event_cb write_cb, void *ctx, vlog_opentracing_context_ptr jsonopentracer_context,
                                     int jsonopentracer_context_size);

/*! \brief enable cb of and event directly added to the eventloop
 *
 * read_cb/write_cb will be called when cb has been enabled via vloop_enable_cb
 *
 * \param event_handle  IN handle to the event for which you want to enable an event
 * \param event_type    IN fd event types to receive callbacks for
 * \return 0 in case of success, -1 in case of error
 * \sa vloop_add_fd, vloop_disable_cb
 */
int vloop_enable_cb(vloop_event_handle_t vloop_event_handle, vloop_fd_event_t event_type);

/*! \brief disable cb of and event directly added to the eventloop
 *
 * read_cb/write_cb will be no longer be called for given type
 *
 * \param event_handle  IN handle to the event for which you want to disable an event
 * \param event_type    IN fd event types to receive callbacks for
 * \return 0 in case of success, -1 in case of error
 * \sa vloop_add_fd, vloop_enable_cb
 */
int vloop_disable_cb(vloop_event_handle_t vloop_event_handle, vloop_fd_event_t event_type);

/*! \brief remove file descriptor from the eventloop
 *
 * \param vloop_event_handle      IN vloop_event_handle, the one got when fd was added to eventlopo
 * \return returns 0 in case of success, -1 in case of unrecoverable error
 */
int vloop_remove_fd(vloop_event_handle_t vloop_event_handle);

/*! \brief Initialize eventloop with number of event priorities to be supported
 *
 * Should be called before the first call to event_base_dispatch, so to be done by
 * application in vloop_app_init callback.
 *
 * \param max_prio IN max nbr of event priorities to be supported
 * \return returns 0 in case of success, -1 in case of unrecoverable error
 * \sa ythread_createloop_with_event_prio
 */
int vloop_set_max_prio(int max_prio);

/*! \brief Set read priority for this vloop event handle
 *
 * Priority goes from 0 to (max_prio-1) where 0 is the highest
 * when not called a default prio is used equal to max_prio/2
 *
 * \param vloop_event_handle  IN handle to the event for which to set priority
 * \param prio_read           IN read priority to be set
 * \return 0 in case of success, -1 in case of error
 */
int vloop_set_read_event_prio(vloop_event_handle_t vloop_event_handle, int prio_read);

/*! \brief Set write priority for this vloop event handle
 *
 * Priority goes from 0 to (max_prio-1) where 0 is the highest
 * when not called a default prio is used equal to max_prio/2
 *
 * \param vloop_event_handle  IN handle to the event for which to set priority
 * \param prio_write          IN write priority to be set
 * \return 0 in case of success, -1 in case of error
 */
int vloop_set_write_event_prio(vloop_event_handle_t vloop_event_handle, int prio_write);

/*!
 * \brief Get the name of the ISAM build (e.g. NR66AA56.507)
 *
 * The build name comes from the path of the program being run.
 * E.g. /mnt/isam/NR66AA56.507/isam_nt_app -> NR66AA56.507
 *
 * \return Pointer to a string representing the build name
 */
const char *vloop_get_build_name(void);

const char *vloop_get_json_config_file(void);

/*!
 * \brief Get the version of the ISAM build (e.g. 56.507)
 *
 * The build version comes from the path of the program being run.
 * E.g. /mnt/isam/NR66AA56.507/isam_nt_app -> 56.507
 *
 * \return Pointer to a string representing the build version
 */
const char *vloop_get_build_version(void);

/*!
 * \brief Get the name of the application executable
 *
 * The executable name comes from the path of the program being run.
 * E.g. /mnt/isam/NR66AA54.507/isam_nt_app -> isam_nt_app
 *
 * \return Pointer to a string representing the executable name
 */
const char *vloop_get_executable_name(void);

/*!
 * \brief Get the application name
 *
 * Application name comes from command line parameter --app-name
 * or by default is equal to argv[0].
 *
 * \return Pointer to a string representing the app name
 */
const char *vloop_get_application_name(void);

/*!
 * \brief Get the application instance id
 *
 * Application instance id string comes from command line parameter --app-instance
 *
 * \return Pointer to a string representing the app instance id
 */
const char *vloop_get_app_instance_id(void);

/*!
 * \brief Convert application instance string to instance_id_t
 *
 * \param inst_type_str  IN  instance string
 * \param inst_id        OUT pointer to instance_id_t
 * Application instance id string comes from command line parameter --app-instance
 *
 * \return returns 0 in case of success, -1 in case of error
 */
int vloop_app_instance_from_string(const char *inst_type_str, instance_id_t *inst_id);

/*!
 * \brief libvapi library main function
 * This function should typically be called directly by the main function
 * of the program with the argc and argv given by the parent process.
 * \param argc Number of command-line arguments
 * \param argv List of command-line arguments
 * \return YAPI_SUCCESS in case the program terminates successfully
 *         YAPI_FAILURE in case the program encounters a failure
 */
int vloop_main(int argc, char *argv[]);

/*!
 * \brief Call the constructors of global objects
 * This function should be implemented in application code.
 */
void vloop_call_constructors(void);


typedef void *vloop_action_t;
typedef void (*vloop_action_cb)(vloop_action_t action, void *ctxt);

/*!
 * \brief Create an action event and insert it in the action list for the current thread.
 * This function can be called when processing needs to be postponed till the current event
 * is fully processed. Zero timers can be used as well but have more overhead.
 * The event needs to be cleaned up by calling vloop_action_delete in the callback or at any
 * other moment in  time.
 * The passed callback will only be called once before executing the next cycle in the eventloop.
 * \param cb Callback function that will be invoked.
 * \param ctxt User context passed transparantly to the callback function.
 * \return handle of the created action.
 */
vloop_action_t vloop_action_create(vloop_action_cb cb, void *ctxt);

/*!
 * \brief Delete an earlier created action event.
 * Deleting a pending action will remove it from the action list and release the memory.
 * When the delete is called in the context of the callback only the memory is released.
 * \param action handle to be delete.
 */
void vloop_action_delete(vloop_action_t action);

/*!
 * \brief Return the event_base of the current thread.
 * This is to be used only in very specific cases where interaction with libevent is needed
 * for example in test applications. Normal libvapi application should use the libvapi interface to
 * interact with the platform.
 */
struct event_base *vloop_get_base(void);

#if defined(__cplusplus)
};
#endif

#endif
