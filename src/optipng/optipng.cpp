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
    const char *in_fname;
    const char *out_fname;
    png_uint_32 flags;
};

// Creates an optimizer object.
static opng_optimizer_t * opng_create_optimizer()
{
    return (opng_optimizer_t *)calloc(1, sizeof(struct opng_optimizer));
}

static unsigned logging_level = OPNG_MSG_DEFAULT;

/*
 * Sets the logging severity level.
 */
void opng_set_logging(unsigned level)
{
    logging_level = level;
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
// Prints a line of information regarding the image representation.
static void opng_print_image_info_line(const struct opng_image *image)
{
    static const int type_channels[8] = {1, 0, 3, 1, 2, 0, 4, 0};

    /* Print the channel depth and the bit depth. */
    int channels = type_channels[image->color_type & 7];
    if (channels != 1)
        opng_printf("   %dx%d bits/pixel", channels, image->bit_depth);
    else {
        opng_printf("   %d bit%s/pixel", image->bit_depth, image->bit_depth != 1 ? "s" : "");
    }

    /* Print the color information and the image type. */
    if (image->color_type & PNG_COLOR_MASK_PALETTE)
    {
        opng_printf(", %d color%s%s in palette\n", image->num_palette, image->num_palette > 1 ? "s": "", image->num_trans > 0 ? " (%d transparent)" : "");
    }
    else
    {
        opng_printf(", %s%s%s\n", image->color_type & PNG_COLOR_MASK_COLOR ? "RGB" : "grayscale", image->color_type & PNG_COLOR_MASK_ALPHA ? "+alpha" : "",
                    image->trans_color_ptr != NULL ? "+transparency" : "");
    }
}

/*
 * Prints a printf-formatted informational message (level = OPNG_MSG_INFO)
 * to the logger.
 */
#ifdef __GNUC__
__attribute__ ((format (printf, 1, 2)))
#endif
void opng_printf(const char *format, ...)
{
    if (logging_level > OPNG_MSG_INFO)
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
    if (opng_decode_image(&context, stream, session->in_fname, force_no_palette) < 0)
    {
        opng_decode_finish(&context, 1);
        return -1;
    }

    // Display the input image file information.
    opng_printf("   %u pixels, %sPNG format%s\n",
                image->width*image->height,
                stats->flags &  OPNG_HAS_MULTIPLE_IMAGES ? "A" : "",
                image->interlace_type != PNG_INTERLACE_NONE ? ", interlaced" : "");
    opng_printf("Processing:\n");
#ifndef NDEBUG
    opng_print_image_info_line(image);
#endif
    if (stats->flags & OPNG_HAS_SNIPPED_IMAGES)
    {
        opng_printf("Snipping:\n");
    }

    if (stats->flags & OPNG_HAS_STRIPPED_METADATA)
    {
        opng_printf("Stripping metadata:\n");
    }
    const struct opng_options *options = session->options;

    // Choose the applicable image reductions.
    int reductions = OPNG_REDUCE_ALL;
    if (options->nz  || stats->flags & OPNG_HAS_DIGITAL_SIGNATURE || stats->flags & OPNG_HAS_MULTIPLE_IMAGES || force_no_palette)
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
        opng_printf("Reducing:\n");
#ifndef NDEBUG
        opng_print_image_info_line(image);
#endif
    }
    if (reductions < 0)
    {
        opng_error(session->in_fname, "An unexpected error occurred while reducing the image");
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
    int result = opng_encode_image(&context, filter, stream, session->out_fname, mode);
    opng_encode_finish(&context);
    return result;
}

// PNG file copying
static int opng_copy_file(struct opng_session *session, FILE *in_stream, FILE *out_stream)
{
    struct opng_codec_context context;
    opng_init_codec_context(&context, NULL, &session->out_stats, session->transformer);
    return opng_copy_png(&context, in_stream, session->in_fname, out_stream, session->out_fname);
}

static int opng_optimize_impl(struct opng_session *session, const char *in_fname, bool force_palette_if_possible, bool force_no_palette)
{
    int optimal_filter = -1;
    FILE * fstream = fopen(in_fname, "rb");
    if (fstream == NULL)
    {
        opng_error(in_fname, "Can't open file");
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
        else{
        return -1;
        }
    }
    
    // Check the digital signature flag.
    if (session->flags & OPNG_HAS_DIGITAL_SIGNATURE)
    {
        opng_error(in_fname,"This file is digitally signed and can't be processed");
        return -1;
    }
    if (options->nz && !session->flags & OPNG_HAS_SNIPPED_IMAGES && !session->flags & OPNG_HAS_STRIPPED_METADATA){
        return 0;}

    //R/W access + existing files
    if (writepermission(in_fname) !=1){
        opng_error(in_fname, "Can't write file");
        return -1;
    }
    // Check the backup file.
    if ((!force_no_palette && exists(((std::string)in_fname).append(".bak").c_str()) > 0) || (!options->nz && (exists(((std::string)in_fname).append(".bak2").c_str()) > 0)))
    {   opng_error(in_fname, "Can't back up the output file");
        return -1;
    }
    // Todo: check backup write perms
    if (!force_no_palette) {
    rename(in_fname, (((std::string)in_fname).append(".bak")).c_str());
    }
    if (options->nz){
        fstream = fopen((((std::string)in_fname).append(".bak")).c_str(), "rb");
        if (fstream == NULL)
        {opng_error(in_fname, "Can't reopen file");}
        else if (fseek(fstream, session->in_stats.datastream_offset, SEEK_SET) != 0){
            opng_error(in_fname, "Can't reposition file");
            fclose(fstream);
        }
        else {
            FILE * out_stream = fopen(in_fname, "wb");
            if (out_stream == NULL)
            {
                opng_error(in_fname, "Can't open file for writing");
            }
            else {opng_copy_file(session, fstream, out_stream);
                fclose(fstream);
                fclose(out_stream);
                unlink((((std::string)in_fname).append(".bak")).c_str());}
        }
    }
    else {
        optk_uint64_t best_idat = 0;
        /* Iterate through the "hyper-rectangle" (zc, zm, zs, f). */
        for (int filter = OPNG_FILTER_MIN;
             filter <= OPNG_FILTER_MAX; ++filter)
        {
            if (filter > 0 && filter < 5 && options->optim_level != 5){continue;}
            if (force_no_palette){
                fstream = fopen(((std::string)in_fname).append(".bak2").c_str(), "wb");
            }
            else {
                fstream = fopen(filter == OPNG_FILTER_MIN ? in_fname: ((std::string)in_fname).append(".bak2").c_str(), "wb");
            }
            opng_write_file(session, filter, fstream, options->optim_level);
            fclose(fstream);
            if (filter == OPNG_FILTER_MIN){
                optimal_filter = filter;
                best_idat = session->out_stats.idat_size;
            }
            else {
                if (best_idat<=session->out_stats.idat_size){unlink((((std::string)in_fname).append(".bak2")).c_str());
                }
                else{
                    if (force_no_palette){unlink((((std::string)in_fname).append(".bak2")).c_str());}
                    else{unlink(in_fname);
                        rename((((std::string)in_fname).append(".bak2")).c_str(), in_fname);}
                    best_idat = session->out_stats.idat_size;
                    optimal_filter = filter;}
            }
        }
    }
    return optimal_filter;
}

static int opng_optimize_file(opng_optimizer_t *optimizer, const char *in_fname, bool force_palette_if_possible, bool force_no_palette)
{
    struct opng_session session;
    const struct opng_options * options = &optimizer->options;
    memset(&session, 0, sizeof(session));
    session.options = options;
    session.transformer = optimizer->transformer;
    opng_init_image(&session.image);
    int optimal_filter = opng_optimize_impl(&session, in_fname, force_palette_if_possible, force_no_palette);
    opng_clear_image(&session.image);
    return optimal_filter;
}

static struct opng_options options;

int Optipng(int filter, const char * Input, bool force_palette_if_possible, bool force_no_palette)
{
    memset(&options, 0, sizeof(options));
    opng_optimizer_t *the_optimizer = opng_create_optimizer();
    opng_transformer_t *the_transformer = opng_create_transformer();
    //Logging works only if NDEBUG is not defined and should be used only for testing
    //opng_set_logging(OPNG_MSG_INFO);

    if (filter==0){
        options.nz=1;
        opng_transform_chunk (the_transformer, "all",1);
        //Protect chunks
        //opng_transform_chunk (the_transformer, "all",0);
        //Strip APNG chunks
        //opng_transform_chunk (the_transformer, "apngc",1);
    }
    else{
        options.optim_level = filter;
    }
    the_optimizer->options = options;
    the_optimizer->transformer = opng_seal_transformer(the_transformer);
    int rval = opng_optimize_file(the_optimizer, Input, force_palette_if_possible, force_no_palette);
    free(the_optimizer);
    opng_destroy_transformer(the_transformer);
    return rval;
}
