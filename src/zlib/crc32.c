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

#ifdef HAS_PCLMUL
 #include "crc32_simd.h"
 #ifndef _MSC_VER
  #include <cpuid.h>
 #endif
#endif

#ifdef __aarch64__

#include <arm_neon.h>
#include <arm_acle.h>
#include <stdint.h>
#include <stddef.h>

uint32_t crc32(uint32_t crc, uint8_t *buf, size_t len) {
    crc = ~crc;

    while (len >= 8) {
        crc = __crc32d(crc, *(uint64_t*)buf);
        len -= 8;
        buf += 8;
    }

    if (len & 4) {
        crc = __crc32w(crc, *(uint32_t*)buf);
        buf += 4;
    }
    if (len & 2) {
        crc = __crc32h(crc, *(uint16_t*)buf);
        buf += 2;
    }
    if (len & 1) {
        crc = __crc32b(crc, *buf);
    }

    return ~crc;
}

#else

/* Definitions for doing the crc four data bytes at a time. */
#define BYFOUR
static unsigned long crc32_little (unsigned long,
                                   const unsigned char *, unsigned);
static unsigned long crc32_big (unsigned long,
                                const unsigned char *, unsigned);

/* ========================================================================
 * Tables of CRC-32s of all single-byte values, made by make_crc_table().
 */
#include "crc32.h"

#define DO1 crc = crc_table[0][((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8)
#define DO8 DO1; DO1; DO1; DO1; DO1; DO1; DO1; DO1

static unsigned long crc32_generic(unsigned long crc, const unsigned char *buf, uInt len)
{
  if (!buf) return 0UL;

#ifdef BYFOUR
  if (sizeof(void *) == sizeof(ptrdiff_t)) {
    z_crc_t endian;

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

#ifdef HAS_PCLMUL

#define PCLMUL_MIN_LEN 64
#define PCLMUL_ALIGN 16
#define PCLMUL_ALIGN_MASK 15

#if defined(__GNUC__)
    #if  __GNUC__ < 5
        int cpu_has_pclmul = -1; //e.g. gcc 4.8.4 https://stackoverflow.com/questions/20326604/stdatomic-h-in-gcc-4-8
    #else
        _Atomic int cpu_has_pclmul = -1; //global: will be 0 or 1 after first test
    #endif
#else
    #ifdef _MSC_VER
        int cpu_has_pclmul = -1; //e.g. gcc 4.8.4 https://stackoverflow.com/questions/20326604/stdatomic-h-in-gcc-4-8
    #else
        _Atomic int cpu_has_pclmul = -1; //global: will be 0 or 1 after first test
    #endif
#endif

int has_pclmul(void) {
    if (cpu_has_pclmul >= 0)
        return cpu_has_pclmul;
    cpu_has_pclmul = 0;
    int leaf = 1;
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    /* %ecx */
    #define crc_bit_PCLMUL (1 << 1)
    #ifdef _MSC_VER
    uint32_t regs[4]; // output: eax, ebx, ecx, edx
    __cpuid( regs, leaf );
    if (leaf == 1) {
        ecx = regs[2];
    #else
    if (__get_cpuid(leaf, &eax, &ebx, &ecx, &edx)) {
    #endif
        if ((ecx & crc_bit_PCLMUL) != 0)
            cpu_has_pclmul = 1;
    }
    return cpu_has_pclmul;
}

uLong crc32(crc, buf, len)
    uLong crc;
    const Bytef *buf;
    uInt len;
{
    if (len < PCLMUL_MIN_LEN + PCLMUL_ALIGN  - 1)
      return crc32_generic(crc, buf, len);
    #ifndef SKIP_CPUID_CHECK
    if (!has_pclmul())
      return crc32_generic(crc, buf, len);
    #endif
    /* Handle the leading patial chunk */
    uInt misalign = PCLMUL_ALIGN_MASK & ((unsigned long)buf);
    uInt sz = (PCLMUL_ALIGN - misalign) % PCLMUL_ALIGN;
    if (sz) {
      crc = crc32_generic(crc, buf, sz);
      buf += sz;
      len -= sz;
    }

    /* Go over 16-byte chunks */
    crc = crc32_sse42_simd_(buf, (len & ~PCLMUL_ALIGN_MASK), crc ^ 0xffffffffUL);
    crc = crc ^ 0xffffffffUL;

    /* Handle the trailing partial chunk */
    sz = len & PCLMUL_ALIGN_MASK;
    if (sz) {
      crc = crc32_generic(crc, buf + len - sz, sz);
    }

    return crc;
}
#undef PCLMUL_MIN_LEN
#undef PCLMUL_ALIGN
#undef PCLMUL_ALIGN_MASK

#else
uLong crc32(crc, buf, len)
uLong crc;
const Bytef *buf;
uInt len;
{
  return crc32_generic(crc, buf, len);
}
#endif

#ifdef BYFOUR

#define DOLIT4 c ^= *buf4++; \
c = crc_table[3][c & 0xff] ^ crc_table[2][(c >> 8) & 0xff] ^ \
crc_table[1][(c >> 16) & 0xff] ^ crc_table[0][c >> 24]
#define DOLIT32 DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4

static unsigned long crc32_little(unsigned long crc, const unsigned char *buf, unsigned len)
{
  register z_crc_t c;
  register const z_crc_t *buf4;

  c = (z_crc_t)crc;
  c = ~c;
  while (len && ((ptrdiff_t)buf & 3)) {
    c = crc_table[0][(c ^ *buf++) & 0xff] ^ (c >> 8);
    len--;
  }

  buf4 = (const z_crc_t *)(const void *)buf;
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

#define DOBIG4 c ^= *buf4++; \
c = crc_table[4][c & 0xff] ^ crc_table[5][(c >> 8) & 0xff] ^ \
crc_table[6][(c >> 16) & 0xff] ^ crc_table[7][c >> 24]
#define DOBIG32 DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4

static unsigned long crc32_big(crc, buf, len)
unsigned long crc;
const unsigned char *buf;
unsigned len;
{
  register z_crc_t c;
  register const z_crc_t *buf4;

  c = ZSWAP32((z_crc_t)crc);
  c = ~c;
  while (len && ((ptrdiff_t)buf & 3)) {
    c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
    len--;
  }

  buf4 = (const z_crc_t *)(const void *)buf;
  while (len >= 32) {
    DOBIG32;
    len -= 32;
  }
  while (len >= 4) {
    DOBIG4;
    len -= 4;
  }
  buf = (const unsigned char *)buf4;

  if (len) do {
    c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
  } while (--len);
  c = ~c;
  return (unsigned long)(ZSWAP32(c));
}

#endif /* BYFOUR */

#endif
