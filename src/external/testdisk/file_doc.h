/*
 * file_doc.h - DOC/OLE2 definitions for TestDisk validators
 *
 * Copyright (C) 2018 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover: Removed FRAMAC annotations, added MSVC compat.
 */
#ifndef _FILE_DOC_H
#define _FILE_DOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "td_config.h"
#include "filegen.h"

void file_check_doc_aux(file_recovery_t *file_recovery, const uint64_t offset);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _FILE_DOC_H */
