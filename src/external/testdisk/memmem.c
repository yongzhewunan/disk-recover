/*
 * memmem.c - Memory search function implementation
 *
 * Provides a memmem replacement for Windows (which doesn't have memmem).
 */

#include <string.h>
#include "memmem.h"

void *td_memmem(const void *haystack, size_t haystacklen,
                const void *needle, size_t needlelen)
{
    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;

    if (needlelen == 0)
        return (void *)haystack;

    if (haystacklen < needlelen)
        return NULL;

    /* For short needles, use simple scan */
    if (needlelen == 1)
        return (void *)memchr(haystack, n[0], haystacklen);

    /* Use a simple two-byte quick search algorithm */
    size_t i;
    for (i = 0; i <= haystacklen - needlelen; i++)
    {
        if (h[i] == n[0] && h[i + 1] == n[1])
        {
            if (memcmp(h + i, n, needlelen) == 0)
                return (void *)(h + i);
        }
    }

    return NULL;
}