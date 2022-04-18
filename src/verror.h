#ifndef __VERROR_H__
#define __VERROR_H__

#include <libvapi/vlog.h>
#include "bufprintf.h"

#define VERROR_MAX_RECORD_SIZE 2048 // maximum size of a formatted error record

/*!
 * \brief   Prototype for an error data formatter.
 *
 * Formatted output will be put inside the error record.
 *
 * \param   buf      OUT  Buffer to write formatted output to
 * \param   data     IN   Error data
 * \param   datalen  IN   Error data len
 */

typedef void (*verror_data_formatter_t)
  (buffer_t *buf, const char *data, size_t datalen);

/*!
 * \brief   Format an error record from a string message
 *
 * \param   output OUT Buffer to write error record to
 * \param   level  IN  Error level
 * \param   app    IN  Application name
 * \param   file   IN  Name of the source file where the error occurred
 * \param   line   IN  Source line number where the error occurred
 * \param   format IN  Error message format string
 */

int verror_str(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level, unsigned long type,
               const char *app, const char *file, int line,
               int async_signal_safe, const char *format, ...) __attribute__((format(printf, 8, 9)));

int verror_vstr(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level, unsigned long type,
                const char *app, const char *file, int line,
                int async_signal_safe, const char *format, va_list arg) __attribute__((format(printf, 8, 0)));

/*!
 * \brief   Format an error record with hexdump of binary data
 *
 * \param   output  OUT Buffer to write error record to
 * \param   level   IN  Error level
 * \param   app     IN  Application name
 * \param   file    IN  Name of the source file where the error occurred
 * \param   line    IN  Source line number where the error occurred
 * \param   data    IN  Error data
 * \param   datalen IN  Length of error data
 */

int verror_hexdump(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level,
                   const char *app, const char *file, int line,
                   int async_signal_safe, const char *data, size_t datalen);

/**
 * \brief   Format an error record with a custom data formatter
 *
 * \param   output    OUT Buffer to write error record to
 * \param   level     IN  Error level
 * \param   app       IN  Application name
 * \param   file      IN  Name of the source file where the error occurred
 * \param   line      IN  Source line number where the error occurred
 * \param   data      IN  Error data
 * \param   datalen   IN  Length of error data
 * \param   formatter IN  Error data formatter
 */

int verror_custom(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level,
                  const char *app, const char *file, int line,
                  int async_signal_safe, const char *data, size_t datalen,
                  verror_data_formatter_t formatter);

#endif
