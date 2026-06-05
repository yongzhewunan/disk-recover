/*
 * common.c - Adapted for disk-recover
 *
 * Copyright (C) 1998-2007 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * This software is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "common.h"

/* Memory allocation wrapper with null check */
void *MALLOC(size_t size)
{
    void *res;
    res = malloc(size);
    if(!res)
    {
        fprintf(stderr,"MALLOC failed: %lu bytes requested\n", (unsigned long)size);
        exit(1);
    }
    return res;
}

/* Big-endian buffer read helpers */
uint32_t td_get_be32(const void *buffer, const unsigned int offset)
{
    const uint8_t *p = (const uint8_t *)buffer + offset;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

uint64_t td_get_be64(const void *buffer, const unsigned int offset)
{
    const uint8_t *p = (const uint8_t *)buffer + offset;
    return ((uint64_t)td_get_be32(buffer, offset) << 32) |
           (uint64_t)td_get_be32(buffer, offset + 4);
}

uint16_t td_get_le16(const void *buffer, const unsigned int offset)
{
    const uint8_t *p = (const uint8_t *)buffer + offset;
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t td_get_le32(const void *buffer, const unsigned int offset)
{
    const uint8_t *p = (const uint8_t *)buffer + offset;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}