#ifndef __VTYPES_H__
#define __VTYPES_H__

#if (defined __linux__)
#ifndef LINUX
#define LINUX
#endif

#ifndef UNIX
#define UNIX
#endif
#elif (defined __sun__)
#ifndef SOLARIS
#define SOLARIS
#endif

#ifndef UNIX
#define UNIX
#endif
#else
#ifndef WIN32
#define WIN32
#endif
#endif

#define VAPI_SUCCESS (0)
#define VAPI_FAILURE (-1)

#ifdef WIN32
#ifdef __cplusplus
extern "C" {
#endif

typedef char             int8_t;
typedef unsigned char    uint8_t;
typedef short            int16_t;
typedef unsigned short   uint16_t;
typedef int              int32_t;
typedef unsigned int     uint32_t;
typedef __int64          int64_t;
typedef unsigned __int64 uint64_t;

typedef unsigned char  uchar_t;
typedef unsigned short ushort_t;
typedef unsigned int   uint_t;
typedef unsigned long  ulong_t;

typedef int ssize_t;

#ifdef __cplusplus
}
#endif
#elif defined(LINUX)
#include <inttypes.h>
#include <sys/types.h>
#include <stdbool.h>

typedef unsigned char  uchar_t;
typedef unsigned short ushort_t;
typedef unsigned int   uint_t;
typedef unsigned long  ulong_t;
#else
#include <inttypes.h>
#include <sys/types.h>
#include <stdbool.h>
#endif

#ifndef MIN
/*!
 * Returns the minimum of two values.
 */
#ifdef __cplusplus
#define MIN(__a, __b)   ({                          \
            auto __min_a = (__a);                   \
            auto __min_b = (__b);                   \
            (void)(&__min_a == &__min_b);           \
            __min_a < __min_b ? __min_a : __min_b;  \
        })
#else
#define MIN(__a, __b)   ({                          \
            __typeof__(__a) __min_a = (__a);        \
            __typeof__(__b) __min_b = (__b);        \
            (void)(&__min_a == &__min_b);           \
            __min_a < __min_b ? __min_a : __min_b;  \
        })
#endif
#endif

#ifndef MAX
/*!
 * Returns the maximum of two values.
 */
#ifdef __cplusplus
#define MAX(__a, __b)   ({                          \
            auto __max_a = (__a);                   \
            auto __max_b = (__b);                   \
            (void)(&__max_a == &__max_b);           \
            __max_a > __max_b ? __max_a : __max_b;  \
        })
#else
#define MAX(__a, __b)   ({                          \
            __typeof__(__a) __max_a = (__a);        \
            __typeof__(__b) __max_b = (__b);        \
            (void)(&__max_a == &__max_b);           \
            __max_a > __max_b ? __max_a : __max_b;  \
        })
#endif
#endif

#ifndef container_of
#define container_of(struct_type,struct_member,val) \
        ((struct_type *)(((char *)(val))-offsetof(struct_type,struct_member)))
#endif

#endif
