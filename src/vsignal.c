#ifndef _GNU_SOURCE
#define _GNU_SOURCE // pipe2
#endif

#include <errno.h> // errno
#include <limits.h> // PIPE_BUF
#include <fcntl.h> // O_CLOEXEC, O_NONBLOCK
#include <unistd.h> // read, write
#include <string.h> // strerror
#include <fenv.h> // feenableexcept
#include <stdlib.h> // exit
#include <execinfo.h> // backtrace
#include <event2/event.h>

#include <libvapi/vlog.h>
#include <libvapi/vmem.h>
#include <libvapi/vmutex.h>
//#include <libvapi/ycov.h>
//#include <libvapi/ytnd.h>
#include <libvapi/vsystem.h>
#include <libvapi/utility.h>

#include "vsignal.h"
#include "verror.h"
#include "vloop_internal.h"
#include "bufprintf.h"
#include "vlog_core.h"
#include "bufprintf.h"
//#include "prettyprint.h"
#include "vlog_vapi.h"
#include "vtnd_console.h"

/* We divide signals into two groups: fatal and non-fatal signals. We define a
 * fatal signal as a signal for which we do not return from the signal handler.
 * The C standard states that returning from a signal handler that results from
 * a computational exception (SIGFPE, SIGILL, SIGSEGV) is undefined behavior.
 * POSIX adds SIGBUS to the list. Here we add SIGABRT as it is meant to be used
 * by abort() which will terminate the process anyway if we return from the handler,
 * and SIGSYS which is meant to signal a bad system call (it is not used by Linux
 * though, and the behavior if we return from the handler does not seem to be
 * standardized, so the discussion is open).
 *
 * It results from the definition that fatal signals have to be treated
 * asynchronously (=immediatly) in the signal handler, after which the process
 * will terminate. Non-fatal signals can be treated synchronously (=later),
 * namely by generating an event in the signal handler, and treating it later
 * within the context of the event loop.
 *
 * Fatal signals:
 *   - An error record is generated inside the signal handler, unless the signal
 *     was expected (e.g. we explicitly called abort()).
 *   - Then the system default handler is called, which will terminate the process.
 *   - No user callback can be registered.
 *
 * Non-fatal signals:
 *   - If no user callback is registered, the system default handler is executed.
 *     This may terminate the process (see man 7 signal).
 *   - If a user callback is registered, the system default handler is _not_
 *     executed.
 *   - The user callback is called in the context of the event loop of the
 *     thread that registered the callback.
 *
 * Implementation: For non-fatal signals, the signal info is sent from the signal
 * handler context to the init thread context (thread that called ysignal_init)
 * using the self-pipe trick (in order to be async-signal-safe). The init thread
 * will generate one event for each registered user callback, and add it to the
 * event loop associated with this callback. This way each callback is called in
 * the context of the thread that registered it.
 *
 * Note: We do not support user-defined callbacks for fatal signals because
 * we do not go back to the event loop after such a signal (process will
 * terminate), so it is not possible to call the callbacks from the event loop
 * context. Calling them from signal context is also not doable because we
 * cannot access any shared structure containing the list of registered
 * callbacks (this would not be async-signal-safe).
 *
 * Note: For each non-fatal signal, we allow zero or one registered callback
 * in the process (not more). This is in line with the Unix approach that says
 * a signal disposition is a per-process attribute. We also do not make any
 * difference between signals directed to the process or to a thread. If thread
 * T1 registers a callback for S, and S is caught, the callback will be
 * executed in the context of T1, regardless of whether S was directed to the
 * whole process, to T1 or to any other thread.
 */
static const int fatal_signals[] = { SIGFPE, SIGILL, SIGSEGV, SIGBUS, SIGABRT, SIGSYS };

/* If we call abort(), we will generate an error record upfront, so it
 * does not need to be done in the signal handler. The variable has to be
 * volatile sig_atomic_t to be safely read in the signal handler. */
static volatile sig_atomic_t abort_expected = 0;

/* Pipe used to communicate from the non-fatal signal handler
 * to the init thread execution context (self-pipe trick) */
static int selfpipe[2] = {-1, -1};

/* Here is the list of user-registered callbacks. Each callback is registered
 * for a given non-fatal signal. To each callback we associate the event loop
 * of the thread that registered this callback.
 */
typedef struct {
    struct event_base *evbase; // event loop of the thread that registered the callback
    vsignal_cb_t cb;           // user callback to be called when signo is caught
    void *ctxt;                // user context to be given to cb
} user_info_t;

user_info_t users[NSIG];
vthread_mutex_t mtx; // mutex to protect the list

/* The following structure contains the necessary information to call a user
 * callback from the event loop of the thread that registered this callback.
 */
typedef struct {
    siginfo_t siginfo;    // signal information
    vsignal_cb_t user_cb; // user callback to be called
    void *user_ctxt;      // user context to be given to cb
    struct event *event;  // libevent event used
} signal_ctxt_t;

static int is_fatal(int signo)
{
    unsigned int i;

    for (i = 0 ; i < DIM(fatal_signals) ; i++)
        if (fatal_signals[i] == signo)
            return 1;

    return 0;
}

static void prettyprint_siginfo_and_ucontext(buffer_t *buf, const char *data, size_t datalen)
{
    void **siginfo_and_uctxt = (void **) data;
    siginfo_t *si = siginfo_and_uctxt[0];
    ucontext_t *ucontext = siginfo_and_uctxt[1];

    bufprintf_asyncsignalsafe(buf, "\nThe following signal was received:\n");
    //prettyprint_siginfo(buf, si);

    bufprintf_asyncsignalsafe(buf, "- Architecture specific information:\n");
    //prettyprint_ucontext(buf, ucontext);
}

static void prettyprint_siginfo_wrap(buffer_t *buf, const char *data, size_t datalen)
{
    siginfo_t *si = (siginfo_t *) data;

    bufprintf_asyncsignalsafe(buf, "\nThe following signal was received:\n");
    //prettyprint_siginfo(buf, si);
}

static void asyncsignalsafe_fatal_error_record(const char *file, int line, siginfo_t *si, void *ucontext)
{
    char error_record[VERROR_MAX_RECORD_SIZE];
    const void *siginfo_and_ucontext[2] = {si, ucontext};

    verror_custom(error_record, VLOG_CRITICAL, "ysignal", file, line, 1,
                  (const char *)siginfo_and_ucontext, 0, prettyprint_siginfo_and_ucontext);

    vlog_print_error_asyncsignalsafe(error_record);
}

void ylegacy_report_exception(int signum, siginfo_t *si, void *ucontext)
{
    asyncsignalsafe_fatal_error_record(__FILE__, __LINE__, si, ucontext);
}

static void asyncsignalsafe_nonfatal_error_record(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    char error_record[VERROR_MAX_RECORD_SIZE];

    va_start(args, fmt);
    verror_vstr(error_record, VLOG_ERROR, 0, "ysignal", file, line, 1, fmt, args);
    va_end(args);

    vlog_print_error_asyncsignalsafe(error_record);
}

static void fatal_error_record(const char *file, int line, siginfo_t *si)
{
    char error_record[VERROR_MAX_RECORD_SIZE];

    verror_custom(error_record, VLOG_ERROR, "ysignal", file, line, 0,
                  (const char *)si, 0, prettyprint_siginfo_wrap);

    vlog_print_error_asyncsignalsafe(error_record);
}

static void fatal_signal_handler(int signum, siginfo_t *si, void *ucontext)
{
    /* Generate error record. If SIGABRT was raised via ysignal_abort,
     * the error record was already printed there */
    if (!(signum == SIGABRT && abort_expected))
        asyncsignalsafe_fatal_error_record(__FILE__, __LINE__, si, ucontext);

    vsystem_stop_childs();

    /* Call system default handler (restored by SA_RESETHAND) which
     * will terminate the process. This call will never return. */
    raise(signum);
}

static void nonfatal_signal_handler(int signum, siginfo_t *si, void *ucontext)
{
    int rc;
    int saved_errno = errno;

    if (selfpipe[1] == -1) {
        asyncsignalsafe_nonfatal_error_record(__FILE__, __LINE__, "Self-pipe is not initialized: signum=%d", signum);
        errno = saved_errno;
        return;
    }

    // only a write of less than PIPE_BUF is guaranteed to be atomic
    STATIC_ASSERT(sizeof(*si) <= PIPE_BUF);

    rc = write(selfpipe[1], si, sizeof(*si));

    if (rc != sizeof(*si)) {
        asyncsignalsafe_nonfatal_error_record(__FILE__, __LINE__,
                                              "Failed to write to self-pipe: signum=%d errno=%d (%s)",
                                              signum, errno, strerror(errno));
    }

    errno = saved_errno;
}

static void install_fatal_signal_handler(int signo)
{
    struct sigaction action;

    action.sa_sigaction = fatal_signal_handler;

    // should we use sigfillset to block all signals while we handle one?
    sigemptyset(&action.sa_mask);

    action.sa_flags  = SA_SIGINFO;
    action.sa_flags |= SA_RESETHAND; // reset default handler before executing ours

    sigaction(signo, &action, NULL);
}

static void install_nonfatal_signal_handler(int signo)
{
    struct sigaction action;

    action.sa_sigaction = nonfatal_signal_handler;

    // should we use sigfillset to block all signals while we handle one?
    sigemptyset(&action.sa_mask);

    action.sa_flags  = SA_SIGINFO;

    sigaction(signo, &action, NULL);
}

static void install_all_fatal_signal_handlers(void)
{
    unsigned int i;

    for (i = 0 ; i < DIM(fatal_signals) ; i++)
        install_fatal_signal_handler(fatal_signals[i]);
}

// this function is executed within the user thread (thread that registered the user callback)
// it is responsible for calling the user callback
static void signal_cb(evutil_socket_t fd, short events, void *ctxt)
{
    signal_ctxt_t *sig_ctxt = (signal_ctxt_t *)ctxt;
    sig_ctxt->user_cb(&sig_ctxt->siginfo, sig_ctxt->user_ctxt);
    event_free(sig_ctxt->event);
    vmem_free(vmem_alloc_default(), sig_ctxt);
}

// this function is executed within the init thread (thread that called ysignal_init)
// it sends one event to a user thread
static void send_signal_event(struct event_base *base, vsignal_cb_t user_cb, siginfo_t *siginfo, void *user_ctxt)
{
    int rc;
    static struct timeval zero = {0, 0};
    signal_ctxt_t *ctxt = vmem_malloc(vmem_alloc_default(), sizeof(signal_ctxt_t));
    ctxt->user_cb = user_cb;
    ctxt->event = evtimer_new(base, signal_cb, ctxt);
    ctxt->user_ctxt = user_ctxt;
    memcpy(&ctxt->siginfo, siginfo, sizeof(siginfo_t));
    rc = evtimer_add(ctxt->event, &zero);
    if (rc == -1) {
        vapi_error("Failed to add signal event");
    }
}

// this function is executed within the init thread
// it reads one siginfo from the self-pipe
// then it loops over registered user callbacks and sends one event per callback
static void selfpipe_cb(evutil_socket_t fd, short events, void *ctx)
{
    int rc;
    siginfo_t siginfo;
    user_info_t *user;

    rc = read(fd, &siginfo, sizeof(siginfo));
    if (rc != sizeof(siginfo)) {
        vapi_error("Failed to read siginfo from self-pipe: errno=%d (%s)",
                   errno, strerror(errno));
        return;
    }

    if (siginfo.si_signo < 0 || siginfo.si_signo >= NSIG) {
        vapi_error("Invalid signal number %d received on self-pipe", siginfo.si_signo);
        return;
    }

    rc = vmutex_lock(&mtx);
    if (rc != 0)
        return;

    user = &users[siginfo.si_signo];
    if (user->cb != NULL)
        send_signal_event(user->evbase, user->cb, &siginfo, user->ctxt);

    vmutex_unlock(&mtx);
}

static int create_selfpipe(void)
{
    int rc;
    struct event *selfpipe_event;

    // XXX O_DIRECT guarantees that we read exactly sizeof(siginfo_t) at a time, but requires Linux >= 3.4
    // O_NONBLOCK to avoid any possible deadlock if the pipe is full
    rc = pipe2(selfpipe, O_CLOEXEC | O_NONBLOCK);
    if (rc == -1) {
        vapi_error("Failed to create self-pipe (errno=%d: %s)", errno, strerror(errno));
        return -1;
    }

    selfpipe_event = event_new(vloop_get_base(), selfpipe[0], EV_READ | EV_PERSIST, selfpipe_cb, NULL);
    rc = event_add(selfpipe_event, NULL);
    if (rc == -1) {
        vapi_error("Failed to add self-pipe event");
        return -1;
    }

    return 0;
}

static void delayed_exit()
{
    const char *delay_ms_str;
    delay_ms_str = getenv("ISAM_APP_DELAY_EXIT");
    if (delay_ms_str == NULL)
        return;

    int delay_ms = atoi(delay_ms_str);
    if (delay_ms > 0)
        usleep(delay_ms * 1000);
}

static void do_nothing(siginfo_t *si, void *ctxt)
{
}

static void log_error_and_call_system_default_handler(siginfo_t *si, void *ctxt)
{
    delayed_exit();

    vsystem_stop_childs();

    fatal_error_record(__FILE__, __LINE__, si);

    //make sure stdin is restored as before
    vtnd_console_reset();

    signal(si->si_signo, SIG_DFL);
    raise(si->si_signo);
}

static void install_default_nonfatal_signals_handlers(void)
{
    /* Avoid termination upon SIGPIPE (see changeset 1a939ebb335f) */
    vsignal_register(SIGPIPE, do_nothing, NULL);

    /* For non-fatal signals for which the system default action is to
     * terminate (see 'man 7 signal'), we install our own handlers to
     * make sure we print an error message before terminating. */
    vsignal_register(SIGHUP, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGTERM, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGINT, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGQUIT, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGTRAP, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGIO, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGALRM, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGUSR1, log_error_and_call_system_default_handler, NULL);
    vsignal_register(SIGUSR2, log_error_and_call_system_default_handler, NULL);
}

static void enable_floating_point_exceptions(void)
{
    /* See changesets 00e6d4b3be9f and 6b26b48fabf1 for background.
     * Enable floating point exceptions
     * On some (all?) systems (seen on CATAN), a divide-by-zero error was not
     * detected, unless floating point exceptions where explicitly enabled.
     */
    feenableexcept(FE_INVALID   |
                   FE_DIVBYZERO |
                   FE_OVERFLOW  |
                   FE_UNDERFLOW);
}

int vsignal_init(void)
{
    /* Make sure we load the backtrace library already here to make it safe to call it from signal handler context. */
    /* see https://stackoverflow.com/questions/29982643/how-to-get-proper-backtrace-in-process-signal-handler-armv7-uclibc for a detailed description */
    void *dummy = NULL;
    backtrace(&dummy, 1);

    vmutex_create(&mtx);
    memset(users, 0, sizeof(users));

    if (create_selfpipe() == -1)
        return -1;

    install_all_fatal_signal_handlers();
    install_default_nonfatal_signals_handlers();

    enable_floating_point_exceptions();

    return 0;
}

int vsignal_register(int signo, vsignal_cb_t cb, void *ctxt)
{
    user_info_t *user;

    if (is_fatal(signo)) {
        vapi_error("User callbacks for fatal signals are not supported (%s)", strsignal(signo));
        return -1;
    }

    int ret = vmutex_lock(&mtx);
    if (ret != 0)
        return -1;

    user = &users[signo];
    if (user->cb != NULL) {
        vmutex_unlock(&mtx);
        vapi_warning("Cannot register callback for signal %d: already registered", signo);
        return -1;
    }

    user->cb = cb;
    user->ctxt = ctxt;
    user->evbase = vloop_get_base();

    install_nonfatal_signal_handler(signo);

    vmutex_unlock(&mtx);

    return signo;
}

int vsignal_deregister(int signo)
{
    int ret = vmutex_lock(&mtx);
    if (ret != 0)
        return -1;

    users[signo].evbase = NULL;
    users[signo].cb = NULL;
    users[signo].ctxt = NULL;

    vmutex_unlock(&mtx);

    return 0;
}

int vsignal_reset_default(int signo)
{
    vsignal_deregister(signo);

    // restore default handler
    vsignal_register(signo, log_error_and_call_system_default_handler, NULL);

    return 0;
}

int vsignal_raise(int signo)
{
    if (is_fatal(signo)) {
        vapi_error("User is trying to raise a fatal signal (%s)", strsignal(signo));
        return -1;
    }

    return raise(signo);
}

void vsignal_abort(void)
{
    abort_expected = 1;
    vapi_critical("Abort requested, process will terminate"); // print error record
    raise(SIGABRT);
    exit(-1); // never executed, this is only to comply with the __noreturn__ attribute of ysignal_abort()
}

/* Overwrite abort() to allow error record generation
 * when e.g. a third-party library calls abort(). */
void abort(void)
{
    vsignal_abort();
}
