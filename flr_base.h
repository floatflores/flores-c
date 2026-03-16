/**
 * @file
 * @brief Flor's C foundation library: basic types, constants, and utility
 * macros.
 */

#ifndef FLR_BASE_H
#define FLR_BASE_H

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

typedef int8_t  flr_i8;
typedef int16_t flr_i16;
typedef int32_t flr_i32;
typedef int64_t flr_i64;

typedef uint8_t  flr_u8;
typedef uint16_t flr_u16;
typedef uint32_t flr_u32;
typedef uint64_t flr_u64;

typedef flr_i8  flr_b8;
typedef flr_i32 flr_b32;

typedef float flr_f32;

#define FLR_KiB(n) ((flr_u64)(n) << 10)
#define FLR_MiB(n) ((flr_u64)(n) << 20)
#define FLR_GiB(n) ((flr_u64)(n) << 30)

#define FLR_MIN(a, b) ((((a) < (b)) ? (a) : (b)))
#define FLR_MAX(a, b) ((((a) > (b)) ? (a) : (b)))

// NOTE: p should be power of 2 to ensure this work correctly
#define FLR_ALIGN_UP_POW2(n, p) (((flr_u64)(n) + ((flr_u64)(p) - 1)) & (~((flr_u64)(p) - 1)))
#define FLR_ALIGN_DOWN_POW2(n, p) ((flr_u64)(n) & (~((flr_u64)(p) - 1)))

// clang-format off
#ifndef FLR_THREAD_LOCAL
    #if defined(__cplusplus) && __cplusplus >= 201103L
        #define FLR_THREAD_LOCAL thread_local
    #elif defined(__GNUC__) || defined(__clang__)
        #define FLR_THREAD_LOCAL __thread
    #elif defined(_MSC_VER)
        #define FLR_THREAD_LOCAL __declspec(thread)
    #else
        #define FLR_THREAD_LOCAL
    #endif
#endif // FLR_THREAD_LOCAL
// clang-format on

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // !FLR_BASE_H
