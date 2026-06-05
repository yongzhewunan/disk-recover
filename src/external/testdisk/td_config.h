/*
 * td_config.h - TestDisk configuration and portability for disk-recover
 *
 * This header provides platform-specific definitions and workarounds
 * to compile TestDisk source code with MSVC.
 *
 * Each validator is compiled as a separate translation unit with
 * SINGLE_FORMAT_xxx defined, which makes each file self-contained.
 */

#ifndef _TD_CONFIG_H
#define _TD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Platform detection */
#ifdef _MSC_VER
  #define TD_MSVC 1
#else
  #define TD_MSVC 0
#endif

/* Disable HAVE_CONFIG_H - we provide our own definitions */
#undef HAVE_CONFIG_H

/* ========================================================================
 * Structure packing - GCC vs MSVC
 *
 * TestDisk uses GCC-specific __attribute__((__packed__)) for structs.
 * On MSVC we redefine __attribute__ to nothing and use #pragma pack.
 * ======================================================================== */
#ifdef _MSC_VER
  /* Suppress GCC-specific attributes on MSVC */
  #ifndef __attribute__
    #define __attribute__(x)
  #endif
#endif

/* ========================================================================
 * Standard integer types and definitions
 * ======================================================================== */
#include <stdint.h>
#include <stddef.h>

/* ssize_t and off_t for MSVC */
#ifdef _MSC_VER
  #include <BaseTsd.h>
  typedef SSIZE_T ssize_t;
  #define off_t int64_t
#else
  #include <sys/types.h>
#endif

/* ========================================================================
 * Standard library feature detection
 * ======================================================================== */
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_TIME_H 1
#define HAVE_CTYPE_H 1
#define HAVE_MEMORY_H 1

/* ========================================================================
 * Disable features we don't need from TestDisk
 * ======================================================================== */
#undef HAVE_LIBJPEG
#undef HAVE_JPEGLIB_H
#undef __FRAMAC__

/* ========================================================================
 * Debug flags - enable for debugging specific formats
 * ======================================================================== */
#ifdef DEBUG_TESTDISK
  #define DEBUG_PHOTOSHOP 1
  #define DEBUG_JPEG 1
  #define DEBUG_INDD
#else
  #undef DEBUG_PHOTOSHOP
  #undef DEBUG_JPEG
  #undef DEBUG_INDD
#endif

/* ========================================================================
 * Memory search function - memmem replacement
 * ======================================================================== */
void* td_memmem(const void* haystack, size_t haystack_len,
                const void* needle, size_t needle_len);

#ifdef __cplusplus
}
#endif

#endif /* _TD_CONFIG_H */