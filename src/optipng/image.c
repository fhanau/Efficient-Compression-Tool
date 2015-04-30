/*
 * opngcore/image.c
 * Image manipulation.
 *
 * Copyright (C) 2001-2011 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file.
 */

/*Modified by Felix Hanau.*/

#include <stdlib.h>
#include <string.h>

#include "image.h"

/*
 * Deallocate memory in a manner that is compatible with libpng's memory
 * allocation/deallocation routines, png_malloc() and png_free().
 * If those routines change, this must be changed accordingly.
 */
static void opng_free(void *ptr)
{
    free(ptr);
}

/*
 * Initializes an image object.
 */
void opng_init_image(struct opng_image *image)
{
    memset(image, 0, sizeof(*image));
}

/*
 * Loads an image object from libpng structures.
 */
void opng_load_image(struct opng_image *image, png_structp png_ptr, png_infop info_ptr)
{
    memset(image, 0, sizeof(*image));

    png_get_IHDR(png_ptr, info_ptr,
        &image->width, &image->height, &image->bit_depth, &image->color_type,
        &image->interlace_type, &image->compression_type, &image->filter_type);
    image->row_pointers = png_get_rows(png_ptr, info_ptr);
    png_get_PLTE(png_ptr, info_ptr, &image->palette, &image->num_palette);
    /* Transparency information is not metadata, although tRNS is ancillary. */
    if (png_get_tRNS(png_ptr, info_ptr,
        &image->trans_alpha, &image->num_trans, &image->trans_color_ptr))
    {
        /* Double copying (pointer + value) is necessary here
         * due to an inconsistency in the libpng design.
         */
        if (image->trans_color_ptr != NULL)
        {
            image->trans_color = *image->trans_color_ptr;
            image->trans_color_ptr = &image->trans_color;
        }
    }

    if (png_get_bKGD(png_ptr, info_ptr, &image->background_ptr))
    {
        /* Same problem as in tRNS. */
        image->background = *image->background_ptr;
        image->background_ptr = &image->background;
    }
    png_get_hIST(png_ptr, info_ptr, &image->hist);
    if (png_get_sBIT(png_ptr, info_ptr, &image->sig_bit_ptr))
    {
        /* Same problem as in tRNS. */
        image->sig_bit = *image->sig_bit_ptr;
        image->sig_bit_ptr = &image->sig_bit;
    }
    image->num_unknowns = png_get_unknown_chunks(png_ptr, info_ptr, &image->unknowns);
}

/*
 * Stores an image object into libpng structures.
 */
void opng_store_image(struct opng_image *image, png_structp png_ptr, png_infop info_ptr)
{
    png_set_IHDR(png_ptr, info_ptr,
        image->width, image->height, image->bit_depth, image->color_type,
        image->interlace_type, image->compression_type, image->filter_type);
    png_set_rows(png_ptr, info_ptr, image->row_pointers);
    if (image->palette != NULL)
        png_set_PLTE(png_ptr, info_ptr, image->palette, image->num_palette);
    /* Transparency information is not metadata, although tRNS is ancillary. */
    if (image->trans_alpha != NULL || image->trans_color_ptr != NULL)
        png_set_tRNS(png_ptr, info_ptr,
            image->trans_alpha, image->num_trans, image->trans_color_ptr);
    if (image->background_ptr != NULL)
        png_set_bKGD(png_ptr, info_ptr, image->background_ptr);
    if (image->hist != NULL)
        png_set_hIST(png_ptr, info_ptr, image->hist);
    if (image->sig_bit_ptr != NULL)
        png_set_sBIT(png_ptr, info_ptr, image->sig_bit_ptr);
    if (image->num_unknowns != 0)
    {
        png_set_unknown_chunks(png_ptr, info_ptr, image->unknowns, image->num_unknowns);
        /* Is this really necessary? Should it not be implemented in libpng? */
        for (int i = 0; i < image->num_unknowns; ++i)
            png_set_unknown_chunk_location(png_ptr, info_ptr, i, image->unknowns[i].location);
    }
}

/*
 * Clears an image object.
 */
void opng_clear_image(struct opng_image *image)
{
    if (image->row_pointers == NULL)
        return;  /* nothing to clean up */

    for (png_uint_32 i = 0; i < image->height; ++i)
        opng_free(image->row_pointers[i]);
    opng_free(image->row_pointers);
    opng_free(image->palette);
    opng_free(image->trans_alpha);
    opng_free(image->hist);
    for (int j = 0; j < image->num_unknowns; ++j)
        opng_free(image->unknowns[j].data);
    opng_free(image->unknowns);
    /* DO NOT deallocate background_ptr, sig_bit_ptr, trans_color_ptr.
     * See the comments regarding double copying inside opng_load_image_info().
     */

    /* Clear the space here and do not worry about double-deallocation issues
     * that might arise later on.
     */
    memset(image, 0, sizeof(*image));
}
