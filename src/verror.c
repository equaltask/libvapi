#include <string.h>
#include <stdio.h>
#include <execinfo.h>
#include <stdarg.h>
#include <ctype.h> // isgraph

#include <libvapi/vlog.h>
#include <libvapi/vtime.h>
#include <libvapi/vloop.h>
//#include <libvapi/vthread.h>

#include "verror.h"
#include "bufprintf.h"

#define MAX_CALLSTACK_DEPTH 30

/* The sequence number of error records is a process global (shared by all
 * threads). We should therefore protect it with a mutex. However this would
 * prevent us from accessing it from a signal handler (risk of deadlock).
 * Using sig_atomic_t would not guarantee that it is thread-safe. Anyway
 * this is just a harmless counter. */
static int seq_num = 0;

static void print_callstack(buffer_t *out_buf, int skip_count)
{
    int j, nptrs;
    void *buffer[MAX_CALLSTACK_DEPTH];

    nptrs = backtrace(buffer, MAX_CALLSTACK_DEPTH);

    bufprintf(out_buf, "\nCall stack :\n");

    for (j = skip_count; j < nptrs; j++) {
        bufprintf(out_buf, "  ip[%02d]      : 0x%08lx\n", j, (unsigned long)buffer[j]);
    }

    bufprintf(out_buf, "\n");
}

/* Translate to ascii len bytes from byte_ptr and put result in result_buf */
static void translate_to_ascii(unsigned char *byte_ptr,
                               unsigned len,
                               char *result_buf)
{
    while (len) {
        if (isgraph(*byte_ptr))
            *result_buf++ = *byte_ptr;
        else
            *result_buf++ = '.';
        byte_ptr++;
        len--;
    }
}

static void hexdump_formatter(buffer_t *out_buf, const char *data, size_t datalen)
{
    unsigned int i;
    unsigned long end_addr, dump_addr;
    char ascii_buf[17]; // ascii translation of 16 bytes
    char padding[50];   // padding for the last line
    const char *byte_ptr;

    bufprintf_asyncsignalsafe(out_buf, "error info       :\n\t");
    ascii_buf[16] = 0;
    for (i = 0; i < sizeof(padding); i++)
        padding[i] = ' ';
    dump_addr = (unsigned long)data;
    end_addr = dump_addr + datalen;
    byte_ptr = data;
    i = 0;

    /* Loop over bytes */
    while (dump_addr < end_addr) {
        /* At the beginning of a line, translate 16 bytes to ascii */
        if (i == 0)
            translate_to_ascii((unsigned char *) byte_ptr, 16, ascii_buf);

        /* Print current byte */
        bufprintf_asyncsignalsafe(out_buf, "%02x ", *byte_ptr++);
        i++;
        dump_addr++;

        /* At the end of a line, print ascii translation */
        if (i >= 16) {
            i = 0;
            bufprintf_asyncsignalsafe(out_buf, "  %s\n\t", ascii_buf);
        }
    }

    /* If we are in the middle of a line, print padding + remaining ascii translation */
    if (i > 0) {
        padding[49 - i * 3] = 0;
        ascii_buf[i] = 0;
        bufprintf_asyncsignalsafe(out_buf, "%s %s\n", padding, ascii_buf);
    }
}

static void string_formatter(buffer_t *out_buf, const char *data, size_t datalen)
{
    bufprintf_asyncsignalsafe(out_buf, "error info       : %s\n", data);
}

static int verror_format_record(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level, unsigned long type,
                                const char *app, const char *file, int line,
                                int async_signal_safe, const char *data, size_t datalen,
                                verror_data_formatter_t formatter)
{
    buffer_t out_buf;
    bufinit(&out_buf, output, VERROR_MAX_RECORD_SIZE);

    /* Format generic fields */
    bufprintf_asyncsignalsafe(&out_buf, "************* ERROR RECORD *************\n");
    bufprintf_asyncsignalsafe(&out_buf, "seq-number       : %08x\n", seq_num++);
    bufprintf_asyncsignalsafe(&out_buf, "application      : %s\n", vloop_get_application_name());
    bufprintf_asyncsignalsafe(&out_buf, "error level      : %s\n", vlog_level_to_str(level));
    if (type) {
        bufprintf_asyncsignalsafe(&out_buf, "error type       : %lx\n", type);
    }
    bufprintf_asyncsignalsafe(&out_buf, "user name        : %s\n", app);
    bufprintf_asyncsignalsafe(&out_buf, "file name        : %s\n", file);
    bufprintf_asyncsignalsafe(&out_buf, "line number      : %d\n", line);

    /* Format error-specific data */
    formatter(&out_buf, data, datalen);

    /* Format call stack */
    // Note: getting the backtrace is not async-signal-safe (see man 7 signal)
    if (!async_signal_safe)
        print_callstack(&out_buf, 0);

    bufprintf_asyncsignalsafe(&out_buf, "****************** END *****************\n");

    return 0;
}

int verror_str(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level, unsigned long type,
               const char *app, const char *file, int line,
               int async_signal_safe, const char *format, ...)
{
    int error = 0;
    va_list arg;
    va_start(arg, format);
    error = verror_vstr(output, level, type, app, file, line, async_signal_safe, format, arg);
    va_end(arg);
    return error;
}

int verror_vstr(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level, unsigned long type,
                const char *app, const char *file, int line,
                int async_signal_safe, const char *format, va_list arg)
{
    char format_buffer[400];
    int nc = vsnprintf(format_buffer, sizeof(format_buffer), format, arg);
    return verror_format_record(output, level, type, app, file, line, async_signal_safe, format_buffer, nc, string_formatter);
}

int verror_hexdump(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level,
                   const char *app, const char *file, int line,
                   int async_signal_safe, const char *data, size_t datalen)
{
    return verror_format_record(output, level, 0, app, file, line, async_signal_safe, data, datalen, hexdump_formatter);
}

int verror_custom(char output[VERROR_MAX_RECORD_SIZE], vlog_level_t level,
                  const char *app, const char *file, int line,
                  int async_signal_safe, const char *data, size_t datalen,
                  verror_data_formatter_t formatter)
{
    return verror_format_record(output, level, 0, app, file, line, async_signal_safe, data, datalen, formatter);
}
