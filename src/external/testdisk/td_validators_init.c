/*
 * td_validators_init.c - TestDisk validator registration for disk-recover
 *
 * This file provides the initialization function that calls each
 * validator's register_header_check function.
 *
 * Each validator is compiled as a separate translation unit with
 * SINGLE_FORMAT and SINGLE_FORMAT_xxx defined, which makes each
 * file self-contained with its own helper functions.
 *
 * Copyright (C) 2024
 * Based on TestDisk/PhotoRec by Christophe GRENIER
 */

#include "td_config.h"
#include "types.h"
#include "filegen.h"

/* ========================================================================
 * Validator declarations - extern file_hint_t and register functions
 * ======================================================================== */

/* Adobe family */
extern const file_hint_t file_hint_psd;
extern const file_hint_t file_hint_psb;
extern const file_hint_t file_hint_abr;
extern const file_hint_t file_hint_indd;
extern const file_hint_t file_hint_pdf;
extern const file_hint_t file_hint_afdesign;

/* Images */
extern const file_hint_t file_hint_jpg;
extern const file_hint_t file_hint_bmp;
extern const file_hint_t file_hint_png;
extern const file_hint_t file_hint_gif;
extern const file_hint_t file_hint_tiff;

/* Videos */
extern const file_hint_t file_hint_mov;
extern const file_hint_t file_hint_riff;
extern const file_hint_t file_hint_mkv;
extern const file_hint_t file_hint_flv;
extern const file_hint_t file_hint_asf;

/* Documents */
extern const file_hint_t file_hint_doc;

/* Archives */
extern const file_hint_t file_hint_zip;
extern const file_hint_t file_hint_7z;
extern const file_hint_t file_hint_rar;

/* Audio */
extern const file_hint_t file_hint_flac;
extern const file_hint_t file_hint_mp3;

/* ========================================================================
 * Registration Function
 * ======================================================================== */

/**
 * @brief Register all TestDisk validators.
 *
 * This function calls the register_header_check function for each
 * file format validator, setting up the signature patterns in the
 * global file_check_list.
 */
void td_register_all_validators(void)
{
    static file_stat_t file_stat;

    /* Adobe family */
    file_hint_psd.register_header_check(&file_stat);
    file_hint_psb.register_header_check(&file_stat);
    file_hint_abr.register_header_check(&file_stat);
    file_hint_indd.register_header_check(&file_stat);
    file_hint_pdf.register_header_check(&file_stat);
    file_hint_afdesign.register_header_check(&file_stat);

    /* Images */
    file_hint_jpg.register_header_check(&file_stat);
    file_hint_bmp.register_header_check(&file_stat);
    file_hint_png.register_header_check(&file_stat);
    file_hint_gif.register_header_check(&file_stat);
    file_hint_tiff.register_header_check(&file_stat);

    /* Videos */
    file_hint_mov.register_header_check(&file_stat);
    file_hint_riff.register_header_check(&file_stat);
    file_hint_mkv.register_header_check(&file_stat);
    file_hint_flv.register_header_check(&file_stat);
    file_hint_asf.register_header_check(&file_stat);

    /* Documents */
    file_hint_doc.register_header_check(&file_stat);

    /* Archives */
    file_hint_zip.register_header_check(&file_stat);
    file_hint_7z.register_header_check(&file_stat);
    file_hint_rar.register_header_check(&file_stat);

    /* Audio */
    file_hint_flac.register_header_check(&file_stat);
    file_hint_mp3.register_header_check(&file_stat);
}