#ifndef __BUFPRINTF_H__
#define __BUFPRINTF_H__

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
  char  *data;      // pointer to a char array where we want to write things
  size_t available; // number of bytes available in this array
} buffer_t;

/*!
 * \brief   Initialize a buffer_t
 *
 * \param   buf    IN  Buffer to initialize
 * \param   output IN  Array where to write characters
 * \param   size   IN  Number of bytes available in output
 */

static inline void bufinit(buffer_t *buf, char *output, size_t size)
{
  buf->data      = output;
  buf->data[0]   = '\0';
  buf->available = size;
}

/*!
 * \brief   Write in a buffer_t from a format string
 *
 * \param   buf    IN  Buffer to write to
 * \param   format IN  Format string to write into buf
 *
 * \return  Number of characters actually written to the buffer
 *          (excluding terminating null byte)
 */

int bufprintf(buffer_t *buf, const char *format, ...) __attribute__((format(printf, 2, 3)));

/*!
 * \brief  Async-signal-safe version of bufprintf
 */
int bufprintf_asyncsignalsafe(buffer_t *buf, const char *format, ...) __attribute__((format(printf, 2, 3)));


int vsnprintf_asyncsignalsafe(char *buf, size_t size, const char *format, va_list args);
int snprintf_asyncsignalsafe(char *buf, size_t size, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif

