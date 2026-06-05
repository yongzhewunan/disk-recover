/*
 * common.h - Adapted for disk-recover
 *
 * Copyright (C) 1998-2007 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * This software is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _COMMON_H
#define _COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

/* Memory allocation wrapper */
void *MALLOC(size_t size);

/* Min/Max macros - MSVC compatible */
#ifdef _MSC_VER
  #define td_min(x, y) ((x) < (y) ? (x) : (y))
  #define td_max(x, y) ((x) > (y) ? (x) : (y))
#else
  #define td_min(x,y) ({ \
      typeof(x) _x = (x); \
      typeof(y) _y = (y); \
      _x < _y ? _x : _y; })
  #define td_max(x,y) ({ \
      typeof(x) _x = (x); \
      typeof(y) _y = (y); \
      _x > _y ? _x : _y; })
#endif

/* Buffer read helpers - shared across validators */
uint32_t td_get_be32(const void *buffer, const unsigned int offset);
uint64_t td_get_be64(const void *buffer, const unsigned int offset);
uint16_t td_get_le16(const void *buffer, const unsigned int offset);
uint32_t td_get_le32(const void *buffer, const unsigned int offset);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _COMMON_H */