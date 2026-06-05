/*
 * setdate.h - File date setting stub for TestDisk validators
 *
 * Copyright (C) 2009 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover: Stub implementation, no actual file date modification.
 */
#ifndef _SETDATE_H
#define _SETDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

/* Stub - disk-recover doesn't modify file dates during validation */
int set_date(const char *pathname, time_t actime, time_t modtime);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _SETDATE_H */
