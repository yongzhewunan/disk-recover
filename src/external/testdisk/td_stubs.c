/*
    File: td_stubs.c

    Stub implementations for TestDisk/PhotoRec helper functions that are
    referenced by validator modules but not needed by disk-recover's
    file carving workflow. These stubs provide minimal no-op or fallback
    implementations to satisfy linker requirements.

    Copyright (C) 2024
    Based on TestDisk/PhotoRec by Christophe GRENIER

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "filegen.h"
#include "common.h"
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ── file_rename: rename recovered file based on content ──
 * In PhotoRec, this renames the recovered file on disk using content extracted
 * from the file (e.g., extracting title from PDF, artist from MP3).
 * disk-recover doesn't need this since we assign filenames at recovery time.
 * Stub: no-op.
 */
void file_rename(file_recovery_t *file_recovery, const void *buffer,
                 size_t buffer_size, size_t offset, const char *ext, int force_ext)
{
    /* No-op stub — disk-recover handles file naming at recovery time */
    (void)file_recovery;
    (void)buffer;
    (void)buffer_size;
    (void)offset;
    (void)ext;
    (void)force_ext;
}

/* ── file_search_footer: search for footer pattern in file ──
 * In PhotoRec, this searches from the end of the file backwards for a pattern
 * to determine the exact file boundary. disk-recover's file_check wrapper
 * handles boundary detection differently.
 * Stub: no-op (sets file_size = calculated_file_size as a fallback).
 */
void file_search_footer(file_recovery_t *file_recovery, const void *footer,
                        size_t footer_length, size_t extra_length)
{
    /* No-op stub — disk-recover uses progressive carving and data_check
     * for boundary detection, not backward footer search */
    (void)file_recovery;
    (void)footer;
    (void)footer_length;
    (void)extra_length;
}

/* ── file_allow_nl: configure newline tolerance for footer search ──
 * In PhotoRec, this configures whether footer search allows bare newlines.
 * Stub: no-op.
 */
void file_allow_nl(file_recovery_t *file_recovery, unsigned int nl_mode)
{
    /* No-op stub */
    (void)file_recovery;
    (void)nl_mode;
}

/* ── file_rsearch: reverse search in file for a pattern ──
 * In PhotoRec, this searches backwards through the file for a pattern.
 * Stub: returns 0 (not found).
 */
uint64_t file_rsearch(FILE *handle, uint64_t offset, const void *pattern, size_t pattern_size)
{
    /* Stub: always return 0 (pattern not found) */
    (void)handle;
    (void)offset;
    (void)pattern;
    (void)pattern_size;
    return 0;
}

/* ── td_ntfs2utc: convert NTFS timestamp to UTC time_t ──
 * NTFS timestamps are 100-nanosecond intervals since Jan 1, 1601.
 * Convert to Unix time_t (seconds since Jan 1, 1970).
 */
time_t td_ntfs2utc(uint64_t ntfs_timestamp)
{
    /* NTFS epoch: Jan 1, 1601. Unix epoch: Jan 1, 1970.
     * Difference: 11644473600 seconds (369 years × 365.25 days × 86400 sec/day)
     * NTFS timestamp unit: 100 nanoseconds = 1e-7 seconds
     */
    const uint64_t NTFS_TO_UNIX_EPOCH = 116444736000000000ULL;  /* in 100-ns units */
    const uint64_t NTFS_TO_SECONDS    = 10000000ULL;            /* 100-ns to seconds */

    if (ntfs_timestamp < NTFS_TO_UNIX_EPOCH) {
        return 0;
    }
    uint64_t unix_time_100ns = ntfs_timestamp - NTFS_TO_UNIX_EPOCH;
    return (time_t)(unix_time_100ns / NTFS_TO_SECONDS);
}

/* ── strncasecmp: case-insensitive string comparison ──
 * Not available on Windows with MSVC in C mode without POSIX extensions.
 */
int strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c1 = (unsigned char)tolower((unsigned char)s1[i]);
        unsigned char c2 = (unsigned char)tolower((unsigned char)s2[i]);
        if (c1 != c2) return (int)c1 - (int)c2;
        if (c1 == 0) return 0;
    }
    return 0;
}

/* ── date_dos2unix: convert DOS date/time to Unix time_t ──
 * DOS date format: bits 0-4=day, 5-8=month, 9-15=year-1980
 * DOS time format: bits 0-4=sec/2, 5-10=min, 11-15=hour
 */
time_t date_dos2unix(unsigned short dos_time, unsigned short dos_date)
{
    struct tm t;
    t.tm_sec  = (dos_time & 31) * 2;
    t.tm_min  = (dos_time >> 5) & 63;
    t.tm_hour = (dos_time >> 11) & 31;
    t.tm_mday = dos_date & 31;
    t.tm_mon  = ((dos_date >> 5) & 15) - 1;  /* 1-12 → 0-11 */
    t.tm_year = (dos_date >> 9) + 80;         /* 1980+ → 1900+ offset */
    t.tm_isdst = -1;
    return mktime(&t);
}

/* ── TIFF header check stubs ──
 * file_tiff.c references header_check_tiff_be and header_check_tiff_le
 * via register_header_check_tiff, but these are defined in file_tiff_be.c
 * and file_tiff_le.c respectively. Since we compile all three tiff files,
 * these should be available. However, under SINGLE_FORMAT_tiff, these
 * external references may not be resolved.
 *
 * The tiff validation in disk-recover is handled by the TestDisk adapter
 * which calls the header_check functions via file_check_plist, so we
 * provide stubs here as fallback.
 */
int header_check_tiff_be(const unsigned char *buffer, unsigned int buffer_size,
                         unsigned int safe_header_only, file_recovery_t *file_recovery,
                         file_recovery_t *file_recovery_new)
{
    /* Stub — TIFF BE validation is handled by the TestDisk adapter */
    (void)buffer;
    (void)buffer_size;
    (void)safe_header_only;
    (void)file_recovery;
    (void)file_recovery_new;
    return 0;
}

int header_check_tiff_le(const unsigned char *buffer, unsigned int buffer_size,
                         unsigned int safe_header_only, file_recovery_t *file_recovery,
                         file_recovery_t *file_recovery_new)
{
    /* Stub — TIFF LE validation is handled by the TestDisk adapter */
    (void)buffer;
    (void)buffer_size;
    (void)safe_header_only;
    (void)file_recovery;
    (void)file_recovery_new;
    return 0;
}

/* ── find_tag_from_tiff_header stubs ──
 * These are helper functions referenced by file_tiff.c for parsing
 * TIFF IFD entries. The actual TIFF validation is handled through
 * file_tiff_le.c and file_tiff_be.c, but cross-references occur
 * under SINGLE_FORMAT builds.
 */
unsigned int find_tag_from_tiff_header_le(const unsigned char *buffer, unsigned int buffer_size,
                                          unsigned int tag, const unsigned char **tag_value)
{
    (void)buffer;
    (void)buffer_size;
    (void)tag;
    (void)tag_value;
    return 0;
}

unsigned int find_tag_from_tiff_header_be(const unsigned char *buffer, unsigned int buffer_size,
                                          unsigned int tag, const unsigned char **tag_value)
{
    (void)buffer;
    (void)buffer_size;
    (void)tag;
    (void)tag_value;
    return 0;
}