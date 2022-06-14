/* deflate.c -- compress data using the deflation algorithm
 * Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/*
 *  ALGORITHM
 *
 *      The "deflation" process depends on being able to identify portions
 *      of the input text which are identical to earlier input (within a
 *      sliding window trailing behind the input currently being processed).
 *
 *      The most straightforward technique turns out to be the fastest for
 *      most input files: try all possible matches and select the longest.
 *      The key feature of this algorithm is that insertions into the string
 *      dictionary are very simple and thus fast, and deletions are avoided
 *      completely. Insertions are performed at each input character, whereas
 *      string matches are performed only when the previous match ends. So it
 *      is preferable to spend more time in matches to allow very fast string
 *      insertions and avoid deletions. The matching algorithm for small
 *      strings is inspired from that of Rabin & Karp. A brute force approach
 *      is used to find longer strings when a small match has been found.
 *      A similar algorithm is used in comic (by Jan-Mark Wams) and freeze
 *      (by Leonid Broukhis).
 *         A previous version of this file used a more sophisticated algorithm
 *      (by Fiala and Greene) which is guaranteed to run in linear amortized
 *      time, but has a larger average cost, uses more memory and is patented.
 *      However the F&G algorithm may be faster for some highly redundant
 *      files if the parameter max_chain_length (described below) is too large.
 *
 *  ACKNOWLEDGEMENTS
 *
 *      The idea of lazy evaluation of matches is due to Jan-Mark Wams, and
 *      I found it in 'freeze' written by Leonid Broukhis.
 *      Thanks to many people for bug reports and testing.
 *
 *  REFERENCES
 *
 *      Deutsch, L.P.,"DEFLATE Compressed Data Format Specification".
 *      Available in http://tools.ietf.org/html/rfc1951
 *
 *      A description of the Rabin and Karp algorithm is given in the book
 *         "Algorithms" by R. Sedgewick, Addison-Wesley, p252.
 *
 *      Fiala,E.R., and Greene,D.H.
 *         Data Compression with Finite Windows, Comm.ACM, 32,4 (1989) 490-595
 *
 */

#include "deflate.h"
#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE __attribute__((always_inline))
#endif

typedef enum {
    need_more,      /* block not completed, need more input or more output */
    block_done,     /* block flush performed */
    finish_started, /* finish started, need only more output at next deflate */
    finish_done     /* finish done, accept no more input or output */
} block_state;

typedef block_state (*compress_func) (deflate_state *s, int flush, uLong* put);
/* Compression function. Returns the block state after the call. */

static int deflateStateCheck(z_streamp strm);
static void fill_window(deflate_state *s);
static block_state deflate_fast(deflate_state *s, int flush, uLong* put);
static block_state deflate_slow(deflate_state *s, int flush, uLong* put);
static void lm_init(deflate_state *s);
static void flush_pending(z_streamp strm);
static unsigned longest_match(deflate_state *s, IPos cur_match);

#define NIL 0

/* Values for max_lazy_match, good_match and max_chain_length, depending on
 * the desired pack level (0..9). The values given below have been tuned to
 * exclude worst case performance for pathological files. Better values may be
 * found for specific files.
 */
typedef struct config_s {
   uint16_t good_length; /* reduce lazy search above this match length */
   uint16_t max_lazy;    /* do not perform lazy search above this match length */
   uint16_t nice_length; /* quit search above this match length */
   uint16_t max_chain;
   compress_func func;
} config;

static const config configuration_table[10] = {
/*      good lazy nice chain */
/* fake 0 */ {16, 258, 258, 2048, deflate_slow}, /* stored mode was removed */
/* 1 */ {16, 258, 258, 4096, deflate_slow}, /* max speed, no lazy matches */
/* 2 */ {4,    5, 16,    8, deflate_fast},
/* 3 */ {4,    6, 32,   32, deflate_fast},

/* 4 */ {4,    4, 16,   16, deflate_slow},  /* lazy matches */
/* 5 */ {8,   16, 32,   32, deflate_slow},
/* 6 */ {8,   16, 128, 128, deflate_slow},
/* 7 */ {8,   32, 128, 256, deflate_slow},
/* 8 */ {32, 258, 258, 2048, deflate_slow},
/* 9 */ {32, 258, 258, 4096, deflate_slow}}; /* max compression */

/* Note: the deflate() code requires max_lazy >= MIN_MATCH and max_chain >= 4
 * For deflate_fast() (levels <= 3) good is ignored and lazy has a different
 * meaning.
 */

/* rank Z_BLOCK between Z_NO_FLUSH and Z_PARTIAL_FLUSH */
#define RANK(f) (((f) << 1) - ((f) > 4 ? 9 : 0))

#if defined __aarch64__ || defined __SSE4_2__
//Any 64-bit ARM CPU has crc32c support as both are supported since ARMv8-A.
#ifdef __aarch64__

#include <arm_neon.h>
#include <arm_acle.h>
__attribute__ ((always_inline)) inline static unsigned
platform_compute_hash(deflate_state *s, const unsigned char *str) {
    unsigned v = 0xffffff & *(const unsigned*)str;
    unsigned hash = __crc32cw(0, v);
    return hash & s->hash_mask;
}

#else /* __SSE4_2__ */

#include "nmmintrin.h"

__attribute__ ((always_inline)) inline static unsigned
platform_compute_hash(deflate_state *s, const unsigned char *str) {
    unsigned v = 0xffffff & *(const unsigned*)str;
    unsigned hash = _mm_crc32_u32(0, v);
    return hash & s->hash_mask;
}
#endif

#define UPDATE_HASH(s,h,str) (h = platform_compute_hash(s, str - 2))
#define INIT_HASH

#endif

/* ===========================================================================
 * Update a hash value with the given input byte
 * IN  assertion: all calls to to UPDATE_HASH are made with consecutive
 *    input characters, so that a running hash key can be computed from the
 *    previous key instead of complete recalculation each time.
 */
#ifndef UPDATE_HASH
    ALWAYS_INLINE inline static unsigned
    _update_hash(deflate_state *s, unsigned h, const unsigned char *str)  {
        return (((h << s->hash_shift) ^ *(str)) & (s->hash_mask));
    }
    #define UPDATE_HASH(s,h,str) (h = _update_hash(s, h, str), h)
#endif

#ifndef INIT_HASH
    #if MIN_MATCH != 3
        #error Need to Call UPDATE_HASH() MIN_MATCH-3 more times in INIT_HAHS
    #endif
    #define INIT_HASH(s, h, str) (h = *str, UPDATE_HASH(s, h, (str)+1))
#endif

/* ===========================================================================
 * Insert string str in the dictionary and set match_head to the previous head
 * of the hash chain (the most recent string with same hash key). Return
 * the previous length of the hash chain.
 * IN  assertion: all calls to to INSERT_STRING are made with consecutive
 *    input characters and the first MIN_MATCH bytes of str are valid
 *    (except for the last MIN_MATCH-1 bytes of the input file).
 */
#define INSERT_STRING(s, str, match_head) \
   (UPDATE_HASH(s, s->ins_h, &s->window[(str) + (MIN_MATCH-1)]), \
    match_head = s->prev[(str) & s->w_mask] = s->head[s->ins_h], \
    s->head[s->ins_h] = (Pos)(str))

ALWAYS_INLINE inline static void
bulk_insert_str(deflate_state *s, Pos startpos, unsigned count) {
    unsigned idx;
    for (idx = 0; idx < count; idx++) {
        Pos dummy;
        INSERT_STRING(s, startpos + idx, dummy);
    }
}

static int _tr_tally_lit(deflate_state *s, uint8_t cc) {
  *(uint16_t*)&(s->sym_buf[s->sym_next]) = 0;
  s->sym_next += 2;
  s->sym_buf[s->sym_next++] = cc;
  s->dyn_ltree[cc].Freq++;
  return (s->sym_next == s->sym_end);
}

static int _tr_tally_dist(deflate_state *s, uint16_t dist, uint8_t len) {
  *(uint16_t*)&(s->sym_buf[s->sym_next]) = dist;
  s->sym_next += 2;
  s->sym_buf[s->sym_next++] = len;
  dist--;
  s->dyn_ltree[_length_code[len]+LITERALS+1].Freq++;
  s->dyn_dtree[d_code(dist)].Freq++;
  return (s->sym_next == s->sym_end);
}

/* ===========================================================================
 * Initialize the hash table prev[] will be initialized on the fly.
 */
#define CLEAR_HASH(s) \
    zmemzero((uint8_t *)s->head, (unsigned)(s->hash_size)*sizeof(*s->head));

/* ========================================================================= */
int deflateInit2_(strm, level, method, windowBits, memLevel, strategy,
                  version, stream_size)
    z_streamp strm;
    int  level;
    int  method;
    int  windowBits;
    int  memLevel;
    int  strategy;
    const char *version;
    int stream_size;
{
    deflate_state *s;
    int wrap = 1;
    static const char my_version[] = ZLIB_VERSION;

    if (version == Z_NULL || version[0] != my_version[0] ||
        stream_size != sizeof(z_stream)) {
        return Z_VERSION_ERROR;
    }
    if (strm == Z_NULL) return Z_STREAM_ERROR;

    strm->msg = Z_NULL;
    if (strm->zalloc == (alloc_func)0) {
        strm->zalloc = zcalloc;
        strm->opaque = (voidpf)0;
    }
    if (strm->zfree == (free_func)0)
        strm->zfree = zcfree;

    if (level == Z_DEFAULT_COMPRESSION) level = 6;

    if (windowBits < 0) { /* suppress zlib wrapper */
        wrap = 0;
        windowBits = -windowBits;
    }
    if (memLevel < 1 || memLevel > MAX_MEM_LEVEL || method != Z_DEFLATED ||
        windowBits < 8 || windowBits > 15 || level < 0 || level > 9 ||
        strategy < 0 || strategy > Z_FIXED) {
        return Z_STREAM_ERROR;
    }
    if (windowBits == 8) windowBits = 9;  /* until 256-byte window bug fixed */
    s = (deflate_state *) ZALLOC(strm, 1, sizeof(deflate_state));
    if (s == Z_NULL) return Z_MEM_ERROR;
    strm->state = (struct internal_state *)s;
    s->strm = strm;
    s->status = INIT_STATE;

    s->wrap = wrap;
    s->gzhead = Z_NULL;
    s->w_bits = windowBits;
    s->w_size = 1 << s->w_bits;
    s->w_mask = s->w_size - 1;

    s->hash_bits = memLevel + 7;
    s->hash_size = 1 << s->hash_bits;
    s->hash_mask = s->hash_size - 1;
    s->hash_shift =  ((s->hash_bits+MIN_MATCH-1)/MIN_MATCH);

    s->window = (uint8_t *) ZALLOC(strm, s->w_size, 2*sizeof(uint8_t));
    s->prev   = (Pos *)  ZALLOC(strm, s->w_size, sizeof(Pos));
    s->head   = (Pos *)  ZALLOC(strm, s->hash_size, sizeof(Pos));

    s->high_water = 0;      /* nothing written to s->window yet */

    s->lit_bufsize = 1 << (memLevel + 6); /* 16K elements by default */

    /* We overlay pending_buf and sym_buf. This works since the average size
     * for length/distance pairs over any compressed block is assured to be 31
     * bits or less.
     *
     * Analysis: The longest fixed codes are a length code of 8 bits plus 5
     * extra bits, for lengths 131 to 257. The longest fixed distance codes are
     * 5 bits plus 13 extra bits, for distances 16385 to 32768. The longest
     * possible fixed-codes length/distance pair is then 31 bits total.
     *
     * sym_buf starts one-fourth of the way into pending_buf. So there are
     * three bytes in sym_buf for every four bytes in pending_buf. Each symbol
     * in sym_buf is three bytes -- two for the distance and one for the
     * literal/length. As each symbol is consumed, the pointer to the next
     * sym_buf value to read moves forward three bytes. From that symbol, up to
     * 31 bits are written to pending_buf. The closest the written pending_buf
     * bits gets to the next sym_buf symbol to read is just before the last
     * code is written. At that time, 31*(n-2) bits have been written, just
     * after 24*(n-2) bits have been consumed from sym_buf. sym_buf starts at
     * 8*n bits into pending_buf. (Note that the symbol buffer fills when n-1
     * symbols are written.) The closest the writing gets to what is unread is
     * then n+14 bits. Here n is lit_bufsize, which is 16384 by default, and
     * can range from 128 to 32768.
     *
     * Therefore, at a minimum, there are 142 bits of space between what is
     * written and what is read in the overlain buffers, so the symbols cannot
     * be overwritten by the compressed data. That space is actually 139 bits,
     * due to the three-bit fixed-code block header.
     *
     * That covers the case where either Z_FIXED is specified, forcing fixed
     * codes, or when the use of fixed codes is chosen, because that choice
     * results in a smaller compressed block than dynamic codes. That latter
     * condition then assures that the above analysis also covers all dynamic
     * blocks. A dynamic-code block will only be chosen to be emitted if it has
     * fewer bits than a fixed-code block would for the same set of symbols.
     * Therefore its average symbol length is assured to be less than 31. So
     * the compressed data for a dynamic block also cannot overwrite the
     * symbols from which it is being constructed.
     */

    s->pending_buf = (uint8_t *) ZALLOC(strm, s->lit_bufsize, 4);
    s->pending_buf_size = (uint64_t)s->lit_bufsize * 4;

    if (s->window == Z_NULL || s->prev == Z_NULL || s->head == Z_NULL ||
        s->pending_buf == Z_NULL) {
        s->status = FINISH_STATE;
        strm->msg = ERR_MSG(Z_MEM_ERROR);
        deflateEnd (strm);
        return Z_MEM_ERROR;
    }
    s->sym_buf = s->pending_buf + s->lit_bufsize;
    s->sym_end = (s->lit_bufsize - 1) * 3;
    /* We avoid equality with lit_bufsize*3 because of wraparound at 64K
     * on 16 bit machines and because stored blocks are restricted to
     * 64K-1 bytes.
     */

    s->level = level;
    s->strategy = strategy;
    s->method = (uint8_t)method;

    return deflateReset(strm);
}

/* =========================================================================
 * Check for a valid deflate stream state. Return 0 if ok, 1 if not.
 */
static int deflateStateCheck (strm)
    z_streamp strm;
{
    deflate_state *s;
    if (strm == Z_NULL ||
        strm->zalloc == (alloc_func)0 || strm->zfree == (free_func)0)
        return 1;
    s = strm->state;
    if (s == Z_NULL || s->strm != strm || (s->status != INIT_STATE &&
                                           s->status != EXTRA_STATE &&
                                           s->status != NAME_STATE &&
                                           s->status != COMMENT_STATE &&
                                           s->status != HCRC_STATE &&
                                           s->status != BUSY_STATE &&
                                           s->status != FINISH_STATE))
        return 1;
    return 0;
}

static int deflateResetKeep (strm)
    z_streamp strm;
{
    deflate_state *s;

    if (deflateStateCheck(strm)) {
        return Z_STREAM_ERROR;
    }

    strm->total_in = strm->total_out = 0;
    strm->msg = Z_NULL; /* use zfree if we ever allocate msg dynamically */
    strm->data_type = Z_UNKNOWN;

    s = (deflate_state *)strm->state;
    s->pending = 0;
    s->pending_out = s->pending_buf;

    if (s->wrap < 0) {
        s->wrap = -s->wrap; /* was made negative by deflate(..., Z_FINISH); */
    }
    s->status = s->wrap ? INIT_STATE : BUSY_STATE;
    strm->adler = adler32(0L, Z_NULL, 0);
    s->last_flush = Z_NO_FLUSH;

    _tr_init(s);

    return Z_OK;
}

int deflateReset (strm)
    z_streamp strm;
{
    int ret;

    ret = deflateResetKeep(strm);
    if (ret == Z_OK)
        lm_init(strm->state);
    return ret;
}

int deflateTune(strm, good_length, max_lazy, nice_length, max_chain)
    z_streamp strm;
    int good_length;
    int max_lazy;
    int nice_length;
    int max_chain;
{
    deflate_state *s;

    if (deflateStateCheck(strm)) return Z_STREAM_ERROR;
    s = strm->state;
    s->good_match = good_length;
    s->max_lazy_match = max_lazy;
    s->nice_match = nice_length;
    s->max_chain_length = max_chain;
    return Z_OK;
}

/* =========================================================================
 * For the default windowBits of 15 and memLevel of 8, this function returns
 * a close to exact, as well as small, upper bound on the compressed size.
 * They are coded as constants here for a reason--if the #define's are
 * changed, then this function needs to be changed as well.  The return
 * value for 15 and 8 only works for those exact settings.
 *
 * For any setting other than those defaults for windowBits and memLevel,
 * the value returned is a conservative worst case for the maximum expansion
 * resulting from using fixed blocks instead of stored blocks, which deflate
 * can emit on compressed data for some combinations of the parameters.
 *
 * This function could be more sophisticated to provide closer upper bounds for
 * every combination of windowBits and memLevel.  But even the conservative
 * upper bound of about 14% expansion does not seem onerous for output buffer
 * allocation.
 */
uint64_t deflateBound(z_streamp strm, uint64_t sourceLen)
{
    deflate_state *s;
    uint64_t complen, wraplen;
    uint8_t *str;

    /* conservative upper bound for compressed data */
    complen = sourceLen +
              ((sourceLen + 7) >> 3) + ((sourceLen + 63) >> 6) + 5;

    /* if can't get parameters, return conservative bound plus zlib wrapper */
    if (deflateStateCheck(strm))
        return complen + 6;

    /* compute wrapper length */
    s = strm->state;
    switch (s->wrap) {
    case 0:                                 /* raw deflate */
        wraplen = 0;
        break;
    case 1:                                 /* zlib wrapper */
        wraplen = 6 + (s->strstart ? 4 : 0);
        break;
    case 2:                                 /* gzip wrapper */
        wraplen = 18;
        if (s->gzhead != Z_NULL) {          /* user-supplied gzip header */
            if (s->gzhead->extra != Z_NULL)
                wraplen += 2 + s->gzhead->extra_len;
            str = s->gzhead->name;
            if (str != Z_NULL)
                do {
                    wraplen++;
                } while (*str++);
            str = s->gzhead->comment;
            if (str != Z_NULL)
                do {
                    wraplen++;
                } while (*str++);
            if (s->gzhead->hcrc)
                wraplen += 2;
        }
        break;
    default:                                /* for compiler happiness */
        wraplen = 6;
    }

    /* if not default parameters, return conservative bound */
    if (s->w_bits != 15 || s->hash_bits != 8 + 7)
        return complen + wraplen;

    /* default settings: return tight bound for that case */
    return sourceLen + (sourceLen >> 12) + (sourceLen >> 14) +
           (sourceLen >> 25) + 13 - 6 + wraplen;
}

/* =========================================================================
 * Put a short in the pending buffer. The 16-bit value is put in MSB order.
 * IN assertion: the stream state is correct and there is enough room in
 * pending_buf.
 */
static void putShortMSB (s, b)
    deflate_state *s;
    uint32_t b;
{
    put_byte(s, (uint8_t)(b >> 8));
    put_byte(s, (uint8_t)(b & 0xff));
}

/* =========================================================================
 * Flush as much pending output as possible. All deflate() output goes
 * through this function so some applications may wish to modify it
 * to avoid allocating a large strm->next_out buffer and copying into it.
 * (See also read_buf()).
 */
static void flush_pending(strm)
    z_streamp strm;
{
    uint32_t len;
    deflate_state *s = strm->state;

    _tr_flush_bits(s);
    len = s->pending;
    if (len > strm->avail_out) len = strm->avail_out;
    if (len == 0) return;

    zmemcpy(strm->next_out, s->pending_out, len);
    strm->next_out  += len;
    s->pending_out  += len;
    strm->total_out += len;
    strm->avail_out  -= len;
    s->pending -= len;
    if (s->pending == 0) {
        s->pending_out = s->pending_buf;
    }
}

static int deflate_ (strm, flush, put)
    z_streamp strm;
    int flush;
    int put;
{
    int old_flush; /* value of flush param for previous deflate call */
    deflate_state *s;

    if (deflateStateCheck(strm) || flush > Z_BLOCK || flush < 0) {
        return Z_STREAM_ERROR;
    }
    s = strm->state;

    if (strm->next_out == Z_NULL ||
        (strm->next_in == Z_NULL && strm->avail_in != 0) ||
        (s->status == FINISH_STATE && flush != Z_FINISH)) {
        ERR_RETURN(strm, Z_STREAM_ERROR);
    }
    if (strm->avail_out == 0) ERR_RETURN(strm, Z_BUF_ERROR);

    s->strm = strm; /* just in case */
    old_flush = s->last_flush;
    s->last_flush = flush;

    /* Write the header */
    if (s->status == INIT_STATE) {
        {
            uint32_t header = (Z_DEFLATED + ((s->w_bits-8)<<4)) << 8;
            uint32_t level_flags;

            if (s->strategy >= Z_HUFFMAN_ONLY || s->level < 2)
                level_flags = 0;
            else if (s->level < 6)
                level_flags = 1;
            else if (s->level == 6)
                level_flags = 2;
            else
                level_flags = 3;
            header |= (level_flags << 6);
            if (s->strstart != 0) header |= PRESET_DICT;
            header += 31 - (header % 31);

            s->status = BUSY_STATE;
            putShortMSB(s, header);

            /* Save the adler32 of the preset dictionary: */
            if (s->strstart != 0) {
                putShortMSB(s, (uint32_t)(strm->adler >> 16));
                putShortMSB(s, (uint32_t)(strm->adler & 0xffff));
            }
            strm->adler = adler32(0L, Z_NULL, 0);
        }
    }

    /* Flush as much pending output as possible */
    if (s->pending != 0) {
        flush_pending(strm);
        if (strm->avail_out == 0) {
            /* Since avail_out is 0, deflate will be called again with
             * more output space, but possibly with both pending and
             * avail_in equal to zero. There won't be anything to do,
             * but this is not an error situation so make sure we
             * return OK instead of BUF_ERROR at next call of deflate:
             */
            s->last_flush = -1;
            return Z_OK;
        }

    /* Make sure there is something to do and avoid duplicate consecutive
     * flushes. For repeated and useless calls with Z_FINISH, we keep
     * returning Z_STREAM_END instead of Z_BUF_ERROR.
     */
    } else if (strm->avail_in == 0 && RANK(flush) <= RANK(old_flush) &&
               flush != Z_FINISH) {
        ERR_RETURN(strm, Z_BUF_ERROR);
    }

    /* User must not provide more input after the first FINISH: */
    if (s->status == FINISH_STATE && strm->avail_in != 0) {
        ERR_RETURN(strm, Z_BUF_ERROR);
    }

    /* Start a new block or continue the current one.
     */
    if (strm->avail_in != 0 || s->lookahead != 0 ||
        (flush != Z_NO_FLUSH && s->status != FINISH_STATE)) {
      block_state bstate = ((*(configuration_table[s->level].func))(s, flush, !put ? &(strm->total_out) : 0));

        if (bstate == finish_started || bstate == finish_done) {
            s->status = FINISH_STATE;
        }
        if (bstate == need_more || bstate == finish_started) {
            if (strm->avail_out == 0) {
                s->last_flush = -1; /* avoid BUF_ERROR next call, see above */
            }
            return Z_OK;
            /* If flush != Z_NO_FLUSH && avail_out == 0, the next call
             * of deflate should use the same flush parameter to make sure
             * that the flush is complete. So we don't have to output an
             * empty block here, this will be done at next call. This also
             * ensures that for a very small output buffer, we emit at most
             * one empty block.
             */
        }
        if (bstate == block_done) {
            if (flush == Z_PARTIAL_FLUSH) {
                _tr_align(s);
            } else if (flush != Z_BLOCK) { /* FULL_FLUSH or SYNC_FLUSH */
                _tr_stored_block(s, (uint8_t*)0, 0L, 0);
                /* For a full flush, this empty block will be recognized
                 * as a special marker by inflate_sync().
                 */
                if (flush == Z_FULL_FLUSH) {
                    CLEAR_HASH(s);             /* forget history */
                    if (s->lookahead == 0) {
                        s->strstart = 0;
                        s->block_start = 0L;
                        s->insert = 0;
                    }
                }
            }
            flush_pending(strm);
            if (strm->avail_out == 0) {
              s->last_flush = -1; /* avoid BUF_ERROR at next call, see above */
              return Z_OK;
            }
        }
    }
    Assert(strm->avail_out > 0, "bug2");

    if (flush != Z_FINISH) return Z_OK;
    if (s->wrap <= 0) return Z_STREAM_END;

    /* Write the trailer */
    {
        putShortMSB(s, (uint32_t)(strm->adler >> 16));
        putShortMSB(s, (uint32_t)(strm->adler & 0xffff));
    }
    flush_pending(strm);
    /* If avail_out is zero, the application will call deflate again
     * to flush the rest.
     */
    if (s->wrap > 0) s->wrap = -s->wrap; /* write the trailer only once! */
    return s->pending != 0 ? Z_OK : Z_STREAM_END;
}

int deflate(z_streamp strm, int flush){
  return deflate_(strm, flush, 1);
}

int deflate_nooutput (z_streamp strm, int flush){
  return deflate_(strm, flush, 0);
}

int deflateEnd (z_streamp strm)
{
    int status;

    if (deflateStateCheck(strm)) return Z_STREAM_ERROR;

    status = strm->state->status;

    /* Deallocate in reverse order of allocations: */
    TRY_FREE(strm, strm->state->pending_buf);
    TRY_FREE(strm, strm->state->head);
    TRY_FREE(strm, strm->state->prev);
    TRY_FREE(strm, strm->state->window);

    ZFREE(strm, strm->state);
    strm->state = Z_NULL;

    return status == BUSY_STATE ? Z_DATA_ERROR : Z_OK;
}

/* =========================================================================
 * Copy the source state to the destination state.
 * To simplify the source, this is not supported for 16-bit MSDOS (which
 * doesn't have enough memory anyway to duplicate compression states).
 */
int ZEXPORT deflateCopy (z_stream* dest, z_stream* source, unsigned char alloc)
{
  deflate_state *ds;
  deflate_state *ss;


  if (deflateStateCheck(source) || dest == Z_NULL) {
    return Z_STREAM_ERROR;
  }

  ss = source->state;


  if(alloc){
    zmemcpy((voidpf)dest, (voidpf)source, sizeof(z_stream));

    ds = (deflate_state *) ZALLOC(dest, 1, sizeof(deflate_state));
    if (ds == Z_NULL) return Z_MEM_ERROR;
    dest->state = (struct internal_state *) ds;
    zmemcpy((voidpf)ds, (voidpf)ss, sizeof(deflate_state));
  }
  else {
    ds = dest->state;
    zmemcpy((voidpf)dest, (voidpf)source, sizeof(z_stream));
    dest->state = ds;

    void* window = ds->window, *prev = ds->prev, *head = ds->head, *pending_buf = ds->pending_buf;
    zmemcpy((voidpf)ds, (voidpf)ss, sizeof(deflate_state));

    ds->window = window;
    ds->prev   = prev;
    ds->head   = head;
    ds->pending_buf = pending_buf;
  }
  ds->strm = dest;

  if(alloc){
    ds->window = (uint8_t *) ZALLOC(dest, ds->w_size, 2*sizeof(uint8_t));
    ds->prev   = (Pos *)  ZALLOC(dest, ds->w_size, sizeof(Pos));
    ds->head   = (Pos *)  ZALLOC(dest, ds->hash_size, sizeof(Pos));
    ds->pending_buf = (uint8_t *) ZALLOC(dest, ds->lit_bufsize, sizeof(uint16_t)+2);
  }

  if (ds->window == Z_NULL || ds->prev == Z_NULL || ds->head == Z_NULL ||
      ds->pending_buf == Z_NULL) {
    deflateEnd (dest);
    return Z_MEM_ERROR;
  }
  zmemcpy(ds->window, ss->window, ds->w_size * 2 * sizeof(uint8_t));
  zmemcpy((voidpf)ds->prev, (voidpf)ss->prev, ds->w_size * sizeof(Pos));
  zmemcpy((voidpf)ds->head, (voidpf)ss->head, ds->hash_size * sizeof(Pos));
  //Do not copy due to performance reasons. If we ever need to copy a stream that actually produces used output, it'll be enabled again.
  //zmemcpy(ds->pending_buf, ss->pending_buf, (uint32_t)ds->pending_buf_size);

  ds->pending_out = ds->pending_buf + (ss->pending_out - ss->pending_buf);
  ds->sym_buf = ds->pending_buf + ds->lit_bufsize;

  ds->l_desc.dyn_tree = ds->dyn_ltree;
  ds->d_desc.dyn_tree = ds->dyn_dtree;
  ds->bl_desc.dyn_tree = ds->bl_tree;

  return Z_OK;
}

/* ===========================================================================
 * Read a new buffer from the current input stream, update the adler32
 * and total number of bytes read.  All deflate() input goes through
 * this function so some applications may wish to modify it to avoid
 * allocating a large strm->next_in buffer and copying from it.
 * (See also flush_pending()).
 */
static int read_buf(strm, buf, size)
    z_streamp strm;
    uint8_t *buf;
    uint32_t size;
{
    uint32_t len = strm->avail_in;

    if (len > size) len = size;
    if (len == 0) return 0;

    strm->avail_in  -= len;

    zmemcpy(buf, strm->next_in, len);
    if (strm->state->wrap == 1) {
        strm->adler = adler32(strm->adler, buf, len);
    }
    strm->next_in  += len;
    strm->total_in += len;

    return (int)len;
}

/* ===========================================================================
 * Initialize the "longest match" routines for a new zlib stream
 */
static void lm_init (deflate_state* s)
{
    s->window_size = (uint64_t)2L*s->w_size;

    CLEAR_HASH(s);

    /* Set the default configuration parameters:
     */
    s->max_lazy_match   = configuration_table[s->level].max_lazy;
    s->good_match       = configuration_table[s->level].good_length;
    s->nice_match       = configuration_table[s->level].nice_length;
    s->max_chain_length = configuration_table[s->level].max_chain;

    s->strstart = 0;
    s->block_start = 0L;
    s->lookahead = 0;
    s->insert = 0;
    s->match_length = s->prev_length = MIN_MATCH-1;
    s->match_available = 0;
    s->ins_h = 0;
}

/* longest_match() with minor change to improve performance (in terms of
 * execution time).
 *
 * The pristine longest_match() function is sketched bellow (strip the
 * then-clause of the "#ifdef UNALIGNED_OK"-directive)
 *
 * ------------------------------------------------------------
 * uInt longest_match(...) {
 *    ...
 *    do {
 *        match = s->window + cur_match;                //s0
 *        if (*(ushf*)(match+best_len-1) != scan_end || //s1
 *            *(ushf*)match != scan_start) continue;    //s2
 *        ...
 *
 *        do {
 *        } while (*(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 scan < strend); //s3
 *
 *        ...
 *    } while(cond); //s4
 *
 * -------------------------------------------------------------
 *
 * The change include:
 *
 *  1) The hottest statements of the function is: s0, s1 and s4. Pull them
 *     together to form a new loop. The benefit is two-fold:
 *
 *    o. Ease the compiler to yield good code layout: the conditional-branch
 *       corresponding to s1 and its biased target s4 become very close (likely,
 *       fit in the same cache-line), hence improving instruction-fetching
 *       efficiency.
 *
 *    o. Ease the compiler to promote "s->window" into register. "s->window"
 *       is loop-invariant; it is supposed to be promoted into register and keep
 *       the value throughout the entire loop. However, there are many such
 *       loop-invariant, and x86-family has small register file; "s->window" is
 *       likely to be chosen as register-allocation victim such that its value
 *       is reloaded from memory in every single iteration. By forming a new loop,
 *       "s->window" is loop-invariant of that newly created tight loop. It is
 *       lot easier for compiler to promote this quantity to register and keep
 *       its value throughout the entire small loop.
 *
 * 2) Transfrom s3 such that it examines sizeof(long)-byte-match at a time.
 *    This is done by:
 *        ------------------------------------------------
 *        v1 = load from "scan" by sizeof(long) bytes
 *        v2 = load from "match" by sizeof(lnog) bytes
 *        v3 = v1 xor v2
 *        match-bit = little-endian-machine(yes-for-x86) ?
 *                     count-trailing-zero(v3) :
 *                     count-leading-zero(v3);
 *
 *        match-byte = match-bit/8
 *
 *        "scan" and "match" advance if necessary
 *       -------------------------------------------------
 */

__attribute__ ((always_inline)) inline static uint32_t longest_match(s, cur_match)
deflate_state *s;
IPos cur_match;                             /* current match */
{
    uint32_t chain_length = s->max_chain_length;/* max hash chain length */
    register uint8_t *scan = s->window + s->strstart; /* current string */
    register uint8_t *match;                       /* matched string */
    register int len;                           /* length of current match */
    int best_len = s->prev_length;              /* best match length so far */
    int nice_match = s->nice_match;             /* stop if match long enough */
    IPos limit = s->strstart > (IPos)MAX_DIST(s) ?
    s->strstart - (IPos)MAX_DIST(s) : NIL;
    /* Stop when cur_match becomes <= limit. To simplify the code,
     * we prevent matches with the string of window index 0.
     */
    Pos *prev = s->prev;
    uint32_t wmask = s->w_mask;

    register uint8_t *strend = s->window + s->strstart + MAX_MATCH;
    register unsigned short scan_start = *(unsigned short*)scan;
    register unsigned short scan_end   = *(unsigned short*)(scan+best_len-1);

    /* The code is optimized for HASH_BITS >= 8 and MAX_MATCH-2 multiple of 16.
     * It is easy to get rid of this optimization if necessary.
     */
    Assert(s->hash_bits >= 8 && MAX_MATCH == 258, "Code too clever");

    /* Do not waste too much time if we already have a good match: */
    if (s->prev_length >= s->good_match) {
        chain_length >>= 2;
    }
    /* Do not look for matches beyond the end of the input. This is necessary
     * to make deflate deterministic.
     */
    if ((uint32_t)nice_match > s->lookahead) nice_match = s->lookahead;

        Assert((uint64_t)s->strstart <= s->window_size-MIN_LOOKAHEAD, "need lookahead");

        do {
            Assert(cur_match < s->strstart, "no future");

            /* Skip to next match if the match length cannot increase
             * or if the match length is less than 2.  Note that the checks below
             * for insufficient lookahead only occur occasionally for performance
             * reasons.  Therefore uninitialized memory will be accessed, and
             * conditional jumps will be made that depend on those values.
             * However the length of the match is limited to the lookahead, so
             * the output of deflate is not affected by the uninitialized values.
             */
            uint8_t * win = s->window;
            int cont = 1;
            do {
                match = win + cur_match;
                if (likely(*(unsigned short*)(match+best_len-1) != scan_end)) {
                    if ((cur_match = prev[cur_match & wmask]) > limit
                        && --chain_length != 0) {
                        continue;
                    }
                    cont = 0;
                }
                break;
            } while (1);

            if (!cont)
                break;

            if (*(unsigned short*)match != scan_start)
                continue;

            /* It is not necessary to compare scan[2] and match[2] since they are
             * always equal when the other bytes match, given that the hash keys
             * are equal and that HASH_BITS >= 8. Compare 2 bytes at a time at
             * strstart+3, +5, ... up to strstart+257. We check for insufficient
             * lookahead only every 4th comparison; the 128th check will be made
             * at strstart+257. If MAX_MATCH-2 is not a multiple of 8, it is
             * necessary to put more guard bytes at the end of the window, or
             * to check more often for insufficient lookahead.
             */
            scan += 2, match+=2;
            do {
                uint64_t sv = *(uint64_t*)(void*)scan;
                uint64_t mv = *(uint64_t*)(void*)match;
                uint64_t xor = sv ^ mv;
                if (xor) {
                    scan += __builtin_ctzll(xor) / 8;
                    break;
                }
                scan += 8;
                match += 8;
            } while (scan < strend);

            if (scan > strend)
                scan = strend;

            Assert(scan <= s->window+(uint32_t)(s->window_size-1), "wild scan");

            len = MAX_MATCH - (int)(strend - scan);
            scan = strend - MAX_MATCH;

            if (len > best_len) {
                s->match_start = cur_match;
                best_len = len;
                if (len >= nice_match) break;
                scan_end = *(unsigned short*)(scan+best_len-1);
            }
        } while ((cur_match = prev[cur_match & wmask]) > limit
                 && --chain_length != 0);
    
    if ((uint32_t)best_len <= s->lookahead) return (uint32_t)best_len;
    return s->lookahead;
}

/* ===========================================================================
 * Fill the window when the lookahead becomes insufficient.
 * Updates strstart and lookahead.
 *
 * IN assertion: lookahead < MIN_LOOKAHEAD
 * OUT assertions: strstart <= window_size-MIN_LOOKAHEAD
 *    At least one byte has been read, or avail_in == 0; reads are
 *    performed for at least two bytes (required for the zip translate_eol
 *    option -- not supported here).
 */
static void fill_window(s)
deflate_state *s;
{
    register uint32_t n, m;
    register Pos *p;
    uint32_t more;    /* Amount of free space at the end of the window. */
    uint32_t wsize = s->w_size;

    Assert(s->lookahead < MIN_LOOKAHEAD, "already enough lookahead");

    do {
        more = (unsigned)(s->window_size -(uint64_t)s->lookahead -(ulg)s->strstart);

        /* If the window is almost full and there is insufficient lookahead,
         * move the upper half to the lower one to make room in the upper half.
         */

        if (s->strstart >= wsize+MAX_DIST(s)) {

            int i;
            zmemcpy(s->window, s->window+wsize, (unsigned)wsize);
            s->match_start -= wsize;
            s->strstart    -= wsize;
            s->block_start -= (int64_t) wsize;
            n = s->hash_size;

#ifdef __aarch64__

            uint16x8_t  W;
            uint16_t   *q ;
            W = vmovq_n_u16(wsize);
            q = (uint16_t*)s->head;

            for(i=0; i<n/8; i++) {
                vst1q_u16(q, vqsubq_u16(vld1q_u16(q), W));
                q+=8;
            }

            n = wsize;
            q = (uint16_t*)s->prev;

            for(i=0; i<n/8; i++) {
                vst1q_u16(q, vqsubq_u16(vld1q_u16(q), W));
                q+=8;
            }

#elif defined __SSE4_2__

            __m128i  W;
            __m128i *q;
            W = _mm_set1_epi16(wsize);
            q = (__m128i*)s->head;

            for(i=0; i<n/8; i++) {
                _mm_storeu_si128(q, _mm_subs_epu16(_mm_loadu_si128(q), W));
                q++;
            }

            n = wsize;
            q = (__m128i*)s->prev;

            for(i=0; i<n/8; i++) {
                _mm_storeu_si128(q, _mm_subs_epu16(_mm_loadu_si128(q), W));
                q++;
            }
#else
          p = &s->head[n];

          /* As of I make this change, gcc (4.8.*) isn't able to vectorize
           * this hot loop using saturated-subtraction on x86-64 architecture.
           * To avoid this defect, we can change the loop such that
           *    o. the pointer advance forward, and
           *    o. demote the variable 'm' to be static to the loop, and
           *       choose type "Pos" (instead of 'unsigned int') for the
           *       variable to avoid unncessary zero-extension.
           */
          {
            Pos *q = p - n;
            for (i = 0; i < n; i++) {
              Pos m = *q;
              Pos t = wsize;
              *q++ = (Pos)(m >= t ? m-t: 0);
            }
          }

          /* The following three assignments are unnecessary as the variable
           * p, n and m are dead at this point. The rationale for these
           * statements is to ease the reader to verify the two loops are
           * equivalent.
           */
          p = p - n;
          m = *p;
          n = wsize;
          p = &s->prev[n];
          {
            int i;
            Pos *q = p - n;
            for (i = 0; i < n; i++) {
              Pos m = *q;
              Pos t = wsize;
              *q++ = (Pos)(m >= t ? m-t: 0);
            }
            p = p - n;
            m = *p;
            n = 0;
          }

#endif
            more += wsize;
        }
        if (s->strm->avail_in == 0) break;

        /* If there was no sliding:
         *    strstart <= WSIZE+MAX_DIST-1 && lookahead <= MIN_LOOKAHEAD - 1 &&
         *    more == window_size - lookahead - strstart
         * => more >= window_size - (MIN_LOOKAHEAD-1 + WSIZE + MAX_DIST-1)
         * => more >= window_size - 2*WSIZE + 2
         * In the BIG_MEM or MMAP case (not yet supported),
         *   window_size == input_size + MIN_LOOKAHEAD  &&
         *   strstart + s->lookahead <= input_size => more >= MIN_LOOKAHEAD.
         * Otherwise, window_size == 2*WSIZE so more >= 2.
         * If there was sliding, more >= WSIZE. So in all cases, more >= 2.
         */
        Assert(more >= 2, "more < 2");

        n = read_buf(s->strm, s->window + s->strstart + s->lookahead, more);
        s->lookahead += n;

        /* Initialize the hash value now that we have some input: */
        if (s->lookahead + s->insert >= MIN_MATCH) {
            uint32_t str = s->strstart - s->insert;
            uint32_t ins_h = s->window[str];
            INIT_HASH(s, ins_h, &s->window[str]);
            while (s->insert) {
                UPDATE_HASH(s, ins_h, &s->window[str + 2]);
                s->prev[str & s->w_mask] = s->head[ins_h];
                s->head[ins_h] = (Pos)str;
                str++;
                s->insert--;
                if (s->lookahead + s->insert < MIN_MATCH)
                    break;
            }
            s->ins_h = ins_h;
        }
        /* If the whole input has less than MIN_MATCH bytes, ins_h is garbage,
         * but this is not important since only literal bytes will be emitted.
         */

    } while (s->lookahead < MIN_LOOKAHEAD && s->strm->avail_in != 0);

    /* If the WIN_INIT bytes after the end of the current data have never been
     * written, then zero those bytes in order to avoid memory check reports of
     * the use of uninitialized (or uninitialised as Julian writes) bytes by
     * the longest match routines.  Update the high water mark for the next
     * time through here.  WIN_INIT is set to MAX_MATCH since the longest match
     * routines allow scanning to strstart + MAX_MATCH, ignoring lookahead.
     */
    if (s->high_water < s->window_size) {
        uint64_t curr = s->strstart + (ulg)(s->lookahead);
        uint64_t init;

        if (s->high_water < curr) {
            /* Previous high water mark below current data -- zero WIN_INIT
             * bytes or up to end of window, whichever is less.
             */
            init = s->window_size - curr;
            if (init > WIN_INIT)
                init = WIN_INIT;
            zmemzero(s->window + curr, (unsigned)init);
            s->high_water = curr + init;
        }
        else if (s->high_water < (uint64_t)curr + WIN_INIT) {
            /* High water mark at or above current data, but below current data
             * plus WIN_INIT -- zero out to current data plus WIN_INIT, or up
             * to end of window, whichever is less.
             */
            init = (uint64_t)curr + WIN_INIT - s->high_water;
            if (init > s->window_size - s->high_water)
                init = s->window_size - s->high_water;
            zmemzero(s->window + s->high_water, (unsigned)init);
            s->high_water += init;
        }
    }
    
    Assert((uint64_t)s->strstart <= s->window_size - MIN_LOOKAHEAD,
           "not enough room for search");
}

/* ===========================================================================
 * Flush the current block, with given end-of-file flag.
 * IN assertion: strstart is set to the end of the current match.
 */
#define FLUSH_BLOCK_ONLY(s, last, put) { \
  _tr_flush_block(s, (s->block_start >= 0L ? \
    (uint8_t *)&s->window[(uint64_t)s->block_start] : \
    (uint8_t *)Z_NULL), \
    (uint64_t)((int64_t)s->strstart - s->block_start), \
    (last), put); \
  s->block_start = s->strstart; \
  flush_pending(s->strm); \
}

/* Same but force premature exit if necessary. */
#define FLUSH_BLOCK(s, last, put) { \
   FLUSH_BLOCK_ONLY(s, last, put); \
   if (s->strm->avail_out == 0) return (last) ? finish_started : need_more; \
}

/* ===========================================================================
 * Compress as much as possible from the input stream, return the current
 * block state.
 * This function does not perform lazy evaluation of matches and inserts
 * new strings in the dictionary only for unmatched strings or for short
 * matches. It is used only for the fast compression options.
 */
static block_state deflate_fast(s, flush, put)
    deflate_state *s;
    int flush;
    uLong* put;
{
    IPos hash_head;       /* head of the hash chain */
    int bflush;           /* set if current block must be flushed */

    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need MAX_MATCH bytes
         * for the next match, plus MIN_MATCH bytes to insert the
         * string following the next match.
         */
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead == 0) break; /* flush the current block */
        }

        /* Insert the string window[strstart .. strstart+2] in the
         * dictionary, and set hash_head to the head of the hash chain:
         */
        hash_head = NIL;
        if (s->lookahead >= MIN_MATCH) {
            INSERT_STRING(s, s->strstart, hash_head);
        }

        /* Find the longest match, discarding those <= prev_length.
         * At this point we have always match_length < MIN_MATCH
         */
        if (hash_head != NIL && s->strstart - hash_head <= MAX_DIST(s)) {
            /* To simplify the code, we prevent matches with the string
             * of window index 0 (in particular we have to avoid a match
             * of the string with itself at the start of the input file).
             */
            s->match_length = longest_match (s, hash_head);
            /* longest_match() sets match_start */
        }
        if (s->match_length >= MIN_MATCH) {
            bflush = _tr_tally_dist(s, s->strstart - s->match_start,
                                    s->match_length - MIN_MATCH);

            s->lookahead -= s->match_length;

            /* Insert new strings in the hash table only if the match length
             * is not too large. This saves time but degrades compression.
             */
            if (s->match_length <= s->max_lazy_match &&
                s->lookahead >= MIN_MATCH) {
                s->match_length--; /* string at strstart already in table */
                do {
                    s->strstart++;
                    INSERT_STRING(s, s->strstart, hash_head);
                    /* strstart never exceeds WSIZE-MAX_MATCH, so there are
                     * always MIN_MATCH bytes ahead.
                     */
                } while (--s->match_length != 0);
                s->strstart++;
            } else {
                s->strstart += s->match_length;
                s->match_length = 0;
                INIT_HASH(s, s->ins_h, &s->window[s->strstart]);
                /* If lookahead < MIN_MATCH, ins_h is garbage, but it does not
                 * matter since it will be recomputed at next deflate call.
                 */
            }
        } else {
            /* No match, output a literal byte */
            bflush = _tr_tally_lit (s, s->window[s->strstart]);
            s->lookahead--;
            s->strstart++;
        }
        if (bflush) FLUSH_BLOCK(s, 0, put);
    }
    s->insert = s->strstart < MIN_MATCH-1 ? s->strstart : MIN_MATCH-1;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1, put);
        return finish_done;
    }
    if (s->sym_next)
        FLUSH_BLOCK(s, 0, put);
    return block_done;
}

/* ===========================================================================
 * Same as above, but achieves better compression. We use a lazy
 * evaluation for matches: a match is finally adopted only if there is
 * no better match at the next window position.
 */
static block_state deflate_slow(s, flush, put)
    deflate_state *s;
    int flush;
    uLong* put;
{
    IPos hash_head;          /* head of hash chain */
    int bflush;              /* set if current block must be flushed */

    /* Process the input block. */
    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need MAX_MATCH bytes
         * for the next match, plus MIN_MATCH bytes to insert the
         * string following the next match.
         */
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead == 0) break; /* flush the current block */
        }

        /* Insert the string window[strstart .. strstart+2] in the
         * dictionary, and set hash_head to the head of the hash chain:
         */
        hash_head = NIL;
        if (s->lookahead >= MIN_MATCH) {
            INSERT_STRING(s, s->strstart, hash_head);
        }

        /* Find the longest match, discarding those <= prev_length.
         */
        s->prev_length = s->match_length, s->prev_match = s->match_start;
        s->match_length = MIN_MATCH-1;

        if (hash_head != NIL && s->prev_length < s->max_lazy_match &&
            s->strstart - hash_head <= MAX_DIST(s)) {
            /* To simplify the code, we prevent matches with the string
             * of window index 0 (in particular we have to avoid a match
             * of the string with itself at the start of the input file).
             */
            s->match_length = longest_match (s, hash_head);
            /* longest_match() sets match_start */

            if (s->match_length <= 5 && (s->strategy == Z_FILTERED )) {

                /* If prev_match is also MIN_MATCH, match_start is garbage
                 * but we will ignore the current match anyway.
                 */
                s->match_length = MIN_MATCH-1;
            }
        }
        /* If there was a match at the previous step and the current
         * match is not better, output the previous match:
         */
        if (s->prev_length >= MIN_MATCH && s->match_length <= s->prev_length) {
            uint32_t mov_fwd ;
            uint32_t insert_cnt ;

            uint32_t max_insert = s->strstart + s->lookahead - MIN_MATCH;
            /* Do not insert strings in hash table beyond this. */

          bflush = _tr_tally_dist(s, s->strstart -1 - s->prev_match,
                                  s->prev_length - MIN_MATCH);

            /* Insert in hash table all strings up to the end of the match.
             * strstart-1 and strstart are already inserted. If there is not
             * enough lookahead, the last two strings are not inserted in
             * the hash table.
             */
            s->lookahead -= s->prev_length-1;

            mov_fwd = s->prev_length - 2;
            insert_cnt = mov_fwd;
            if (unlikely(insert_cnt > max_insert - s->strstart))
                insert_cnt = max_insert - s->strstart;

            bulk_insert_str(s, s->strstart + 1, insert_cnt);
            s->prev_length = 0;
            s->match_available = 0;
            s->match_length = MIN_MATCH-1;
            s->strstart += mov_fwd + 1;

            if (bflush) FLUSH_BLOCK(s, 0, put);

        } else if (s->match_available) {
            /* If there was no match at the previous position, output a
             * single literal. If there was a match but the current match
             * is longer, truncate the previous match to a single literal.
             */
            bflush = _tr_tally_lit(s, s->window[s->strstart-1]);
            if (bflush) {
                FLUSH_BLOCK_ONLY(s, 0, put);
            }
            s->strstart++;
            s->lookahead--;
            if (s->strm->avail_out == 0) return need_more;
        } else {
            /* There is no previous match to compare with, wait for
             * the next step to decide.
             */
            s->match_available = 1;
            s->strstart++;
            s->lookahead--;
        }
    }
    Assert (flush != Z_NO_FLUSH, "no flush?");
    if (s->match_available) {
        bflush = _tr_tally_lit(s, s->window[s->strstart-1]);
        s->match_available = 0;
    }
    s->insert = s->strstart < MIN_MATCH-1 ? s->strstart : MIN_MATCH-1;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1, put);
        return finish_done;
    }
    if (s->sym_next)
        FLUSH_BLOCK(s, 0, put);
    return block_done;
}
