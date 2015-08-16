/*
 * opngcore/codec.h
 * PNG encoding and decoding.
 *
 * Copyright (C) 2001-2012 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file.
 */

/*Modified by Felix Hanau.*/

#ifndef OPNGCORE_CODEC_H
#define OPNGCORE_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "opngtrans.h"
#include "opngreduc/opngreduc.h"

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Use the Standard C minimum-width integer types.
 *
 * The exact-width types intN_t and uintN_t are not guaranteed to exist
 * for all N=8,16,32,64. For example, certain dedicated CPUs may handle
 * 32-bit and 64-bit integers only, with sizeof(int)==sizeof(char)==1.
 * On the other hand, any minimum-width integer type is guaranteed to be
 * the same as its exact-width counterpart, when the latter does exist.
 *
 * Since exact-width integer semantics is not strictly required, we use
 * int_leastN_t and uint_leastN_t, which are required to exist for all
 * N=8,16,32,64.
 */

typedef int_least64_t  optk_int64_t;
typedef uint_least64_t optk_uint64_t;

#define OPTK_INT64_MAX  INT_LEAST64_MAX

/*
 * The encoding statistics structure.
 */
struct opng_encoding_stats
{
    optk_uint64_t idat_size;
    optk_int64_t datastream_offset;
    png_uint_32 flags;
    bool first;
};

/*
 * The codec context structure.
 * Everything that libpng and its callbacks use is found in here.
 * Although this structure is exposed so that it can be placed on
 * the stack, the caller must pretend not to know what's inside.
 */
struct opng_codec_context
{
    struct opng_image *image;
    struct opng_encoding_stats *stats;
    FILE *stream;
    const char *fname;
    png_structp libpng_ptr;
    png_infop info_ptr;
    const opng_transformer_t *transformer;
    optk_int64_t crt_idat_offset;
    optk_uint64_t crt_idat_size;
    png_uint_32 crt_idat_crc;
    int crt_chunk_is_allowed;
    int crt_chunk_is_idat;
    bool no_write;
};

/*
 * Encoding flags.
 */
enum
{
    OPNG_HAS_DIGITAL_SIGNATURE = 0x0008,
    OPNG_HAS_MULTIPLE_IMAGES   = 0x0010,
    OPNG_HAS_SNIPPED_IMAGES    = 0x0020,
    OPNG_HAS_STRIPPED_METADATA = 0x0040,
    OPNG_HAS_ERRORS            = 0x0100
};

/*
 * Initializes a codec context object.
 */
void opng_init_codec_context(struct opng_codec_context *context, struct opng_image *image, struct opng_encoding_stats *stats, const opng_transformer_t *transformer);

/*
 * Decodes an image from an image file stream.
 * The image may be either in PNG format or in an external file format.
 * The function returns 0 on success or -1 on error.
 */
int opng_decode_image(struct opng_codec_context *context, FILE *stream, const char *fname, bool force_no_palette);

/*
 * Attempts to reduce the imported image.
 * The function returns a mask of successful reductions (0 for no reductions),
 * or -1 on error.
 * No error is normally expected to occur; if it does, it indicates a defect.
 */
int opng_decode_reduce_image(struct opng_codec_context *context, int reductions);

/*
 * Stops the decoder.
 * Frees the stored PNG image data and clears the internal image object,
 * if required.
 */
void opng_decode_finish(struct opng_codec_context *context, int free_data);

/*
 * Encodes an image to a PNG file stream.
 * If the output file stream is NULL, PNG encoding is still done,
 * and statistics are still collected, but no actual data is written.
 * The function returns 0 on success or -1 on error.
 */
int opng_encode_image(struct opng_codec_context *context, int filter, FILE *stream, const char *fname, int mode);

/*
 * Copies a PNG file stream to another PNG file stream.
 * The function returns 0 on success or -1 on error.
 */
int opng_copy_png(struct opng_codec_context *context, FILE *in_stream, const char *Infile, FILE *out_stream, const char *Outfile);

/*
 * Tests whether the given chunk is an image chunk.
 * An image chunk is a chunk that stores image data: a critical chunk or tRNS.
 *
 * It can be argued that APNG fdAT is also an image chunk, but we don't
 * consider that here. We handle APNG separately.
 */
int opng_is_image_chunk(const png_byte *chunk_type);

/*
 * Tests whether the given chunk is an APNG chunk.
 */
int opng_is_apng_chunk(const png_byte *chunk_type);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* OPNGCORE_CODEC_H */
