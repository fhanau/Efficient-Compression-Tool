/*
 * opngtrans/trans.h
 * Image transformations.
 *
 * Copyright (C) 2011-2012 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file.
 */

/*Modified by Felix Hanau.*/

#ifndef OPNGTRANS_TRANS_H
#define OPNGTRANS_TRANS_H

#include "opngtrans.h"
#include "../libpng/png.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The chunk signature set structure.
 *
 * This is a set structure optimized for the case when all insertions
 * are expected to be done first and all lookups are expected to be done
 * last.
 *
 * Since we are inside an internal module, we afford not to make
 * this structure opaque, e.g. to allow fast allocations on the stack.
 * The structure members, however, should not be accessed externally.
 *
 * The operations associated with this structure do NOT perform validity
 * checks on chunk signatures.
 *
 * TODO: Try replacing this with a hash, it might be better.
 */
struct opng_sigs
{
    png_byte *buffer;
    size_t count;
    size_t capacity;
    int sorted;
};

/*
 * The maximum size of the chunk signature set.
 *
 * This size is a very large number under typical PNG chunk handling
 * requirements, yet it limits the occupied memory to a reasonably small
 * limit.
 */
enum
{
    OPNG_SIGS_SIZE_MAX = 1024
};

/*
 * Object IDs.
 *
 * The underlying bitset representation is highly efficient, although
 * it restricts the number of recognized IDs. This restriction is
 * acceptable at this time, because only a few IDs are currently in use.
 */
typedef enum
{
    OPNG_ID__UNKNOWN        = 0x0001,  /* unknown object */
    OPNG_ID_CHUNK_IMAGE     = 0x0010,  /* critical chunk or tRNS */
    OPNG_ID_CHUNK_ANIMATION = 0x0020,  /* APNG chunk: acTL, fcTL or fdAT */
    OPNG_ID_CHUNK_META      = 0x0040,  /* any other ancillary chunk */

    OPNG_ID_ALL                   = 0x00000100,  /* "all" */
    OPNG_ID_ANIMATION             = 0x00001000  /* "animation" */
} opng_id_t;

/*
 * Object ID sets: "all chunks", "can strip", etc.
 */
enum
{
    OPNG_IDSET_CHUNK =
        OPNG_ID_CHUNK_IMAGE |
        OPNG_ID_CHUNK_ANIMATION |
        OPNG_ID_CHUNK_META,
    OPNG_IDSET_CAN_TRANSFORM =
        OPNG_ID_ALL |
        OPNG_ID_ANIMATION |
        OPNG_ID_CHUNK_META,
};

/*
 * The transformer structure.
 */
struct opng_transformer
{
    struct opng_sigs strip_sigs;
    struct opng_sigs protect_sigs;
    opng_id_t strip_ids;
    opng_id_t protect_ids;
};

/*
 * Returns 1 if the given chunk ought to be stripped, or 0 otherwise.
 */
int opng_transform_query_strip_chunk(const opng_transformer_t *transformer, png_byte *chunk_sig);


#ifdef __cplusplus
}  /* extern "C" */
#endif


#endif  /* OPNGTRANS_TRANS_H */
