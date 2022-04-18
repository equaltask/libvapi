#ifndef __UTILITY_H__
#define __UTILITY_H__

#include <stdio.h>
#include <stdlib.h>

/*
 * \file utility.h
 * This header file is intended for small utility macros, related to basic
 * C programming, for common usage.
 * It should not be used for feature-specific macros, or complicated stuff.
 *
 * \author Thomas De Schampheleire
 */

/**
 * \brief Return the number of elements in an array or string
 */
#ifndef DIM
#define DIM(x) (sizeof(x)/sizeof((x)[0]))
#endif


/**
 * \brief Return a bitmask with bit x set (all other bits zero)
 */
#define BIT(x) (1UL << (x))


/**
 * \brief Returns a bitmask with bits a to b (included) set to 1
 */
#define BITMASK(a,b) ((BIT(MAX((a), (b)) + 1) - 1) ^ (BIT(MIN((a), (b))) - 1))

/**
 * \brief Concatenates two symbol.
 */
#define SYMBOL_CONCAT_(a, b) a##b
#define SYMBOL_CONCAT(a, b) SYMBOL_CONCAT_(a, b)

/**
 * \brief Invokes compilation error(dividing by zero) on false condition. It is compile assert.
 */
/* Reference: http://www.pixelbeat.org/programming/gcc/static_assert.html */
/* These can't be used after statements in c89. */
#ifdef __COUNTER__
#define STATIC_ASSERT(e) \
    ; /* ERROR: static assert failed. */ enum { SYMBOL_CONCAT(static_assert_, __COUNTER__) = 1/(!!(e)) }
#else
/* This can't be used twice on the same line so ensure if using in headers
 * that the headers are not included twice (by wrapping in #ifndef...#endif)
 * Note it doesn't cause an issue when used on same line of separate modules
 * compiled with gcc -combine -fwhole-program.  */
#define STATIC_ASSERT(e) \
    ; /* ERROR: static assert failed. */ enum { SYMBOL_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }
#endif
/**
 * \brief Align an address to the specified alignment, upwards (new >= old)
 */
#define ALIGN(address, alignment) (((address) + (alignment) - 1UL) & ~((alignment) - 1UL))
#define ALIGN_UP ALIGN

/**
 * \brief Align an address to the specified alignment, downwards (new <= old)
 */
#define ALIGN_DOWN(address, alignment) ((address) & ~((alignment) - 1UL))

/**
 * \brief Convert a constant into a string constant
 *
 * For example, XSTR(4) returns "4".
 */
#define XSTR(s) STR(s)
#define STR(s) #s

/**
 * \brief Count the number of arguments given to a variadic macro
 *
 * Copied from: https://groups.google.com/forum/#!topic/comp.std.c/d-6Mj5Lko_s/discussion
 *
 */
#define __VA_NARG__(...) \
        __VA_NARG_(_0, ## __VA_ARGS__, __RSEQ_N())
#define __VA_NARG_(...) \
        __VA_ARG_N(__VA_ARGS__)
#define __VA_ARG_N( \
        _0, _1, _2, _3, _4, _5, _6, _7, _8, _9,_10, \
        _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
        _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
        _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
        _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
        _51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
        _61,_62,N,...) N
#define __RSEQ_N() \
        62, 61, 60,                         \
        59, 58, 57, 56, 55, 54, 53, 52, 51, 50, \
        49, 48, 47, 46, 45, 44, 43, 42, 41, 40, \
        39, 38, 37, 36, 35, 34, 33, 32, 31, 30, \
        29, 28, 27, 26, 25, 24, 23, 22, 21, 20, \
        19, 18, 17, 16, 15, 14, 13, 12, 11, 10, \
         9,  8,  7,  6,  5,  4,  3,  2,  1,  0

/**
 * \brief Can be used to write an enum value stringifier function
 */
#define CASE_ENUM_TO_STR(v) case v: return #v;


#define LINE_SIZE   128

int hexdump(void *addr, size_t len, char **buffer);

#endif /* __UTILITY_H__ */

