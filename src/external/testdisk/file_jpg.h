/*
 * file_jpg.h - JPEG definitions for TestDisk validators
 *
 * Copyright (C) 2009 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover: Removed FRAMAC annotations, added MSVC compat.
 */
#ifndef _FILE_JPG_H
#define _FILE_JPG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "td_config.h"
#include "types.h"

/* JPEG marker codes */
#define JPEG_MARKER_SOI   0xD8   /* Start of Image */
#define JPEG_MARKER_EOI   0xD9   /* End of Image */
#define JPEG_MARKER_SOS   0xDA   /* Start of Scan */
#define JPEG_MARKER_DQT   0xDB   /* Define Quantization Table */
#define JPEG_MARKER_DHT   0xC4   /* Define Huffman Table */
#define JPEG_MARKER_DRI   0xDD   /* Define Restart Interval */
#define JPEG_MARKER_APP1  0xE1   /* APP1 - EXIF */
#define JPEG_MARKER_APP2  0xE2   /* APP2 - ICC profile */
#define JPEG_MARKER_APP13 0xED   /* APP13 - Photoshop IRB */
#define JPEG_MARKER_COM   0xFE   /* Comment */

/* JPEG recovery context size */
#define JPEG_SAVE_STRINGS 5
#define JPG_MAX_STRIPS    1024

/* JPEG save structure for progressive validation */
typedef struct {
    uint32_t stripped_offsets[JPG_MAX_STRIPS];
    uint32_t stripped_sizes[JPG_MAX_STRIPS];
    unsigned int stripped_count;
    int ok;
    time_t time;
    unsigned int offset_star_coord;
    unsigned int offset_sof;
    int sof_len;
    uint32_t height;
    uint32_t width;
    unsigned int ncomp;
    const char *tag;
    unsigned int offsets[2];
    unsigned int sizes[2];
    unsigned int avif_count;
    unsigned int truncated;
} jpg_app12_t;

/* JPEG header check function declaration */
int header_check_jpg(const unsigned char *buffer, const unsigned int buffer_size,
                     const unsigned int safe_header_only,
                     const file_recovery_t *file_recovery,
                     file_recovery_t *file_recovery_new);

/* JPEG data check function declaration */
data_check_t data_check_jpg(const unsigned char *buffer, const unsigned int buffer_size,
                            file_recovery_t *file_recovery);

/* JPEG file check function declaration */
void file_check_jpg(file_recovery_t *file_recovery);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _FILE_JPG_H */