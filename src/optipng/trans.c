/*
 * opngtrans/chunksig.c
 * Image Transformation & Chunk signatures.
 *
 * Copyright (C) 2011 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file.
 */

/*Modified by Felix Hanau.*/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "trans.h"
#include "codec.h"
#include "opngtrans.h"

/*
 * Initializes a signature set.
 */
static void opng_sigs_init(struct opng_sigs *sigs)
{
    memset(sigs, 0, sizeof(struct opng_sigs));
    sigs->sorted = 1;
}

/*
 * Clears a signature set.
 */
static void opng_sigs_clear(struct opng_sigs *sigs)
{
    free(sigs->buffer);
    opng_sigs_init(sigs);
}

/*
 * Compares two chunk signatures, for the benefit of qsort.
 */
static int opng_sigs_cmp(const void *sig1, const void *sig2)
{
    return memcmp(sig1, sig2, 4);
}

/*
 * Sorts and uniq's the array buffer that stores chunk signatures.
 */
static void opng_sigs_sort_uniq(struct opng_sigs *sigs)
{
    if (sigs->sorted)
        return;

    png_byte * buffer = sigs->buffer;
    size_t count = sigs->count;

    /* Sort. */
    qsort(buffer, count, 4, opng_sigs_cmp);
    size_t i, j;
    /* Uniq. */
    for (i = 1, j = 0; i < count; ++i)
    {
        if (memcmp(buffer + i * 4, buffer + j * 4, 4) == 0)
            continue;
        if (++j == i)
            continue;
        memcpy(buffer + j * 4, buffer + i * 4, 4);
    }
    sigs->count = ++j;
    sigs->sorted = 1;
}

/*
 * Returns 1 if a chunk signature is found in the given set,
 * or 0 otherwise.
 */
static int opng_sigs_find(const struct opng_sigs *sigs, const png_byte *chunk_sig)
{
    /* Perform a binary search. */
    int left = 0;
    int right = (int)sigs->count;
    while (left < right)
    {
        int mid = (left + right) / 2;
        int cmp = memcmp(chunk_sig, sigs->buffer + mid * 4, 4);
        if (cmp == 0)
            return 1;  /* found */
        else if (cmp < 0)
            right = mid - 1;
        else  /* cmp > 0 */
            left = mid + 1;
    }
    return 0;  /* not found */
}

/*
 * Converts a chunk name to a chunk signature and adds it to
 * a chunk signature set.
 */
static int opng_sigs_add(struct opng_sigs *sigs, const char *chunk_name)
{
    const png_byte * chunk_sig = (const png_byte *)chunk_name;

    /* If the current buffer is filled, reallocate a larger size. */
    if (sigs->count >= sigs->capacity)
    {
        if (sigs->capacity == 0)
            sigs->capacity = 32;
        else
            sigs->capacity *= 2;
        void * new_buffer = realloc(sigs->buffer, sigs->capacity * 4);
        if (new_buffer == NULL)
            return -1;
        sigs->buffer = (png_byte *)new_buffer;
    }

    /* Append the new signature to the buffer, which is no longer
     * considered to be sorted.
     */
    memcpy(sigs->buffer + sigs->count * 4, chunk_sig, 4);
    ++sigs->count;
    sigs->sorted = 0;
    return 0;
}

/*
 * Converts a string representing an object to an object id.
 */
static opng_id_t opng_string_to_id(const char *str)
{
    int is_chunk_name = strlen(str) == 4 && isalpha(str[0]) && isalpha(str[1]) &&isalpha(str[2]) && isalpha(str[3]);

    if (!is_chunk_name){
        if (strcmp(str, "all")==0){return OPNG_ID_ALL;}
        if (strcmp(str, "apngc")==0){return OPNG_ID_ANIMATION;}
        else{return OPNG_ID__UNKNOWN;}}

    /* Critical chunks and tRNS contain image data.
     * The other chunks contain metadata.
     */
    if (isupper(str[0]))
        return OPNG_ID_CHUNK_IMAGE;
    if (memcmp(str, "tRNS", 4) == 0)
        return OPNG_ID_CHUNK_IMAGE;
    if (memcmp(str, "acTL", 4) == 0 ||
        memcmp(str, "fcTL", 4) == 0 ||
        memcmp(str, "fdAT", 4) == 0)
        return OPNG_ID_CHUNK_ANIMATION;
    return OPNG_ID_CHUNK_META;
}

void opng_transform_chunk (opng_transformer_t *transformer, const char *chunk, int strip){
    opng_id_t id = opng_string_to_id(chunk);
    if (strip){
        transformer->strip_ids |= id;
    }
    else{transformer->protect_ids |= id;}
    if (id & OPNG_IDSET_CHUNK)
    {opng_sigs_add(strip ? &transformer->strip_sigs : &transformer->protect_sigs, chunk);
    }
    if ((id & OPNG_IDSET_CAN_TRANSFORM) == 0)
    {
        /* Missing or incorrect id. */
        if (strip){
            const char *err_message = NULL;
            if (id == OPNG_ID_CHUNK_IMAGE)
            {
                /* Tried to (but shouldn't) strip image data. */
                if (strncmp(chunk, "tRNS", 4) == 0)
                    err_message = "Can't strip tRNS";
                else
                    err_message = "Can't strip critical chunks";
            }
            else if (id == OPNG_ID_CHUNK_ANIMATION)
            {
                /* Tried to strip animation data. */
                /*TODO:*/
                err_message = "Use -strip apngc to strip APNG chunks";
            }
            if (err_message!=NULL){
                printf("%s", err_message);
                return;
            }
        }
        printf("Cant %s %s", strip ? "strip" : "protect", chunk);
    }
}

/*
 * Creates a transformer object.
 */
opng_transformer_t * opng_create_transformer()
{
    opng_transformer_t * result = (opng_transformer_t *)calloc(1, sizeof(struct opng_transformer));
    if (result == NULL)
        return NULL;

    opng_sigs_init(&result->strip_sigs);
    opng_sigs_init(&result->protect_sigs);
    return result;
}

/*
 * Returns 1 if the given chunk ought to be stripped, or 0 otherwise.
 */
int opng_transform_query_strip_chunk(const opng_transformer_t *transformer, png_byte *chunk_sig)
{
    opng_id_t strip_ids = transformer->strip_ids;
    opng_id_t protect_ids = transformer->protect_ids;

    if (opng_is_image_chunk(chunk_sig))
    {
        /* Image chunks (i.e. critical chunks and tRNS) are never stripped. */
        return 0;
    }
    if (opng_is_apng_chunk(chunk_sig))
    {
        /* Although APNG chunks are encoded as ancillary chunks,
         * they are not metadata, and the regular strip/protect policies
         * do not apply to them.
         */
        return ((strip_ids & OPNG_ID_ANIMATION) != 0);
    }
    if ((strip_ids & (OPNG_ID_ALL | OPNG_ID_CHUNK_META)) == 0)
    {
        /* Nothing is stripped. */
        return 0;
    }
    if ((protect_ids & OPNG_ID_ALL) != 0)
    {
        /* Everything is protected. */
        return 0;
    }
    if ((strip_ids & OPNG_ID_ALL) == 0 && !opng_sigs_find(&transformer->strip_sigs, chunk_sig))
    {
        /* The chunk signature is not in the strip set. */
        return 0;
    }
    if (opng_sigs_find(&transformer->protect_sigs, chunk_sig))
    {
        /* The chunk signature is in the protect set. */
        return 0;
    }

    /* The chunk is stripped and not protected. */
    return 1;
}

/*
 * Seals a transformer object.
 */
const opng_transformer_t * opng_seal_transformer(opng_transformer_t *transformer)
{
    opng_sigs_sort_uniq(&transformer->strip_sigs);
    opng_sigs_sort_uniq(&transformer->protect_sigs);
    return transformer;
}

/*
 * Destroys a transformer object.
 */
void opng_destroy_transformer(opng_transformer_t *transformer)
{
    if (transformer == NULL)
        return;

    opng_sigs_clear(&transformer->strip_sigs);
    opng_sigs_clear(&transformer->protect_sigs);
    free(transformer);
}
