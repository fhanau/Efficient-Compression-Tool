/* crc32.c -- compute the CRC-32 of a data stream
 * Copyright (C) 1995-2006, 2010, 2011, 2012 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 *
 * Thanks to Rodney Brown <rbrown64@csc.com.au> for his contribution of faster
 * CRC methods: exclusive-oring 32 bits of data at a time, and pre-computing
 * tables for updating the shift register in one step with three exclusive-ors
 * instead of four steps with four exclusive-ors.  This results in about a
 * factor of two increase in speed on a Power PC G4 (PPC7455) using gcc -O3.
 */
/*
 * Compute the CRC32 using a parallelized folding approach with the PCLMULQDQ 
 * instruction.
 *
 * A white paper describing this algorithm can be found at:
 * http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 * Authors:
 * 	Wajdi Feghali   <wajdi.k.feghali@intel.com>
 * 	Jim Guilford    <james.guilford@intel.com>
 * 	Vinodh Gopal    <vinodh.gopal@intel.com>
 * 	Erdinc Ozturk   <erdinc.ozturk@intel.com>
 * 	Jim Kukunas     <james.t.kukunas@linux.intel.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "zutil.h"

#if defined(__GNUC__) && defined(__PCLMUL__)

#include <immintrin.h>
#include <wmmintrin.h>

typedef struct internal_state {
  unsigned __attribute__((aligned(16))) crc0[4 * 5];
} deflate_state;

#define CRC_LOAD(s) \
    do { \
        __m128i xmm_crc0 = _mm_loadu_si128((__m128i *)s->crc0 + 0);\
        __m128i xmm_crc1 = _mm_loadu_si128((__m128i *)s->crc0 + 1);\
        __m128i xmm_crc2 = _mm_loadu_si128((__m128i *)s->crc0 + 2);\
        __m128i xmm_crc3 = _mm_loadu_si128((__m128i *)s->crc0 + 3);\
        __m128i xmm_crc_part = _mm_loadu_si128((__m128i *)s->crc0 + 4);

#define CRC_SAVE(s) \
        _mm_storeu_si128((__m128i *)s->crc0 + 0, xmm_crc0);\
        _mm_storeu_si128((__m128i *)s->crc0 + 1, xmm_crc1);\
        _mm_storeu_si128((__m128i *)s->crc0 + 2, xmm_crc2);\
        _mm_storeu_si128((__m128i *)s->crc0 + 3, xmm_crc3);\
        _mm_storeu_si128((__m128i *)s->crc0 + 4, xmm_crc_part);\
    } while (0);

static void crc_fold_init(deflate_state* s)
{
    CRC_LOAD(s)

    xmm_crc0 = _mm_cvtsi32_si128(0x9db42487);
    xmm_crc1 = _mm_setzero_si128();
    xmm_crc2 = _mm_setzero_si128();
    xmm_crc3 = _mm_setzero_si128();

    CRC_SAVE(s)
}

static void fold_1(__m128i *xmm_crc0, __m128i *xmm_crc1, __m128i *xmm_crc2, __m128i *xmm_crc3)
{
    __m128i xmm_fold4 = _mm_set_epi32(
            0x00000001, 0x54442bd4,
            0x00000001, 0xc6e41596);
    
    __m128i x_tmp3;
    __m128 ps_crc0, ps_crc3, ps_res;

    x_tmp3 = *xmm_crc3;

    *xmm_crc3 = *xmm_crc0;
    *xmm_crc0 = _mm_clmulepi64_si128(*xmm_crc0, xmm_fold4, 0x01);
    *xmm_crc3 = _mm_clmulepi64_si128(*xmm_crc3, xmm_fold4, 0x10);
    ps_crc0 = _mm_castsi128_ps(*xmm_crc0);
    ps_crc3 = _mm_castsi128_ps(*xmm_crc3);
    ps_res = _mm_xor_ps(ps_crc0, ps_crc3);

    *xmm_crc0 = *xmm_crc1;
    *xmm_crc1 = *xmm_crc2;
    *xmm_crc2 = x_tmp3;
    *xmm_crc3 = _mm_castps_si128(ps_res);
}

static void fold_2(__m128i *xmm_crc0, __m128i *xmm_crc1, __m128i *xmm_crc2, __m128i *xmm_crc3)
{
    __m128i xmm_fold4 = _mm_set_epi32(
            0x00000001, 0x54442bd4,
            0x00000001, 0xc6e41596);

    __m128i x_tmp3, x_tmp2;
    __m128 ps_crc0, ps_crc1, ps_crc2, ps_crc3, ps_res31, ps_res20;

    x_tmp3 = *xmm_crc3;
    x_tmp2 = *xmm_crc2;

    *xmm_crc3 = *xmm_crc1;
    *xmm_crc1 = _mm_clmulepi64_si128(*xmm_crc1, xmm_fold4, 0x01);
    *xmm_crc3 = _mm_clmulepi64_si128(*xmm_crc3, xmm_fold4, 0x10);
    ps_crc3 = _mm_castsi128_ps(*xmm_crc3);
    ps_crc1 = _mm_castsi128_ps(*xmm_crc1);
    ps_res31= _mm_xor_ps(ps_crc3, ps_crc1);

    *xmm_crc2 = *xmm_crc0;
    *xmm_crc0 = _mm_clmulepi64_si128(*xmm_crc0, xmm_fold4, 0x01);
    *xmm_crc2 = _mm_clmulepi64_si128(*xmm_crc2, xmm_fold4, 0x10);
    ps_crc0 = _mm_castsi128_ps(*xmm_crc0);
    ps_crc2 = _mm_castsi128_ps(*xmm_crc2);
    ps_res20= _mm_xor_ps(ps_crc0, ps_crc2);

    *xmm_crc0 = x_tmp2;
    *xmm_crc1 = x_tmp3;
    *xmm_crc2 = _mm_castps_si128(ps_res20);
    *xmm_crc3 = _mm_castps_si128(ps_res31);
}

static void fold_3(__m128i *xmm_crc0, __m128i *xmm_crc1, __m128i *xmm_crc2, __m128i *xmm_crc3)
{
    __m128i xmm_fold4 = _mm_set_epi32(
            0x00000001, 0x54442bd4,
            0x00000001, 0xc6e41596);

    __m128i x_tmp3;
    __m128 ps_crc0, ps_crc1, ps_crc2, ps_crc3, ps_res32, ps_res21, ps_res10;

    x_tmp3 = *xmm_crc3;

    *xmm_crc3 = *xmm_crc2;
    *xmm_crc2 = _mm_clmulepi64_si128(*xmm_crc2, xmm_fold4, 0x01);
    *xmm_crc3 = _mm_clmulepi64_si128(*xmm_crc3, xmm_fold4, 0x10);
    ps_crc2 = _mm_castsi128_ps(*xmm_crc2);
    ps_crc3 = _mm_castsi128_ps(*xmm_crc3);
    ps_res32 = _mm_xor_ps(ps_crc2, ps_crc3);

    *xmm_crc2 = *xmm_crc1;
    *xmm_crc1 = _mm_clmulepi64_si128(*xmm_crc1, xmm_fold4, 0x01);
    *xmm_crc2 = _mm_clmulepi64_si128(*xmm_crc2, xmm_fold4, 0x10);
    ps_crc1 = _mm_castsi128_ps(*xmm_crc1);
    ps_crc2 = _mm_castsi128_ps(*xmm_crc2);
    ps_res21= _mm_xor_ps(ps_crc1, ps_crc2);

    *xmm_crc1 = *xmm_crc0;
    *xmm_crc0 = _mm_clmulepi64_si128(*xmm_crc0, xmm_fold4, 0x01);
    *xmm_crc1 = _mm_clmulepi64_si128(*xmm_crc1, xmm_fold4, 0x10);
    ps_crc0 = _mm_castsi128_ps(*xmm_crc0);
    ps_crc1 = _mm_castsi128_ps(*xmm_crc1);
    ps_res10= _mm_xor_ps(ps_crc0, ps_crc1);

    *xmm_crc0 = x_tmp3;
    *xmm_crc1 = _mm_castps_si128(ps_res10);
    *xmm_crc2 = _mm_castps_si128(ps_res21);
    *xmm_crc3 = _mm_castps_si128(ps_res32);
}

static void fold_4(__m128i *xmm_crc0, __m128i *xmm_crc1, __m128i *xmm_crc2, __m128i *xmm_crc3)
{
    __m128i xmm_fold4 = _mm_set_epi32(
            0x00000001, 0x54442bd4,
            0x00000001, 0xc6e41596);

    __m128i x_tmp0, x_tmp1, x_tmp2, x_tmp3;
  
    x_tmp0 = *xmm_crc0;
    x_tmp1 = *xmm_crc1;
    x_tmp2 = *xmm_crc2;
    x_tmp3 = *xmm_crc3;

    *xmm_crc0 = _mm_clmulepi64_si128(*xmm_crc0, xmm_fold4, 0x01);
    x_tmp0 = _mm_clmulepi64_si128(x_tmp0, xmm_fold4, 0x10);
    *xmm_crc0 = _mm_xor_si128(*xmm_crc0, x_tmp0);

    *xmm_crc1 = _mm_clmulepi64_si128(*xmm_crc1, xmm_fold4, 0x01);
    x_tmp1 = _mm_clmulepi64_si128(x_tmp1, xmm_fold4, 0x10);
    *xmm_crc1 = _mm_xor_si128(*xmm_crc1, x_tmp1);

    *xmm_crc2 = _mm_clmulepi64_si128(*xmm_crc2, xmm_fold4, 0x01);
    x_tmp2 = _mm_clmulepi64_si128(x_tmp2, xmm_fold4, 0x10);
    *xmm_crc2 = _mm_xor_si128(*xmm_crc2, x_tmp2);

    *xmm_crc3 = _mm_clmulepi64_si128(*xmm_crc3, xmm_fold4, 0x01);
    x_tmp3 = _mm_clmulepi64_si128(x_tmp3, xmm_fold4, 0x10);
    *xmm_crc3 = _mm_xor_si128(*xmm_crc3, x_tmp3);
}

static const unsigned __attribute__((aligned(32))) pshufb_shf_table[60] = {
	0x84838281,0x88878685,0x8c8b8a89,0x008f8e8d, /* shl 15 (16 - 1)/shr1 */
	0x85848382,0x89888786,0x8d8c8b8a,0x01008f8e, /* shl 14 (16 - 3)/shr2 */
	0x86858483,0x8a898887,0x8e8d8c8b,0x0201008f, /* shl 13 (16 - 4)/shr3 */
	0x87868584,0x8b8a8988,0x8f8e8d8c,0x03020100, /* shl 12 (16 - 4)/shr4 */
	0x88878685,0x8c8b8a89,0x008f8e8d,0x04030201, /* shl 11 (16 - 5)/shr5 */
	0x89888786,0x8d8c8b8a,0x01008f8e,0x05040302, /* shl 10 (16 - 6)/shr6 */
	0x8a898887,0x8e8d8c8b,0x0201008f,0x06050403, /* shl  9 (16 - 7)/shr7 */
	0x8b8a8988,0x8f8e8d8c,0x03020100,0x07060504, /* shl  8 (16 - 8)/shr8 */
	0x8c8b8a89,0x008f8e8d,0x04030201,0x08070605, /* shl  7 (16 - 9)/shr9 */
	0x8d8c8b8a,0x01008f8e,0x05040302,0x09080706, /* shl  6 (16 -10)/shr10*/
	0x8e8d8c8b,0x0201008f,0x06050403,0x0a090807, /* shl  5 (16 -11)/shr11*/
	0x8f8e8d8c,0x03020100,0x07060504,0x0b0a0908, /* shl  4 (16 -12)/shr12*/
	0x008f8e8d,0x04030201,0x08070605,0x0c0b0a09, /* shl  3 (16 -13)/shr13*/
	0x01008f8e,0x05040302,0x09080706,0x0d0c0b0a, /* shl  2 (16 -14)/shr14*/
	0x0201008f,0x06050403,0x0a090807,0x0e0d0c0b  /* shl  1 (16 -15)/shr15*/
};

static void partial_fold(size_t len,
        __m128i *xmm_crc0, __m128i *xmm_crc1,
        __m128i *xmm_crc2, __m128i *xmm_crc3,
        __m128i *xmm_crc_part)
{

    __m128i xmm_fold4 = _mm_set_epi32(
            0x00000001, 0x54442bd4,
            0x00000001, 0xc6e41596);
    __m128i xmm_mask3 = _mm_set1_epi32(0x80808080);
    
    __m128i xmm_shl, xmm_shr, xmm_tmp1, xmm_tmp2, xmm_tmp3;
    __m128i xmm_a0_0, xmm_a0_1;
    __m128 ps_crc3, psa0_0, psa0_1, ps_res;

    xmm_shl = _mm_load_si128((__m128i *)pshufb_shf_table + (len - 1));
    xmm_shr = xmm_shl;
    xmm_shr = _mm_xor_si128(xmm_shr, xmm_mask3);

    xmm_a0_0 = _mm_shuffle_epi8(*xmm_crc0, xmm_shl);

    *xmm_crc0 = _mm_shuffle_epi8(*xmm_crc0, xmm_shr);
    xmm_tmp1 = _mm_shuffle_epi8(*xmm_crc1, xmm_shl);
    *xmm_crc0 = _mm_or_si128(*xmm_crc0, xmm_tmp1);

    *xmm_crc1 = _mm_shuffle_epi8(*xmm_crc1, xmm_shr);
    xmm_tmp2 = _mm_shuffle_epi8(*xmm_crc2, xmm_shl);
    *xmm_crc1 = _mm_or_si128(*xmm_crc1, xmm_tmp2);

    *xmm_crc2 = _mm_shuffle_epi8(*xmm_crc2, xmm_shr);
    xmm_tmp3 = _mm_shuffle_epi8(*xmm_crc3, xmm_shl);
    *xmm_crc2 = _mm_or_si128(*xmm_crc2, xmm_tmp3);

    *xmm_crc3 = _mm_shuffle_epi8(*xmm_crc3, xmm_shr);
    *xmm_crc_part = _mm_shuffle_epi8(*xmm_crc_part, xmm_shl);
    *xmm_crc3 = _mm_or_si128(*xmm_crc3, *xmm_crc_part);

    xmm_a0_1 = _mm_clmulepi64_si128(xmm_a0_0, xmm_fold4, 0x10);
    xmm_a0_0 = _mm_clmulepi64_si128(xmm_a0_0, xmm_fold4, 0x01);

    ps_crc3 = _mm_castsi128_ps(*xmm_crc3);
    psa0_0 = _mm_castsi128_ps(xmm_a0_0);
    psa0_1 = _mm_castsi128_ps(xmm_a0_1);

    ps_res = _mm_xor_ps(ps_crc3, psa0_0);
    ps_res = _mm_xor_ps(ps_res, psa0_1);

    *xmm_crc3 = _mm_castps_si128(ps_res);
}

static void crc_fold(deflate_state * s,
        const unsigned char *src, long len)
{
    unsigned long algn_diff;
    __m128i xmm_t0, xmm_t1, xmm_t2, xmm_t3;

    CRC_LOAD(s)

    if (len < 16) {
      char __attribute__((aligned((16)))) partial_buf[16] = { 0 };

      if (len == 0)
        return;

      memcpy(partial_buf, src, len);
      xmm_crc_part = _mm_loadu_si128((const __m128i *)partial_buf);
      goto partial;
    }

    algn_diff = 0 - (unsigned long)src & 0xF;
    if (algn_diff) {
        xmm_crc_part = _mm_loadu_si128((__m128i *)src);

        src += algn_diff;
        len -= algn_diff;

        partial_fold(algn_diff, &xmm_crc0, &xmm_crc1, &xmm_crc2, &xmm_crc3,
            &xmm_crc_part);
    }

    while ((len -= 64) >= 0) {
        xmm_t0 = _mm_load_si128((__m128i *)src);
        xmm_t1 = _mm_load_si128((__m128i *)src + 1);
        xmm_t2 = _mm_load_si128((__m128i *)src + 2);
        xmm_t3 = _mm_load_si128((__m128i *)src + 3);

        fold_4(&xmm_crc0, &xmm_crc1, &xmm_crc2, &xmm_crc3);



        xmm_crc0 = _mm_xor_si128(xmm_crc0, xmm_t0);
        xmm_crc1 = _mm_xor_si128(xmm_crc1, xmm_t1);
        xmm_crc2 = _mm_xor_si128(xmm_crc2, xmm_t2);
        xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_t3);
        
        src += 64;
    }

    /*
     * len = num bytes left - 64
     */
    if (len + 16 >= 0) {
        len += 16;

        xmm_t0 = _mm_load_si128((__m128i *)src);
        xmm_t1 = _mm_load_si128((__m128i *)src + 1);
        xmm_t2 = _mm_load_si128((__m128i *)src + 2);

        fold_3(&xmm_crc0, &xmm_crc1, &xmm_crc2, &xmm_crc3);

        xmm_crc1 = _mm_xor_si128(xmm_crc1, xmm_t0);
        xmm_crc2 = _mm_xor_si128(xmm_crc2, xmm_t1);
        xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_t2);

        if (len == 0)
            goto done;

        xmm_crc_part = _mm_load_si128((__m128i *)src + 3);
    } else if (len + 32 >= 0) {
        len += 32;

        xmm_t0 = _mm_load_si128((__m128i *)src);
        xmm_t1 = _mm_load_si128((__m128i *)src + 1);

        fold_2(&xmm_crc0, &xmm_crc1, &xmm_crc2, &xmm_crc3);

        xmm_crc2 = _mm_xor_si128(xmm_crc2, xmm_t0);
        xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_t1);

        if (len == 0)
            goto done;

        xmm_crc_part = _mm_load_si128((__m128i *)src + 2);
    } else if (len + 48 >= 0) {
        len += 48;

        xmm_t0 = _mm_load_si128((__m128i *)src);

        fold_1(&xmm_crc0, &xmm_crc1, &xmm_crc2, &xmm_crc3);

        xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_t0);

        if (len == 0)
            goto done;
        
        xmm_crc_part = _mm_load_si128((__m128i *)src + 1);
    } else {
        len += 64;
        if (len == 0)
            goto done;
        xmm_crc_part = _mm_load_si128((__m128i *)src);
    }

partial:
    partial_fold(len, &xmm_crc0, &xmm_crc1, &xmm_crc2, &xmm_crc3,
        &xmm_crc_part);
done:
    CRC_SAVE(s)
}

static const unsigned __attribute__((aligned(16))) crc_k[] = {
    0xccaa009e, 0x00000000, /* rk1 */
    0x751997d0, 0x00000001, /* rk2 */
    0xccaa009e, 0x00000000, /* rk5 */
    0x63cd6124, 0x00000001, /* rk6 */
    0xf7011640, 0x00000001, /* rk7 */
    0xdb710640, 0x00000001  /* rk8 */
};

static const unsigned __attribute__((aligned(16))) crc_mask[4] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000
};

static const unsigned __attribute__((aligned(16))) crc_mask2[4] = {
    0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned crc_fold_512to32(deflate_state* s)
{
    __m128i xmm_mask  = _mm_load_si128((__m128i *)crc_mask);
    __m128i xmm_mask2 = _mm_load_si128((__m128i *)crc_mask2);

    unsigned crc;
    __m128i x_tmp0, x_tmp1, x_tmp2, crc_fold;

    CRC_LOAD(s)

    /*
     * k1
     */
    crc_fold = _mm_load_si128((__m128i *)crc_k);

    x_tmp0 = _mm_clmulepi64_si128(xmm_crc0, crc_fold, 0x10);
    xmm_crc0 = _mm_clmulepi64_si128(xmm_crc0, crc_fold, 0x01);
    xmm_crc1 = _mm_xor_si128(xmm_crc1, x_tmp0);
    xmm_crc1 = _mm_xor_si128(xmm_crc1, xmm_crc0);

    x_tmp1 = _mm_clmulepi64_si128(xmm_crc1, crc_fold, 0x10);
    xmm_crc1 = _mm_clmulepi64_si128(xmm_crc1, crc_fold, 0x01);
    xmm_crc2 = _mm_xor_si128(xmm_crc2, x_tmp1);
    xmm_crc2 = _mm_xor_si128(xmm_crc2, xmm_crc1);

    x_tmp2 = _mm_clmulepi64_si128(xmm_crc2, crc_fold, 0x10);
    xmm_crc2 = _mm_clmulepi64_si128(xmm_crc2, crc_fold, 0x01);
    xmm_crc3 = _mm_xor_si128(xmm_crc3, x_tmp2);
    xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_crc2);

    /*
     * k5
     */
    crc_fold = _mm_load_si128((__m128i *)crc_k + 1);

    xmm_crc0 = xmm_crc3;
    xmm_crc3 = _mm_clmulepi64_si128(xmm_crc3, crc_fold, 0);
    xmm_crc0 = _mm_srli_si128(xmm_crc0, 8);
    xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_crc0);

    xmm_crc0 = xmm_crc3;
    xmm_crc3 = _mm_slli_si128(xmm_crc3, 4);
    xmm_crc3 = _mm_clmulepi64_si128(xmm_crc3, crc_fold, 0x10);
    xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_crc0);
    xmm_crc3 = _mm_and_si128(xmm_crc3, xmm_mask2);

    /*
     * k7
     */
    xmm_crc1 = xmm_crc3;
    xmm_crc2 = xmm_crc3;
    crc_fold = _mm_load_si128((__m128i *)crc_k + 2);

    xmm_crc3 = _mm_clmulepi64_si128(xmm_crc3, crc_fold, 0);
    xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_crc2);
    xmm_crc3 = _mm_and_si128(xmm_crc3, xmm_mask);

    xmm_crc2 = xmm_crc3;
    xmm_crc3 = _mm_clmulepi64_si128(xmm_crc3, crc_fold, 0x10);
    xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_crc2);
    xmm_crc3 = _mm_xor_si128(xmm_crc3, xmm_crc1);

    crc = _mm_extract_epi32(xmm_crc3, 2);
    return ~crc;
    CRC_SAVE(s)
}

unsigned long crc32_SSSE3(const unsigned char* buf, unsigned len)
{
  deflate_state s;
  crc_fold_init(&s);
  crc_fold(&s, buf, len);
  return crc_fold_512to32(&s);
}
#endif

/* Definitions for doing the crc four data bytes at a time. */
#define BYFOUR
#ifdef BYFOUR
static unsigned long crc32_little (unsigned long,
                                   const unsigned char *, unsigned);
static unsigned long crc32_big (unsigned long,
                                const unsigned char *, unsigned);
#endif /* BYFOUR */

#include "crc32.h"

#define DO1 crc = crc_table[0][((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8)
#define DO8 DO1; DO1; DO1; DO1; DO1; DO1; DO1; DO1

static unsigned long crc32_generic(unsigned long crc, const unsigned char *buf, unsigned len)
{
  if (!buf) return 0UL;

#ifdef BYFOUR
  if (sizeof(void *) == sizeof(ptrdiff_t)) {
    unsigned endian;

    endian = 1;
    if (*((unsigned char *)(&endian)))
      return crc32_little(crc, buf, len);
    else
      return crc32_big(crc, buf, len);
  }
#endif /* BYFOUR */
  crc = crc ^ 0xffffffffUL;
  while (len >= 8) {
    DO8;
    len -= 8;
  }
  if (len) do {
    DO1;
  } while (--len);
  return crc ^ 0xffffffffUL;
}

unsigned long crc32(crc, buf, len)
unsigned long crc;
const unsigned char *buf;
unsigned len;
{
#if defined(__GNUC__) && defined(__PCLMUL__)
  if(!crc && len){
    return crc32_SSSE3(buf, len);
  }
#endif
  return crc32_generic(crc, buf, len);
}

#ifdef BYFOUR

#define DOLIT4 c ^= *buf4++; \
c = crc_table[3][c & 0xff] ^ crc_table[2][(c >> 8) & 0xff] ^ \
crc_table[1][(c >> 16) & 0xff] ^ crc_table[0][c >> 24]
#define DOLIT32 DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4

static unsigned long crc32_little(unsigned long crc, const unsigned char *buf, unsigned len)
{
  register unsigned c;
  register const unsigned *buf4;

  c = (unsigned)crc;
  c = ~c;
  while (len && ((ptrdiff_t)buf & 3)) {
    c = crc_table[0][(c ^ *buf++) & 0xff] ^ (c >> 8);
    len--;
  }

  buf4 = (const unsigned *)(const void *)buf;
  while (len >= 32) {
    DOLIT32;
    len -= 32;
  }
  while (len >= 4) {
    DOLIT4;
    len -= 4;
  }
  buf = (const unsigned char *)buf4;

  if (len) do {
    c = crc_table[0][(c ^ *buf++) & 0xff] ^ (c >> 8);
  } while (--len);
  c = ~c;
  return (unsigned long)c;
}

#define DOBIG4 c ^= *++buf4; \
c = crc_table[4][c & 0xff] ^ crc_table[5][(c >> 8) & 0xff] ^ \
crc_table[6][(c >> 16) & 0xff] ^ crc_table[7][c >> 24]
#define DOBIG32 DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4

static unsigned long crc32_big(crc, buf, len)
unsigned long crc;
const unsigned char *buf;
unsigned len;
{
  register unsigned c;
  register const unsigned *buf4;

  c = ZSWAP32((unsigned)crc);
  c = ~c;
  while (len && ((ptrdiff_t)buf & 3)) {
    c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
    len--;
  }

  buf4 = (const unsigned *)(const void *)buf;
  buf4--;
  while (len >= 32) {
    DOBIG32;
    len -= 32;
  }
  while (len >= 4) {
    DOBIG4;
    len -= 4;
  }
  buf4++;
  buf = (const unsigned char *)buf4;

  if (len) do {
    c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
  } while (--len);
  c = ~c;
  return (unsigned long)(ZSWAP32(c));
}

#endif /* BYFOUR */
