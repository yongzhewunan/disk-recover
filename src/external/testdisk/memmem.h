/*
 * memmem.h - Memory search function for disk-recover
 *
 * Provides a memmem replacement function for TestDisk validators.
 */

#ifndef _MEMMEM_H
#define _MEMMEM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Search for a byte sequence within a larger byte sequence.
 *
 * @param haystack    The data to search within.
 * @param haystacklen Length of haystack.
 * @param needle      The data to search for.
 * @param needlelen   Length of needle.
 * @return Pointer to the first occurrence, or NULL if not found.
 */
void *td_memmem(const void *haystack, size_t haystacklen,
                const void *needle, size_t needlelen);

/* Compatibility alias - TestDisk uses memmem */
#define memmem td_memmem

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _MEMMEM_H */