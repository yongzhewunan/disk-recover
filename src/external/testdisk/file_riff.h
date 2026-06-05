/*
 * file_riff.h - RIFF/AVI definitions for TestDisk validators
 *
 * Copyright (C) 2021 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover: Removed FRAMAC annotations, added MSVC compat.
 */
#ifndef _FILE_RIFF_H
#define _FILE_RIFF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "td_config.h"
#include "filegen.h"

data_check_t data_check_avi_stream(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _FILE_RIFF_H */
