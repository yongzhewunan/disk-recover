/*
 * filegen.h - TestDisk file generation framework (adapted for disk-recover)
 *
 * Copyright (C) 1998-2008 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover:
 * - Removed FRAMAC annotations
 * - Added MSVC compatibility
 * - Always defines file_recovery_t (no SINGLE_FORMAT guard)
 * - Uses alloc_list_t from list.h
 */
#ifndef _FILEGEN_H
#define _FILEGEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <time.h>
#include "td_config.h"
#include "list.h"

/* ========================================================================
 * Data check result enumeration
 * ======================================================================== */
typedef enum {
    DC_SCAN = 0,
    DC_CONTINUE = 1,
    DC_STOP = 2,
    DC_ERROR = 3
} data_check_t;

/* ========================================================================
 * Newline detection flags (used by PDF and other text-based formats)
 * ======================================================================== */
#define NL_BARENL       (1 << 0)
#define NL_CRLF         (1 << 1)
#define NL_BARECR       (1 << 2)

/* ========================================================================
 * Forward declarations
 * ======================================================================== */
typedef struct file_hint_struct file_hint_t;
typedef struct file_stat_struct file_stat_t;
typedef struct file_recovery_struct file_recovery_t;

/* ========================================================================
 * file_hint_t - format descriptor
 * ======================================================================== */
struct file_hint_struct {
    const char *extension;       /* file extension (without dot) */
    const char *description;     /* human-readable description */
    const uint64_t max_filesize; /* maximum valid file size, 0=unlimited */
    const int recover;           /* whether this format can be recovered */
    const unsigned int enable_by_default; /* default enabled status */
    void (*register_header_check)(file_stat_t *file_stat); /* registration function */
};

/* ========================================================================
 * alloc_data_t - allocation tracking
 * ======================================================================== */
typedef struct {
    struct td_list_head list;
    uint64_t start;
    uint64_t end;
    file_stat_t *file_stat;
    unsigned int data;
} alloc_data_t;

/* ========================================================================
 * file_stat_t - file statistics
 * ======================================================================== */
struct file_stat_struct {
    unsigned int not_recovered;
    unsigned int recovered;
    const file_hint_t *file_hint;
};

/* ========================================================================
 * file_recovery_t - recovery context for a single file
 *
 * Uses alloc_list_t from list.h for the location field.
 * ======================================================================== */
struct file_recovery_struct {
    char filename[2048];
    alloc_list_t location;       /* List of allocation extents (from list.h) */
    file_stat_t *file_stat;
    FILE *handle;
    time_t time;
    uint64_t file_size;
    const char *extension;
    uint64_t min_filesize;
    uint64_t offset_ok;
    uint64_t offset_error;
    uint64_t extra;              /* extra bytes between offset_ok and offset_error */
    uint64_t calculated_file_size;
    data_check_t (*data_check)(const unsigned char *buffer,
                               const unsigned int buffer_size,
                               file_recovery_t *file_recovery);
    void (*file_check)(file_recovery_t *file_recovery);
    void (*file_rename)(file_recovery_t *file_recovery);
    uint64_t checkpoint_offset;
    int checkpoint_status;
    unsigned int blocksize;
    unsigned int flags;
    unsigned int data_check_tmp;
};

/* ========================================================================
 * file_check_t - header check registration entry
 * ======================================================================== */
typedef struct {
    struct td_list_head list;
    const void *value;
    unsigned int length;
    unsigned int offset;
    int (*header_check)(const unsigned char *buffer, const unsigned int buffer_size,
                        const unsigned int safe_header_only,
                        const file_recovery_t *file_recovery,
                        file_recovery_t *file_recovery_new);
    file_stat_t *file_stat;
} file_check_t;

/* ========================================================================
 * file_check_list_t - header check list
 * ======================================================================== */
typedef struct {
    file_check_t file_checks[256];
    struct td_list_head list;
    unsigned int offset;
} file_check_list_t;

/* ========================================================================
 * Core API functions
 * ======================================================================== */

/* Global header check list - accessible for adapter */
extern file_check_t file_check_plist;

void reset_file_recovery(file_recovery_t *file_recovery);

data_check_t data_check_size(const unsigned char *buffer,
                             const unsigned int buffer_size,
                             file_recovery_t *file_recovery);

void file_check_size(file_recovery_t *file_recovery);
void file_check_size_min(file_recovery_t *file_recovery);
void file_check_size_max(file_recovery_t *file_recovery);

void register_header_check(const unsigned int offset,
                           const void *value, const unsigned int length,
                           int (*header_check)(const unsigned char *buffer,
                                               const unsigned int buffer_size,
                                               const unsigned int safe_header_only,
                                               const file_recovery_t *file_recovery,
                                               file_recovery_t *file_recovery_new),
                           file_stat_t *file_stat);

void header_ignored(const file_recovery_t *file_recovery_new);
void header_ignored_cond_reset(uint64_t start, uint64_t end);
int header_ignored_adv(const file_recovery_t *file_recovery,
                       file_recovery_t *file_recovery_new);

int my_fseek(FILE *stream, long offset, int whence);

time_t get_time_from_YYMMDDHHMMSS(const char *date_asc);
time_t get_time_from_YYYY_MM_DD_HH_MM_SS(const unsigned char *date_asc);
time_t get_time_from_YYYY_MM_DD_HHMMSS(const char *date_asc);
time_t get_time_from_YYYYMMDD_HHMMSS(const char *date_asc);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _FILEGEN_H */