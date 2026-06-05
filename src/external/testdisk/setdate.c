/*
 * setdate.c - File date setting stub for TestDisk validators
 *
 * Copyright (C) 1998-2009 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover: Stub implementation, no actual file date modification.
 */

#include "td_config.h"
#include "types.h"
#include <stdio.h>
#include "log.h"
#include "setdate.h"

/**
 * set_date - Set the file's date and time (stub)
 * @pathname:  Path and name of the file to alter
 * @actime:    Date and time to set
 * @modtime:   Date and time to set
 *
 * Stub - disk-recover doesn't modify file dates during validation.
 * Return: 0 always
 */
int set_date(const char *pathname, time_t actime, time_t modtime)
{
  (void)pathname;
  (void)actime;
  (void)modtime;
  return 0;
}
