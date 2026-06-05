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
    /* Each file_hint_t gets its own file_stat_t so the adapter can
       identify which format a given file_check_t entry belongs to
       by comparing fc->file_stat against the per-hint stat.

       CRITICAL: file_stat->file_hint MUST be set before calling
       register_header_check(), because register_header_check() only
       sets file_check_new->file_stat = file_stat but does NOT set
       file_stat->file_hint. Without this back-pointer, the adapter
       cannot determine which file_hint a file_check_t belongs to. */
    static file_stat_t stat_psd, stat_psb, stat_abr, stat_indd, stat_pdf, stat_afdesign;
    static file_stat_t stat_jpg, stat_bmp, stat_png, stat_gif, stat_tiff;
    static file_stat_t stat_mov, stat_riff, stat_mkv, stat_flv, stat_asf;
    static file_stat_t stat_doc, stat_zip, stat_7z, stat_rar, stat_flac, stat_mp3;

    /* Adobe family - set file_hint back-pointer before registration */
    stat_psd.file_hint = &file_hint_psd;
    file_hint_psd.register_header_check(&stat_psd);
    stat_psb.file_hint = &file_hint_psb;
    file_hint_psb.register_header_check(&stat_psb);
    stat_abr.file_hint = &file_hint_abr;
    file_hint_abr.register_header_check(&stat_abr);
    stat_indd.file_hint = &file_hint_indd;
    file_hint_indd.register_header_check(&stat_indd);
    stat_pdf.file_hint = &file_hint_pdf;
    file_hint_pdf.register_header_check(&stat_pdf);
    stat_afdesign.file_hint = &file_hint_afdesign;
    file_hint_afdesign.register_header_check(&stat_afdesign);

    /* Images */
    stat_jpg.file_hint = &file_hint_jpg;
    file_hint_jpg.register_header_check(&stat_jpg);
    stat_bmp.file_hint = &file_hint_bmp;
    file_hint_bmp.register_header_check(&stat_bmp);
    stat_png.file_hint = &file_hint_png;
    file_hint_png.register_header_check(&stat_png);
    stat_gif.file_hint = &file_hint_gif;
    file_hint_gif.register_header_check(&stat_gif);
    stat_tiff.file_hint = &file_hint_tiff;
    file_hint_tiff.register_header_check(&stat_tiff);

    /* Videos */
    stat_mov.file_hint = &file_hint_mov;
    file_hint_mov.register_header_check(&stat_mov);
    stat_riff.file_hint = &file_hint_riff;
    file_hint_riff.register_header_check(&stat_riff);
    stat_mkv.file_hint = &file_hint_mkv;
    file_hint_mkv.register_header_check(&stat_mkv);
    stat_flv.file_hint = &file_hint_flv;
    file_hint_flv.register_header_check(&stat_flv);
    stat_asf.file_hint = &file_hint_asf;
    file_hint_asf.register_header_check(&stat_asf);

    /* Documents */
    stat_doc.file_hint = &file_hint_doc;
    file_hint_doc.register_header_check(&stat_doc);

    /* Archives */
    stat_zip.file_hint = &file_hint_zip;
    file_hint_zip.register_header_check(&stat_zip);
    stat_7z.file_hint = &file_hint_7z;
    file_hint_7z.register_header_check(&stat_7z);
    stat_rar.file_hint = &file_hint_rar;
    file_hint_rar.register_header_check(&stat_rar);

    /* Audio */
    stat_flac.file_hint = &file_hint_flac;
    file_hint_flac.register_header_check(&stat_flac);
    stat_mp3.file_hint = &file_hint_mp3;
    file_hint_mp3.register_header_check(&stat_mp3);
}