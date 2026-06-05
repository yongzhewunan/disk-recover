/*
 * filegen.c - TestDisk file generation framework (adapted for disk-recover)
 *
 * Copyright (C) 2007-2009 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover:
 * - Simplified registration (no sorting needed for adapter use)
 * - MSVC compatibility
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "td_config.h"
#include "types.h"
#include "common.h"
#include "filegen.h"

/* Global header check list - non-static for adapter access */
file_check_t file_check_plist = {
    .list = TD_LIST_HEAD_INIT(file_check_plist.list)
};

file_check_list_t file_check_list = {
    .list = TD_LIST_HEAD_INIT(file_check_list.list)
};

/* ========================================================================
 * Core functions
 * ======================================================================== */

void reset_file_recovery(file_recovery_t *file_recovery)
{
    memset(file_recovery, 0, sizeof(*file_recovery));
    file_recovery->filename[0] = '\0';
    file_recovery->location.list.prev = &file_recovery->location.list;
    file_recovery->location.list.next = &file_recovery->location.list;
    file_recovery->location.end = 0;
    file_recovery->location.start = 0;
}

data_check_t data_check_size(const unsigned char *buffer,
                             const unsigned int buffer_size,
                             file_recovery_t *file_recovery)
{
    if (file_recovery->calculated_file_size == 0)
        return DC_CONTINUE;
    if (file_recovery->file_size >= file_recovery->calculated_file_size)
        return DC_STOP;
    return DC_CONTINUE;
}

void file_check_size(file_recovery_t *file_recovery)
{
    if (file_recovery->calculated_file_size > 0 &&
        file_recovery->file_size > file_recovery->calculated_file_size)
    {
        file_recovery->file_size = file_recovery->calculated_file_size;
    }
}

void file_check_size_min(file_recovery_t *file_recovery)
{
    if (file_recovery->calculated_file_size > 0 &&
        file_recovery->calculated_file_size >= file_recovery->min_filesize &&
        file_recovery->file_size > file_recovery->calculated_file_size)
    {
        file_recovery->file_size = file_recovery->calculated_file_size;
    }
}

void file_check_size_max(file_recovery_t *file_recovery)
{
    if (file_recovery->calculated_file_size > 0 &&
        file_recovery->calculated_file_size <= file_recovery->file_stat->file_hint->max_filesize &&
        file_recovery->file_size > file_recovery->calculated_file_size)
    {
        file_recovery->file_size = file_recovery->calculated_file_size;
    }
}

void register_header_check(const unsigned int offset,
                           const void *value, const unsigned int length,
                           int (*header_check)(const unsigned char *buffer,
                                               const unsigned int buffer_size,
                                               const unsigned int safe_header_only,
                                               const file_recovery_t *file_recovery,
                                               file_recovery_t *file_recovery_new),
                           file_stat_t *file_stat)
{
    file_check_t *file_check_new;

    if (offset > PHOTOREC_MAX_SIG_OFFSET ||
        length == 0 ||
        length > PHOTOREC_MAX_SIG_SIZE ||
        offset + length > PHOTOREC_MAX_SIG_OFFSET)
    {
        return;
    }

    file_check_new = (file_check_t *)malloc(sizeof(file_check_t));
    if (file_check_new == NULL)
        return;

    file_check_new->value = value;
    file_check_new->length = length;
    file_check_new->offset = offset;
    file_check_new->header_check = header_check;
    file_check_new->file_stat = file_stat;

    td_list_add_tail(&file_check_new->list, &file_check_plist.list);
}

void header_ignored(const file_recovery_t *file_recovery_new)
{
    /* Placeholder - may be needed for some validators */
}

void header_ignored_cond_reset(uint64_t start, uint64_t end)
{
    /* Placeholder */
}

int header_ignored_adv(const file_recovery_t *file_recovery,
                       file_recovery_t *file_recovery_new)
{
    /* Return 0 to reject nested detection */
    return 0;
}

int my_fseek(FILE *stream, long offset, int whence)
{
    return fseek(stream, offset, whence);
}

time_t get_time_from_YYMMDDHHMMSS(const char *date_asc)
{
    /* Stub implementation */
    return 0;
}

time_t get_time_from_YYYY_MM_DD_HH_MM_SS(const unsigned char *date_asc)
{
    /* Stub implementation */
    return 0;
}

time_t get_time_from_YYYY_MM_DD_HHMMSS(const char *date_asc)
{
    /* Stub implementation */
    return 0;
}

time_t get_time_from_YYYYMMDD_HHMMSS(const char *date_asc)
{
    /* Stub implementation */
    return 0;
}