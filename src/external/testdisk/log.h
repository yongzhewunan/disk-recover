/*

    File: log.h - Adapted for disk-recover

    Simplified logging interface for TestDisk validators.

    Copyright (C) 2007-2009 Christophe GRENIER <grenier@cgsecurity.org>
    Adapted for disk-recover in 2024

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#ifndef _LOG_H
#define _LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>

/* Log levels (kept for compatibility with TestDisk macros) */
#define LOG_LEVEL_DEBUG    (1 <<  0)
#define LOG_LEVEL_TRACE    (1 <<  1)
#define LOG_LEVEL_QUIET    (1 <<  2)
#define LOG_LEVEL_INFO     (1 <<  3)
#define LOG_LEVEL_VERBOSE  (1 <<  4)
#define LOG_LEVEL_PROGRESS (1 <<  5)
#define LOG_LEVEL_WARNING  (1 <<  6)
#define LOG_LEVEL_ERROR    (1 <<  7)
#define LOG_LEVEL_PERROR   (1 <<  8)
#define LOG_LEVEL_CRITICAL (1 <<  9)

/* Stub functions - disk-recover uses its own logging system */

static inline unsigned int log_set_levels(const unsigned int levels) {
    return 0;
}

static inline int log_open(const char* default_filename, const int mode, int *errsv) {
    return 0;
}

static inline int log_open_default(const char* default_filename, const int mode, int *errsv) {
    return 0;
}

static inline int log_flush(void) {
    return 0;
}

static inline int log_close(void) {
    return 0;
}

/* Simplified log_redirect - outputs to stderr when debug is enabled */
static inline int log_redirect(const unsigned int level, const char *format, ...) {
#ifdef DEBUG_TESTDISK
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
#endif
    return 0;
}

static inline int log_redirect_nojson(const unsigned int level, const char *format, ...) {
#ifdef DEBUG_TESTDISK
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
#endif
    return 0;
}

static inline void dump_log(const void *nom_dump, const unsigned int lng) {
    /* Stub - not needed for validation */
}

static inline void dump2_log(const void *dump_1, const void *dump_2, const unsigned int lng) {
    /* Stub - not needed for validation */
}

/* Log macros - kept for TestDisk source compatibility */
#define log_debug(FORMAT, ...)  log_redirect(LOG_LEVEL_DEBUG, FORMAT, ##__VA_ARGS__)
#define log_trace(FORMAT, ...)  log_redirect(LOG_LEVEL_TRACE, FORMAT, ##__VA_ARGS__)
#define log_quiet(FORMAT, ...)  log_redirect(LOG_LEVEL_QUIET, FORMAT, ##__VA_ARGS__)
#define log_info(FORMAT, ...)   log_redirect(LOG_LEVEL_INFO, FORMAT, ##__VA_ARGS__)
#define log_info_nojson(FORMAT, ...) log_redirect_nojson(LOG_LEVEL_INFO, FORMAT, ##__VA_ARGS__)
#define log_verbose(FORMAT, ...) log_redirect(LOG_LEVEL_VERBOSE, FORMAT, ##__VA_ARGS__)
#define log_progress(FORMAT, ...) log_redirect(LOG_LEVEL_PROGRESS, FORMAT, ##__VA_ARGS__)
#define log_warning(FORMAT, ...) log_redirect(LOG_LEVEL_WARNING, FORMAT, ##__VA_ARGS__)
#define log_error(FORMAT, ...)  log_redirect(LOG_LEVEL_ERROR, FORMAT, ##__VA_ARGS__)
#define log_perror(FORMAT, ...) log_redirect(LOG_LEVEL_PERROR, FORMAT, ##__VA_ARGS__)
#define log_critical(FORMAT, ...) log_redirect(LOG_LEVEL_CRITICAL, FORMAT, ##__VA_ARGS__)

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _LOG_H */