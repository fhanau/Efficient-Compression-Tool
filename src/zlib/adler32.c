/*
 * adler32.c -- compute the Adler-32 checksum of a data stream
 *   x86 implementation
 * Copyright (C) 1995-2007 Mark Adler
 * Copyright (C) 2009-2012 Jan Seiffert
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include "zutil.h"

#define BASE 65521      /* largest prime smaller than 65536 */
#define NMAX 5552
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */

#if defined(__SSSE3__) && defined (__GNUC__)

static const struct { char d[16]; } vord_b __attribute__((__aligned__(16))) = {
    {16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1}
};

static const unsigned char *adler32_jumped(buf, s1, s2, k)
    const unsigned char *buf;
    unsigned *s1;
    unsigned *s2;
    unsigned k;
{
    unsigned t;
    unsigned n = k % 16;
    buf += n;
    k = (k / 16) + 1;

    __asm__ __volatile__ (
#    define CLOB "&"
            "lea	1f(%%rip), %q4\n\t"
            "lea	(%q4,%q5,8), %q4\n\t"
            "jmp	*%q4\n\t"
            ".p2align 1\n"
            "2:\n\t"
#  ifdef __i386
            ".byte 0x3e\n\t"
#  endif
            "add	$0x10, %2\n\t"
            ".p2align 1\n"
            "1:\n\t"
            /* 128 */
            "movzbl	-16(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /* 120 */
            "movzbl	-15(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /* 112 */
            "movzbl	-14(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /* 104 */
            "movzbl	-13(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  96 */
            "movzbl	-12(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  88 */
            "movzbl	-11(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  80 */
            "movzbl	-10(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  72 */
            "movzbl	 -9(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  64 */
            "movzbl	 -8(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  56 */
            "movzbl	 -7(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  48 */
            "movzbl	 -6(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  40 */
            "movzbl	 -5(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  32 */
            "movzbl	 -4(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  24 */
            "movzbl	 -3(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*  16 */
            "movzbl	 -2(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*   8 */
            "movzbl	 -1(%2), %4\n\t"	/* 4 */
            "add	%4, %0\n\t"	/* 2 */
            "add	%0, %1\n\t"	/* 2 */
            /*   0 */
            "dec	%3\n\t"
            "jnz	2b"
        : /* %0 */ "=R" (*s1),
          /* %1 */ "=R" (*s2),
          /* %2 */ "=abdSD" (buf),
          /* %3 */ "=c" (k),
          /* %4 */ "="CLOB"R" (t)
        : /* %5 */ "r" (16 - n),
          /*    */ "0" (*s1),
          /*    */ "1" (*s2),
          /*    */ "2" (buf),
          /*    */ "3" (k)
        : "cc", "memory"
    );

    return buf;
}

#define TO_STR2(x) #x
#define TO_STR(x) TO_STR2(x)
#define PMADDUBSW_XMM5_XMM0 "pmaddubsw	%%xmm5, %%xmm0\n\t"
#define PALIGNR_XMM0_XMM0(o) "palignr	$("TO_STR(o)"), %%xmm0, %%xmm0\n\t"
#define PALIGNR_XMM1_XMM0(o) "palignr	$("TO_STR(o)"), %%xmm1, %%xmm0\n\t"
#   define NREG "q"
unsigned long ZEXPORT adler32(unsigned long adler, const unsigned char* buf,unsigned len)
{
    unsigned s1 = adler & 0xffff;
    unsigned s2 = (adler >> 16) & 0xffff;
    unsigned k,t;

    __asm__ __volatile__ (
            "jb	9f\n\t"				/* if(k < 32) goto OUT */
            "prefetchnta	0x70(%0)\n\t"
            "movdqa	%6, %%xmm5\n\t"		/* get vord_b */
            "movd	%k2, %%xmm2\n\t"	/* init vector sum vs2 with s2 */
            "movd	%k1, %%xmm3\n\t"	/* init vector sum vs1 with s1 */
            "pxor	%%xmm4, %%xmm4\n"	/* zero */
            "mov	%k0, %k3\n\t"		/* copy buf pointer */
            "and	$-16, %0\n\t"		/* align buf pointer down */
            "xor	%k0, %k3\n\t"		/* extract num_misaligned */
            "jz	4f\n\t"				/* no misalignment? goto start of loop */
            "movdqa	(%0), %%xmm0\n\t"	/* fetch input data as first arg */
            "pxor	%%xmm1, %%xmm1\n\t"	/* clear second arg */
            "mov	%3, %5\n\t"		/* copy num_misaligned as last arg */
            "call	ssse3_align_l\n"	/* call ssse3 left alignment helper */
            "sub	$16, %k3\n\t"		/* build valid bytes ... */
            "add	$16, %0\n\t"		/* advance input data pointer */
            "neg	%k3\n\t"		/* ... out of num_misaligned */
            "sub	%k3, %k4\n\t"		/* len -= valid bytes */
            "imul	%k1, %k3\n\t"		/* valid_bytes *= s1 */
            "movd	%k3, %%xmm1\n\t"	/* transfer to sse */
            "pslldq	$12, %%xmm1\n\t"	/* mov into high dword */
            "por	%%xmm1, %%xmm2\n\t"	/* stash s1 times valid_bytes in vs2 */
            "movdqa	%%xmm0, %%xmm1\n\t"	/* make a copy of the input data */
            PMADDUBSW_XMM5_XMM0		/* multiply all input bytes by vord_b bytes, add adjecent results to words */
            "psadbw	%%xmm4, %%xmm1\n\t"	/* subtract zero from every byte, add 8 bytes to a sum */
            "movdqa	%%xmm0, %%xmm6\n\t"	/* copy word mul result */
            "paddd	%%xmm1, %%xmm3\n\t"	/* vs1 += psadbw */
            "punpckhwd	%%xmm4, %%xmm0\n\t"	/* zero extent upper words to dwords */
            "punpcklwd	%%xmm4, %%xmm6\n\t"	/* zero extent lower words to dwords */
            "paddd	%%xmm0, %%xmm2\n\t"	/* vs2 += vs2_i.upper */
            "paddd	%%xmm6, %%xmm2\n\t"	/* vs2 += vs2_i.lower */
            "jmp	4f\n\t"
#  if defined(__ELF__) && !defined(__clang__)
            ".subsection 2\n\t"
#  endif
            ".p2align 2\n"
            /*
             * helper function to align data in sse reg
             * will keep %5 bytes in xmm0 on the left
             * and fill the rest with bytes from xmm1
             * (note: little-endian -> left == higher addr)
             */
            "ssse3_align_l:\n\t"
            "sub	$1, %5\n\t"	/* k -= 1 */
            "shl	$4, %5\n\t"	/* k *= 16 */
            "lea	7f(%%rip), %%r11\n\t"
            "lea	(%%r11,%q5, 1), %q5\n\t"
            "jmp	*%q5\n\t"
            ".p2align 2\n"
            /*
             * helper function to align data in sse reg
             * will push %5 bytes in xmm0 to the left
             * and fill the rest with bytes from xmm1
             * (note: little-endian -> left == higher addr)
             */
            "ssse3_align_r:\n\t"
            "sub	$15, %5\n\t"
            "neg	%5\n\t"		/* k = 16 - (k - 1) */
            "shl	$4, %5\n\t"	/* k *= 16 */
            "lea	6f(%%rip), %%r11\n\t"
            "lea	(%%r11,%q5, 1), %q5\n\t"
            "jmp	*%q5\n\t"

#  define ALIGNSTEP(o) \
    PALIGNR_XMM0_XMM0(o) \
    ".p2align 3\n\t" \
    PALIGNR_XMM1_XMM0(16-o) \
    "ret\n\t" \
    ".p2align 4\n\t"

            ".p2align 4\n"
            "7:\n\t"		/* 0 */
            PALIGNR_XMM0_XMM0(1) /* 6 */
            ".p2align 3\n\t" /* 2 */
            "6:\n\t"
            PALIGNR_XMM1_XMM0(16-1) /* 6 */
            "ret\n\t" /* 1 */
            ".p2align 4\n\t"	/* 16 */
            ALIGNSTEP(2)	/* 32 */
            ALIGNSTEP(3)	/* 48 */
            ALIGNSTEP(4)	/* 64 */
            ALIGNSTEP(5)	/* 80 */
            ALIGNSTEP(6)	/* 96 */
            ALIGNSTEP(7)	/* 112 */
            ALIGNSTEP(8)	/* 128 */
            ALIGNSTEP(9)	/* 144 */
            ALIGNSTEP(10)	/* 160 */
            ALIGNSTEP(11)	/* 176 */
            ALIGNSTEP(12)	/* 192 */
            ALIGNSTEP(13)	/* 208 */
            ALIGNSTEP(14)	/* 224 */
            ALIGNSTEP(15)	/* 256 */
#  undef ALIGNSTEP
            ".p2align 2\n"
            /*
             * reduction function to bring a vector sum within the range of BASE
             * This does no full reduction! When the sum is large, a number > BASE
             * is the result. To do a full reduction call multiple times.
             */
            "sse2_chop:\n\t"
            "movdqa	%%xmm0, %%xmm1\n\t"	/* y = x */
            "pslld	$16, %%xmm1\n\t"	/* y <<= 16 */
            "psrld	$16, %%xmm0\n\t"	/* x >>= 16 */
            "psrld	$16, %%xmm1\n\t"	/* y >>= 16 */
            "psubd	%%xmm0, %%xmm1\n\t"	/* y -= x */
            "pslld	$4, %%xmm0\n\t"		/* x <<= 4 */
            "paddd	%%xmm1, %%xmm0\n\t"	/* x += y */
            "ret\n\t"
#  if defined(__ELF__) && !defined(__clang__)
            ".previous\n\t"
#  endif
            ".p2align 2\n"
            "6:\n\t"
            "mov	$128, %1\n\t"		/* inner_k = 128 bytes till vs2_i overflows */
            "cmp	%1, %3\n\t"
            "cmovb	%3, %1\n\t"		/* inner_k = k >= inner_k ? inner_k : k */
            "and	$-16, %1\n\t"		/* inner_k = ROUND_TO(inner_k, 16) */
            "sub	%1, %3\n\t"		/* k -= inner_k */
            "shr	$4, %1\n\t"		/* inner_k /= 16 */
            "mov	$1, %5\n\t"		/* outer_k = 1 */
            "jmp	5f\n"			/* goto loop start */
            "3:\n\t"
            "pxor	%%xmm7, %%xmm7\n\t"	/* zero vs1_round_sum */
            "mov	%3, %5\n\t"		/* determine full inner_k (==8) rounds from k */
            "and	$-128, %5\n\t"		/* outer_k = ROUND_TO(outer_k, 128) */
            "sub	%5, %3\n\t"		/* k -= outer_k */
            "shr	$7, %5\n\t"		/* outer_k /= 128 */
            "jz         6b\n\t"			/* if outer_k == 0 handle trailer */
            ".p2align 3,,3\n\t"
            ".p2align 2\n"
            "2:\n\t"
            "mov	$8, %1\n"		/* inner_k = 8 */
            "5:\n\t"
            "pxor	%%xmm6, %%xmm6\n\t"	/* zero vs2_i */
            ".p2align 4,,7\n"
            ".p2align 3\n"
            "1:\n\t"
            "movdqa	(%0), %%xmm0\n\t"	/* fetch input data */
            "prefetchnta	0x70(%0)\n\t"
            "paddd	%%xmm3, %%xmm7\n\t"	/* vs1_round_sum += vs1 */
            "add	$16, %0\n\t"		/* advance input data pointer */
            "dec	%1\n\t"			/* decrement inner_k */
            "movdqa	%%xmm0, %%xmm1\n\t"	/* make a copy of the input data */
            PMADDUBSW_XMM5_XMM0		/* multiply all input bytes by vord_b bytes, add adjecent results to words */
            "psadbw	%%xmm4, %%xmm1\n\t"	/* subtract zero from every byte, add 8 bytes to a sum */
            "paddw	%%xmm0, %%xmm6\n\t"	/* vs2_i += in * vorder_b */
            "paddd	%%xmm1, %%xmm3\n\t"	/* vs1 += psadbw */
            "jnz	1b\n\t"			/* repeat if inner_k != 0 */
            "movdqa	%%xmm6, %%xmm0\n\t"	/* copy vs2_i */
            "punpckhwd	%%xmm4, %%xmm0\n\t"	/* zero extent vs2_i upper words to dwords */
            "punpcklwd	%%xmm4, %%xmm6\n\t"	/* zero extent vs2_i lower words to dwords */
            "paddd	%%xmm0, %%xmm2\n\t"	/* vs2 += vs2_i.upper */
            "paddd	%%xmm6, %%xmm2\n\t"	/* vs2 += vs2_i.lower */
            "dec	%5\n\t"			/* decrement outer_k  */
            "jnz	2b\n\t"			/* repeat with inner_k = 8 if outer_k != 0 */
            "cmp	$15, %3\n\t"
            "ja	6b\n\t"				/* if(k > 15) repeat */
            "movdqa	%%xmm7, %%xmm0\n\t"	/* move vs1_round_sum */
            "call	sse2_chop\n\t"		/* chop vs1_round_sum */
            "pslld	$4, %%xmm0\n\t"		/* vs1_round_sum *= 16 */
            "paddd	%%xmm2, %%xmm0\n\t"	/* vs2 += vs1_round_sum */
            "call	sse2_chop\n\t"		/* chop again */
            "movdqa	%%xmm0, %%xmm2\n\t"	/* move vs2 back in place */
            "movdqa	%%xmm3, %%xmm0\n\t"	/* move vs1 */
            "call	sse2_chop\n\t"		/* chop */
            "movdqa	%%xmm0, %%xmm3\n\t"	/* move vs1 back in place */
            "add	%3, %4\n"		/* len += k */
            "4:\n\t"	/* top of loop */
            "mov	%7, %3\n\t"		/* get max. byte count VNMAX till v1_round_sum overflows */
            "cmp	%3, %4\n\t"
            "cmovb	%4, %3\n\t"		/* k = len >= VNMAX ? k : len */
            "sub	%3, %4\n\t"		/* len -= k */
            "cmp	$15, %3\n\t"
            "ja	3b\n\t"				/* if(k > 15) repeat */
            "test	%k3, %k3\n\t"		/* test for 0 */
            "jz	5f\n\t"				/* if (k == 0) goto OUT */
            "movdqa	(%0), %%xmm0\n\t"	/* fetch remaining input data as first arg */
            "add	%"NREG"3, %"NREG"0\n\t"		/* add remainder to to input data pointer */
            "movdqa	%%xmm4, %%xmm1\n\t"	/* set second arg 0 */
            "mov	%k3, %k5\n\t"		/* copy remainder as last arg */
            "call	ssse3_align_r\n\t"	/* call ssse3 right alignment helper */
            "movdqa	%%xmm4, %%xmm7\n\t"	/* sum = 0 */
            "movdqa	%%xmm3, %%xmm1\n\t"	/* t = vs1 */
            /* russian peasant mul, k is small */
            ".p2align 2\n"
            "6:\n\t"
            "shr	$1, %k3\n\t"		/* k >>= 1 */
            "jnc	7f\n\t"
            "paddd	%%xmm1, %%xmm7\n\t"	/* add t to sum if 1 bit shifted out of k */
            "7:\n\t"
            "paddd	%%xmm1, %%xmm1\n\t"	/* t *= 2 */
            "jnz	6b\n\t"			/* while(k != 0) */
            "paddd	%%xmm7, %%xmm2\n\t"	/* vs2 += k * vs1 */
            "movdqa	%%xmm0, %%xmm1\n\t"	/* make a copy of the input data */
            PMADDUBSW_XMM5_XMM0		/* multiply all input bytes by vord_b bytes, add adjecent results to words */
            "psadbw	%%xmm4, %%xmm1\n\t"	/* subtract zero from every byte, add 8 bytes to a sum */
            "movdqa	%%xmm0, %%xmm6\n\t"	/* copy word mul result */
            "paddd	%%xmm1, %%xmm3\n\t"	/* vs1 += psadbw */
            "punpckhwd	%%xmm4, %%xmm0\n\t"	/* zero extent upper words to dwords */
            "punpcklwd	%%xmm4, %%xmm6\n\t"	/* zero extent lower words to dwords */
            "paddd	%%xmm0, %%xmm2\n\t"	/* vs2 += vs2_i.upper */
            "paddd	%%xmm6, %%xmm2\n\t"	/* vs2 += vs2_i.lower */
            "5:\n\t"	/* OUT */
            "pshufd	$0xEE, %%xmm3, %%xmm1\n\t"	/* collect vs1 & vs2 in lowest vector member */
            "pshufd	$0xEE, %%xmm2, %%xmm0\n\t"
            "paddd	%%xmm3, %%xmm1\n\t"
            "paddd	%%xmm2, %%xmm0\n\t"
            "pshufd	$0xE5, %%xmm0, %%xmm2\n\t"
            "paddd	%%xmm0, %%xmm2\n\t"
            "movd	%%xmm1, %1\n\t"		/* mov vs1 to s1 */
            "movd	%%xmm2, %2\n"		/* mov vs2 to s2 */
            "9:"
        : /* %0 */ "=r" (buf),
          /* %1 */ "=r" (s1),
          /* %2 */ "=r" (s2),
          /* %3 */ "=r" (k),
          /* %4 */ "=r" (len),
          /* %5 */ "=r" (t)
        : /* %6 */ "m" (vord_b),
          /*
           * somewhere between 5 & 6, psadbw 64 bit sums ruin the party
           * spreading the sums with palignr only brings it to 7 (?),
           * while introducing an op into the main loop (2800 ms -> 3200 ms)
           */
          /* %7 */ "i" (5*NMAX),
          /*    */ "0" (buf),
          /*    */ "1" (s1),
          /*    */ "2" (s2),
          /*    */ "4" (len)
        : "cc", "memory"
          , "r11"
          , "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"
    );

    if (__builtin_expect(!!(k), 0))
        buf = adler32_jumped(buf, &s1, &s2, k);
  s1 %= BASE;
  s2 %= BASE;
  return (s2 << 16) | s1;
}
#else

#define DO1(buf,i)  {adler += (buf)[i]; sum2 += adler;}
#define DO2(buf,i)  DO1(buf,i); DO1(buf,i+1);
#define DO4(buf,i)  DO2(buf,i); DO2(buf,i+2);
#define DO8(buf,i)  DO4(buf,i); DO4(buf,i+4);
#define DO16(buf)   DO8(buf,0); DO8(buf,8);

/* use NO_DIVIDE if your processor does not do division in hardware --
 try it both ways to see which is faster */
#ifdef NO_DIVIDE
/* note that this assumes BASE is 65521, where 65536 % 65521 == 15
 (thank you to John Reiser for pointing this out) */
#  define CHOP(a) \
do { \
unsigned long tmp = a >> 16; \
a &= 0xffffUL; \
a += (tmp << 4) - tmp; \
} while (0)
#  define MOD28(a) \
do { \
CHOP(a); \
if (a >= BASE) a -= BASE; \
} while (0)
#  define MOD(a) \
do { \
CHOP(a); \
MOD28(a); \
} while (0)
#else
#  define MOD(a) a %= BASE
#  define MOD28(a) a %= BASE
#endif

uLong ZEXPORT adler32(adler, buf, len)
uLong adler;
const Bytef *buf;
uInt len;
{
  unsigned long sum2;
  unsigned n;

  /* split Adler-32 into component sums */
  sum2 = (adler >> 16) & 0xffff;
  adler &= 0xffff;

  /* in case user likes doing a byte at a time, keep it fast */
  if (len == 1) {
    adler += buf[0];
    if (adler >= BASE)
      adler -= BASE;
    sum2 += adler;
    if (sum2 >= BASE)
      sum2 -= BASE;
    return adler | (sum2 << 16);
  }

  /* initial Adler-32 value (deferred check for len == 1 speed) */
  if (!buf)
    return 1L;

  /* in case short lengths are provided, keep it somewhat fast */
  if (len < 16) {
    while (len--) {
      adler += *buf++;
      sum2 += adler;
    }
    if (adler >= BASE)
      adler -= BASE;
    MOD28(sum2);            /* only added so many BASE's */
    return adler | (sum2 << 16);
  }

  /* do length NMAX blocks -- requires just one modulo operation */
  while (len >= NMAX) {
    len -= NMAX;
    n = NMAX / 16;          /* NMAX is divisible by 16 */
    do {
      DO16(buf);          /* 16 sums unrolled */
      buf += 16;
    } while (--n);
    MOD(adler);
    MOD(sum2);
  }

  /* do remaining bytes (less than NMAX, still just one modulo) */
  if (len) {                  /* avoid modulos if none remaining */
    while (len >= 16) {
      len -= 16;
      DO16(buf);
      buf += 16;
    }
    while (len--) {
      adler += *buf++;
      sum2 += adler;
    }
    MOD(adler);
    MOD(sum2);
  }

  /* return recombined sums */
  return adler | (sum2 << 16);
}
#endif
