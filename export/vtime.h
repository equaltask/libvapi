// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __VTIME_H__
#define __VTIME_H__

#include <time.h>
#include <sys/time.h>

/*! \file vtime.h
 *  \brief Interface definition for the time managament services of the yAPI platform.
 *  \example vtime_example.c
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Used in YlogFormat to return the size the tag TIME
 * Number corresponding to the total size of the tag TIME expanded.
 * The time TAG is composed as follow:
 * "%d/%m/%Y-%H:%M:%S" equal to 19 characters + ".%06ld" equal to 7 characters
 *  Once expanded, the total is always equal to 26 characters
 */
#define ETIME_TIME_DATE_STR_LEN 26

typedef enum {
    TIME_INTVAL_1MIN,
    TIME_INTVAL_5MIN,
    TIME_INTVAL_15MIN,
    TIME_INTVAL_1HR,
    TIME_INTVAL_24HR,
    TIME_INTVAL_MAX
} vtime_interval_t;

/*!
 * \brief   Get a timestamp relative to the start of the process.
 *
 * \param   time        OUT
 * \return  0 on success, -1 on error
 * \sa      ytime_get_wallclock
 */
int vtime_get_time(struct timespec *time);

/*!
 * \brief   Get the wallclock time.
 *
 * \param   time        OUT
 * \return  0 on success, -1 on error
 */
int vtime_get_wallclock(struct timespec *time);

/*!
 * \brief   Get the UTC time (with struct tm output).
 *
 * \param   time        OUT
 * \return  0 on success, -1 on error
 */
int vtime_get_utc_tm(struct tm *time);

/*!
 * \brief   Convert a unix epoch value to the UTC time (with struct tm output).
 *
 * \param   timep       IN
 * \param   time        OUT
 * \return  0 on success, -1 on error
 */
int vtime_convert_utc_tm(time_t timep, struct tm *time);

/*!
 * \brief   Get the wallclock time (with struct tm output).
 *
 * \param   time        OUT
 * \return  0 on success, -1 on error
 */
int vtime_get_wallclock_tm(struct tm *time);

/*!
 * \brief   Convert a unix epoch value to the wallclock time (with struct tm output).
 *
 * \param   timep       IN
 * \param   time        OUT
 * \return  0 on success, -1 on error
 */
int vtime_convert_wallclock_tm(time_t timep, struct tm *time);

/*!
 * \brief   Convert the wallclock time (with struct tm output) to a unix epoch value.
 *
 * \param   time        IN
 * \param   timep       OUT
 * \return  0 on success, -1 on error
 */
int vtime_convert_wallclock_time_t(struct tm *time, time_t *timep);

/*!
 * \brief   Set the wallclock time (with struct tm input).
 *
 * \param   time        IN
 * \return  0 on success, -1 on error
 */
int vtime_set_wallclock_tm(struct tm *time);

/*!
 * \brief   Get the wallclock time (with string output (the same one you would get out of ytime_time_date_str)).
 *
 * \param   buf        IN/OUT  Buffer to store date/time in string format
 * \param   len        IN   Length of the buffer
 * \return  0 on success, -1 on error
 */
int vtime_get_wallclock_string(char *buf, int len);

/*!
 * \brief   Get the time since the last boot up.
 *
 * \param   time        OUT
 * \return  0 on success, -1 on error
 */
int vtime_get_uptime(struct timespec *time);

/*!
 * \brief   Get the time rounded up till the nearest specified interval.
 *
 * \param   interval    IN
 * \param   time        OUT
 * \return  0 on success, -1 on error
 */
int vtime_get_next_interval_time(vtime_interval_t interval, struct timespec *time);

/*!
 * \brief   Convert date/time to string.
 *
 * Converts timespec structure to string format %d/%m/%Y %H:%M:%S.%09ld.
 * \param   buf        IN/OUT  Buffer to store date/time in string format
 * \param   len        IN   Length of the buffer
 * \param   time       IN   Time to be converted
 * \return  0 on success, -1 on error.
 * \sa      ytime_get_wallclock
 */
int vtime_time_date_str(char *buf, int len, struct timespec *time);

/*!
 * \brief   Changes the current timezone name
 *
 * This changes the timezone FOR THE WHOLE SYSTEM. Applications that don't read the new timezone properly will probably fail
 * if they rely on time information to work properly. You should probably reboot the system after calling that function.
 *
 * \param   user_cb     IN  Callback which will get called when the process ends (failure or success).
 * \param   tz_name     IN  The timezone as a string (eg. "US/Pacific" or "Etc/GMT" ; see man tzset for more info).
 * \param   user_ctx    IN  User context which will get passed to the user callback.
 * \return  A yevent handle if succesfull, NULL on error.
 */
//eevent_t *vtime_set_timezone_name(yevent_cb_t user_cb, const char *tz_name, void *user_ctx);

/*!
 * \brief   Changes the current timezone offset
 *
 * This changes the timezone FOR THE WHOLE SYSTEM. Applications that don't read the new timezone properly will probably fail
 * if they rely on time information to work properly. You should probably reboot the system after calling that function.
 *
 * \param   user_cb     IN  Callback which will get called when the process ends (failure or success).
 * \param   tz_offset   IN  The timezone utc offset in hours
 * \param   user_ctx    IN  User context which will get passed to the user callback.
 * \return  A yevent handle if succesfull, NULL on error.
 */
//vevent_t *vtime_set_timezone_offset(yevent_cb_t user_cb, int tz_offset, void *user_ctx);

/*!
 * \brief   checks the timezone name is a name knowns
 *
 * \param   tz_name     IN  The timezone as a string (eg. "US/Pacific" ).
 * \return  eturn  0 on success, -1 on error
 */
int vtime_valid_timezone_name(const char *tz_name);

/*!
 * \brief   checks the timezone name is a name knowns
 *
 * \param   tz_offset
 * \return  eturn  0 on success, -1 on error
 */
int vtime_valid_timezone_offset(int tz_offset);

/*!
 * \brief Callback type for time update notification.
 *
 * \param delta     IN Time update delta
 * \param user_ctx  IN User context provided in ytime_register_update_cb().
 */
typedef void (*vtime_update_cb_t)(time_t delta, void *user_ctx);

/*!
 * \brief Register for time update notification.
 *
 * This function will register a callback function for notification of time updates.
 *
 * \param on_update     IN Time update callback.
 * \param user_ctx      IN Callback context.
 * \return 0 on success, -1 on failure
 */
int vtime_register_update_cb(vtime_update_cb_t on_update, void *user_ctx);

/*!
 * \brief Callback type for timezone update notification.
 *
 * \param tz_name   IN Updated timezone name
 * \param delta     IN Delta in time_t seconds compared to previous timezone
 * \param user_ctx  IN User context provided in ytime_register_timezone_cb().
 */
typedef void (*vtime_timezone_cb_t)(const char *tz_name, time_t delta, void *user_ctx);

/*!
 * \brief Register for timezone update notification.
 *
 * This function will register a callback function for notification of timezone updates.
 *
 * \param on_timezone_change   IN Timezone update callback.
 * \param user_ctx             IN Callback context.
 * \return 0 on success, -1 on failure
 */
int vtime_register_timezone_cb(vtime_timezone_cb_t on_timezone_change, void *user_ctx);

#ifdef __cplusplus
};
#endif

#endif // __VTIME_H__

