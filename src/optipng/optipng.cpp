/*
 * OptiPNG: Advanced PNG optimization program.
 * http://optipng.sourceforge.net/
 *
 * Copyright (C) 2001-2012 Cosmin Truta and the Contributing Authors.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file.
 *
 * PNG optimization is described in detail in the PNG-Tech article
 * "A guide to PNG optimization"
 * http://optipng.sourceforge.net/pngtech/png_optimization.html
 *
 * The idea of running multiple compression trials with different
 * PNG filters and zlib parameters is inspired from the pngcrush
 * program by Glenn Randers-Pehrson.
 * The idea of performing lossless image reductions is inspired
 * from the pngrewrite program by Jason Summers.
 */

/* Modified by Felix Hanau. */

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "opngcore.h"
#include "codec.h"
#include "image.h"
#include "../support.h"
#include "../main.h"

//The user options structure
struct opng_options
{
    int fix;
    int nb, nc, np, nz;
    int optim_level;
};

// The optimization engine
struct opng_optimizer
{
    const opng_transformer_t *transformer;
    struct opng_options options;
};

// The optimization session structure
struct opng_session
{
    const struct opng_options *options;
    const opng_transformer_t *transformer;
    struct opng_image image;
    struct opng_encoding_stats in_stats;
    struct opng_encoding_stats out_stats;
    const char *Infile;
    const char *Outfile;
    png_uint_32 flags;
};

// Creates an optimizer object.
static opng_optimizer * opng_create_optimizer()
{
    return (opng_optimizer *)calloc(1, sizeof(struct opng_optimizer));
}

/*
 * Prints a warning message (level = OPNG_MSG_WARNING) to the logger.
 */
void opng_warning(const char *fname, const char *message)
{
    fprintf(stderr, "%s: warning: %s\n", fname != NULL ? fname : "ECT", message);
}

/*
 * Prints an error message (level = OPNG_MSG_ERROR), optionally accompanied
 * by a submessage, to the logger.
 */
void opng_error(const char *fname, const char *message)
{
    fprintf(stderr, "%s: error: %s\n", fname != NULL ? fname : "ECT", message);
}

#ifndef NDEBUG

static bool logging;

/*
 * Prints a printf-formatted informational message (level = OPNG_MSG_INFO)
 * to the logger.
 */
#ifdef __GNUC__
__attribute__ ((format (printf, 1, 2)))
#endif
void opng_printf(const char *format, ...)
{
    if (!logging)
        return;
    va_list arg_ptr;
    va_start(arg_ptr, format);
    if (format[0] != 0)
        vfprintf(stderr, format, arg_ptr);
    va_end(arg_ptr);
    return;
}

#else
void opng_printf(
#ifdef __GNUC__
                 __attribute__((__unused__))
#endif
                 const char *format, ...){}
#endif

// Reads an image from an image file stream. Reduces the image if possible.
static int opng_read_file(struct opng_session *session, FILE *stream, bool force_palette_if_possible, bool force_no_palette)
{
    struct opng_codec_context context;
    struct opng_image *image = &session->image;
    struct opng_encoding_stats *stats = &session->in_stats;

    opng_init_codec_context(&context, image, stats, session->transformer);
    if (opng_decode_image(&context, stream, session->Infile, force_no_palette) < 0)
    {
        opng_decode_finish(&context, 1);
        return -1;
    }
    const struct opng_options *options = session->options;
    // Choose the applicable image reductions.
    int reductions = OPNG_REDUCE_ALL;
    if (options->nz || stats->flags & OPNG_HAS_DIGITAL_SIGNATURE || stats->flags & OPNG_HAS_MULTIPLE_IMAGES || force_no_palette)
    {
        // Do not reduce files with PNG datastreams under -nz, signed files or files with APNG chunks.
        reductions = OPNG_REDUCE_NONE;
    }
    else {
    if (options->nb)
        reductions &= ~OPNG_REDUCE_BIT_DEPTH;
    if (options->nc)
        reductions &= ~OPNG_REDUCE_COLOR_TYPE;
    if (options->np)
        reductions &= ~OPNG_REDUCE_PALETTE;
    }
    // Try to reduce the image.
    if (reductions != OPNG_REDUCE_NONE){
        reductions = opng_decode_reduce_image(&context, reductions, force_palette_if_possible);
    }
    if (reductions < 0)
    {
        opng_error(session->Infile, "An unexpected error occurred while reducing the image");
        opng_decode_finish(&context, 1);
        return -1;
    }
    if (!stats->flags & OPNG_HAS_MULTIPLE_IMAGES)
        image->interlace_type = PNG_INTERLACE_NONE;
    // Keep the loaded image data.
    opng_decode_finish(&context, 0);
    return 0;
}

// Writes an image to a PNG file stream.
static int opng_write_file(struct opng_session *session, int filter, FILE *stream, int mode)
{
    struct opng_codec_context context;
    opng_init_codec_context(&context,
                            &session->image,
                            &session->out_stats,
                            session->transformer);
    return opng_encode_image(&context, filter, stream, session->Outfile, mode);
}

// PNG file copying
static int opng_copy_file(struct opng_session *session, FILE *in_stream, FILE *out_stream)
{
    struct opng_codec_context context;
    opng_init_codec_context(&context, NULL, &session->out_stats, session->transformer);
    return opng_copy_png(&context, in_stream, session->Infile, out_stream, session->Outfile);
}

static int opng_optimize_impl(struct opng_session *session, const char *Infile, bool force_palette_if_possible, bool force_no_palette)
{
    FILE * fstream = fopen(Infile, "rb");
    if (fstream == NULL)
    {
        opng_error(Infile, "Can't open file");
        return -1;
    }
    int result = opng_read_file(session, fstream, force_palette_if_possible, force_no_palette);
    fclose(fstream);
    if (result < 0)
        return result;
    const struct opng_options * options = session->options;
    session->flags = session->in_stats.flags;

    // Check the error flag. This must be the first check.
    if (session->flags & OPNG_HAS_ERRORS)
    {
        if (options->fix)
        {
            opng_printf("Recoverable errors found in input. Fixing...\n");
        }
        else
        {
            return -1;
        }
    }
    
    // Check the digital signature flag.
    if (session->flags & OPNG_HAS_DIGITAL_SIGNATURE)
    {
        opng_error(Infile,"This file is digitally signed and can't be processed");
        return -1;
    }
    if (options->nz && !session->flags & OPNG_HAS_SNIPPED_IMAGES && !session->flags & OPNG_HAS_STRIPPED_METADATA){
        return 0;
    }

    //R/W access + existing files
    if (writepermission(Infile) !=1){
        opng_error(Infile, "Can't write file");
        return -1;
    }
    // Check the backup file.
    if ((!force_no_palette && exists(((std::string)Infile).append(".bak").c_str()) > 0) || (!options->nz && (exists(((std::string)Infile).append(".bak2").c_str()) > 0)))
    {   opng_error(Infile, "Can't back up the output file");
        return -1;
    }
    // Todo: check backup write perms
    if (!force_no_palette) {
    rename(Infile, (((std::string)Infile).append(".bak")).c_str());
    }
    int optimal_filter = -1;
    if (options->nz){
        fstream = fopen((((std::string)Infile).append(".bak")).c_str(), "rb");
        if (fstream == NULL)
        {opng_error(Infile, "Can't reopen file");}
        else if (fseek(fstream, session->in_stats.datastream_offset, SEEK_SET) != 0){
            opng_error(Infile, "Can't reposition file");
            fclose(fstream);
        }
        else {
            FILE * out_stream = fopen(Infile, "wb");
            if (out_stream == NULL)
            {
                opng_error(Infile, "Can't open file for writing");
            }
            else {opng_copy_file(session, fstream, out_stream);
                fclose(fstream);
                fclose(out_stream);
                unlink((((std::string)Infile).append(".bak")).c_str());}
        }
        return 0;
    }
    else {
        optk_uint64_t best_idat = 0;
        optimal_filter = 0;
        /* Iterate through filters PNG_FILTER_NONE and PNG_ALL_FILTERS. */
        for (int filter = 0;
             filter <= 5; filter+=5)
        {
            fstream = fopen((filter == 0 && !force_no_palette) ? Infile: ((std::string)Infile).append(".bak2").c_str(), "wb");
            opng_write_file(session, filter, fstream, options->optim_level);
            fclose(fstream);
            if (filter == 0){
                best_idat = session->out_stats.idat_size;
            }
            else {
                if (best_idat <= session->out_stats.idat_size){unlink((((std::string)Infile).append(".bak2")).c_str());
                }
                else{
                    if (force_no_palette){unlink((((std::string)Infile).append(".bak2")).c_str());}
                    else{unlink(Infile);
                        rename((((std::string)Infile).append(".bak2")).c_str(), Infile);}
                    optimal_filter = filter;}
            }
        }
    }
    return optimal_filter;
}

static int opng_optimize_file(opng_optimizer *optimizer, const char *Infile, bool force_palette_if_possible, bool force_no_palette)
{
    struct opng_session session;
    const struct opng_options * options = &optimizer->options;
    memset(&session, 0, sizeof(session));
    session.options = options;
    session.transformer = optimizer->transformer;
    opng_init_image(&session.image);
    int optimal_filter = opng_optimize_impl(&session, Infile, force_palette_if_possible, force_no_palette);
    opng_clear_image(&session.image);
    return optimal_filter;
}

static struct opng_options options;

int Optipng(int level, const char * Infile, bool force_palette_if_possible, bool force_no_palette)
{
    memset(&options, 0, sizeof(options));
    opng_optimizer *the_optimizer = opng_create_optimizer();
    opng_transformer_t *the_transformer = opng_create_transformer();
    //Logging works only if NDEBUG is not defined and should be used only for testing
    //logging = true;

    if (level == 0){
        options.nz=1;
        //Strip chunks
        opng_transform_chunk (the_transformer, "all", 1);
        //Strip APNG chunks
        //opng_transform_chunk (the_transformer, "apngc", 1);
    }
    else{
        options.optim_level = level;
    }
    the_optimizer->options = options;
    the_optimizer->transformer = opng_seal_transformer(the_transformer);
    int val = opng_optimize_file(the_optimizer, Infile, force_palette_if_possible, force_no_palette);
    free(the_optimizer);
    opng_destroy_transformer(the_transformer);
    return val;
}
