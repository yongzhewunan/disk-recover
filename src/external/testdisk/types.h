/*

    File: types.h - Adapted for disk-recover

    Copyright (C) 1998-2004 Christophe GRENIER <grenier@cgsecurity.org>

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */
#ifndef _TESTDISK_TYPES_H
#define _TESTDISK_TYPES_H

#include <stdint.h>

/* Byte order macros - Windows is little endian */
#define be16(x)  __swab16(x)
#define be24(x)  __swab24(x)
#define be32(x)  __swab32(x)
#define be64(x)  __swab64(x)
#define le16(x)  (x)
#define le24(x)  (x)
#define le32(x)  (x)
#define le64(x)  (x)

/* Byte swap macros */
#define __swab16(x) ((((uint16_t)(x) & (uint16_t)0xff00) >> 8) | \
                     (((uint16_t)(x) & (uint16_t)0x00ff) << 8))

#define __swab24(x) ((((x) & 0x000000ffUL) << 16) | \
                     ((x) & 0x0000ff00UL)        | \
                     (((x) & 0x00ff0000UL) >> 16))

#define __swab32(x)  ((((uint32_t)(x)&(uint32_t)0xff000000UL)>>24) | \
                      (((uint32_t)(x)&(uint32_t)0x00ff0000UL)>>8)  | \
                      (((uint32_t)(x)&(uint32_t)0x0000ff00UL)<<8)  | \
                      (((uint32_t)(x)&(uint32_t)0x000000ffUL)<<24))

#define __swab64(x)  ((((uint64_t)(x)&(uint64_t)0xff00000000000000ULL)>>56) | \
                      (((uint64_t)(x)&(uint64_t)0x00ff000000000000ULL)>>40) | \
                      (((uint64_t)(x)&(uint64_t)0x000000ff00000000ULL)>>24) | \
                      (((uint64_t)(x)&(uint64_t)0x00000000ff000000ULL)>>8)  | \
                      (((uint64_t)(x)&(uint64_t)0x0000000000ff0000ULL)<<8)  | \
                      (((uint64_t)(x)&(uint64_t)0x000000000000ff00ULL)<<24) | \
                      (((uint64_t)(x)&(uint64_t)0x00000000000000ffULL)<<40) | \
                      (((uint64_t)(x)&(uint64_t)0x0000000000000000ULL)<<56))

/* PhotoRec/TestDisk limits */
#define PHOTOREC_MAX_FILE_SIZE (((uint64_t)1<<41)-1)
#define PHOTOREC_MAX_SIZE_16 (((uint64_t)1<<15)-1)
#define PHOTOREC_MAX_SIZE_32 (((uint64_t)1<<31)-1)
#define PHOTOREC_MAX_SIG_OFFSET 65535
#define PHOTOREC_MAX_SIG_SIZE 4095
#define PHOTOREC_MAX_BLOCKSIZE 32*1024*1024

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _TESTDISK_TYPES_H */