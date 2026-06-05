/*
 * file_tiff.h - TIFF definitions for TestDisk validators
 *
 * Copyright (C) 2009 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover: Removed FRAMAC annotations, added MSVC compat.
 */
#ifndef _FILE_TIFF_H
#define _FILE_TIFF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "td_config.h"
#include "types.h"

#define TIFF_ERROR 0xffffffffffffffffull

#define TIFF_BIGENDIAN          0x4d4d
#define TIFF_LITTLEENDIAN       0x4949
#define TIFFTAG_IMAGEDESCRIPTION        270
#define TIFFTAG_MAKE                    271
#define TIFFTAG_MODEL                   272
#define TIFFTAG_STRIPOFFSETS            273
#define TIFFTAG_STRIPBYTECOUNTS         279
#define TIFFTAG_TILEOFFSETS             324
#define TIFFTAG_TILEBYTECOUNTS          325
#define TIFFTAG_SUBIFD                  330
#define TIFFTAG_JPEGIFOFFSET            513
#define TIFFTAG_JPEGIFBYTECOUNT         514
#define TIFFTAG_KODAKIFD                33424
#define TIFFTAG_EXIFIFD                 34665
#define EXIFTAG_MAKERNOTE               37500
#define TIFFTAG_SONY_FILEFORMAT         0xb000
#define TIFFTAG_IMAGEOFFSET             0xbcc0
#define TIFFTAG_IMAGEBYTECOUNT          0xbcc1
#define TIFFTAG_ALPHAOFFSET             0xbcc2
#define TIFFTAG_ALPHABYTECOUNT          0xbcc3
#define TIFFTAG_PRINTIM                 50341
#define TIFFTAG_DNGVERSION              50706
#define TIFFTAG_DNGPRIVATEDATA          50740

typedef struct {
    uint16_t  tiff_magic;
    uint16_t  tiff_version;
    uint32_t  tiff_diroff;
} TIFFHeader;

typedef struct {
    uint16_t          tdir_tag;
    uint16_t          tdir_type;
    uint32_t          tdir_count;
    uint32_t          tdir_offset;
} TIFFDirEntry;

/* Packed IFD header - MSVC compatible */
#ifdef _MSC_VER
__pragma(pack(push, 1))
struct ifd_header {
    uint16_t nbr_fields;
    TIFFDirEntry ifd;
};
__pragma(pack(pop))
#else
struct ifd_header {
    uint16_t nbr_fields;
    TIFFDirEntry ifd;
} __attribute__ ((gcc_struct, __packed__));
#endif

/* TIFF header check functions (defined in file_tiff_be.c / file_tiff_le.c) */
int header_check_tiff_be(const unsigned char *buffer, const unsigned int buffer_size,
                         const unsigned int safe_header_only,
                         const file_recovery_t *file_recovery,
                         file_recovery_t *file_recovery_new);

int header_check_tiff_le(const unsigned char *buffer, const unsigned int buffer_size,
                         const unsigned int safe_header_only,
                         const file_recovery_t *file_recovery,
                         file_recovery_t *file_recovery_new);

/* TIFF helper functions */
time_t get_date_from_tiff_header(const unsigned char*buffer, const unsigned int buffer_size);
unsigned int find_tag_from_tiff_header(const unsigned char *buffer, const unsigned int buffer_size, const unsigned int tag, const unsigned char **potential_error);
unsigned int find_tag_from_tiff_header_le(const unsigned char *buffer, const unsigned int tiff_size, const unsigned int tag, const unsigned char**potential_error);
unsigned int find_tag_from_tiff_header_be(const unsigned char*buffer, const unsigned int tiff_size, const unsigned int tag, const unsigned char**potential_error);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _FILE_TIFF_H */