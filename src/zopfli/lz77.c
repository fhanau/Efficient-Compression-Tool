/*
Copyright 2011 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Author: lode.vandevenne@gmail.com (Lode Vandevenne)
Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)
*/

/*Modified by Felix Hanau*/

#include "lz77.h"
#include "util.h"
#include "match.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ZopfliInitLZ77Store(ZopfliLZ77Store* store) {
  store->size = 0;
  store->litlens = 0;
  store->dists = 0;
  store->symbols = 0;
}

void ZopfliCleanLZ77Store(ZopfliLZ77Store* store) {
  free(store->litlens);
  free(store->dists);
}

void ZopfliCopyLZ77Store(const ZopfliLZ77Store* source, ZopfliLZ77Store* dest) {
  ZopfliCleanLZ77Store(dest);
  dest->litlens = (unsigned short*)malloc(sizeof(*dest->litlens) * source->size);
  dest->dists = (unsigned short*)malloc(sizeof(*dest->dists) * source->size);

  if (!dest->litlens || !dest->dists) exit(1); /* Allocation failed. */

  dest->size = source->size;

  memcpy(dest->litlens, source->litlens, source->size * sizeof(*dest->dists));
  memcpy(dest->dists, source->dists, source->size * sizeof(*dest->dists));
}

/*
Appends the length and distance to the LZ77 arrays of the ZopfliLZ77Store.
context must be a ZopfliLZ77Store*.
*/
static void ZopfliStoreLitLenDist(unsigned short length, unsigned char dist,
                           ZopfliLZ77Store* store) {
  size_t size2 = store->size;  /* Needed for using ZOPFLI_APPEND_DATA twice. */
  ZOPFLI_APPEND_DATA(length, &store->litlens, &store->size);
  ZOPFLI_APPEND_DATA(dist, (unsigned char**)&store->dists, &size2);
}

#ifndef NDEBUG
void ZopfliVerifyLenDist(const unsigned char* data, size_t datasize, size_t pos,
                         unsigned short dist, unsigned short length) {
  assert(pos + length <= datasize);
  for (size_t i = 0; i < length; i++) {
    assert(data[pos - dist + i] == data[pos + i]);
  }
}
#endif

static unsigned symtox(unsigned lls){
  if (lls <= 279){
    return 0;
  }
  if (lls <= 283){
    return 100;
  }
  return 200;
}

/*
 LZ4 HC - High Compression Mode of LZ4
 Copyright (C) 2011-2015, Yann Collet.
 BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
 copyright notice, this list of conditions and the following disclaimer
 in the documentation and/or other materials provided with the
 distribution.
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 You can contact the author at :
 - LZ4 source repository : https://github.com/Cyan4973/lz4
 - LZ4 public forum : https://groups.google.com/forum/#!forum/lz4c
 */
#include <stdint.h>
typedef  uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;

#define DICTIONARY_LOGSIZE 15
#define DICTIONARY_LOGSIZE3 11 //better use 9 for PNG

#define MAXD (1<<DICTIONARY_LOGSIZE)
#define MAXD3 (1<<DICTIONARY_LOGSIZE3)
#define MAX_DISTANCE (MAXD - 1)
#define MAX_DISTANCE3 (MAXD3 - 1)
#define HASH_LOG (DICTIONARY_LOGSIZE + 1)
#define HASHTABLESIZE (1 << HASH_LOG)
#define HASH_LOG3 DICTIONARY_LOGSIZE3
#define HASHTABLESIZE3 (1 << HASH_LOG3)

typedef struct
{
  U32   hashTable[HASHTABLESIZE];
  U16   chainTable[MAXD];
  const BYTE* base;       /* All index relative to this position */
  U32   nextToUpdate;     /* index from which to continue dictionary update */
} LZ4HC_Data_Structure;
typedef struct
{
  U32   hashTable[HASHTABLESIZE3];
  U16   chainTable[MAXD3];
  const BYTE* base;       /* All index relative to this position */
  U32   nextToUpdate;     /* index from which to continue dictionary update */
} LZ3HC_Data_Structure;

#ifdef __SSE4_2__
#include <nmmintrin.h>
static U32 LZ4HC_hashPtr(const void* ptr) { return _mm_crc32_u32(0, *(unsigned*)ptr) >> (32-HASH_LOG); }
static U32 LZ4HC_hashPtr3(const void* ptr) { return _mm_crc32_u32(0, (*(unsigned*)ptr) & 0xFFFFFF) >> (32-HASH_LOG3); }
#else
#define HASH_FUNCTION(i)       (((i) * 2654435761U) >> (32-HASH_LOG))
#define HASH_FUNCTION3(i)       (((i) * 2654435761U) >> (32-HASH_LOG3))

static U32 LZ4HC_hashPtr(const void* ptr) { return HASH_FUNCTION(*(unsigned*)ptr); }
static U32 LZ4HC_hashPtr3(const void* ptr) { return HASH_FUNCTION3((*(unsigned*)ptr) & 0xFFFFFF); }
#endif

static void LZ4HC_init (LZ4HC_Data_Structure* hc4, const BYTE* start)
{
  memset((void*)hc4->hashTable, 0, sizeof(hc4->hashTable));
  memset(hc4->chainTable, 0xFF, sizeof(hc4->chainTable));

  hc4->nextToUpdate = MAXD;
  hc4->base = start - MAXD;
}
static void LZ4HC_init3 (LZ3HC_Data_Structure* hc4, const BYTE* start)
{
  memset((void*)hc4->hashTable, 0, sizeof(hc4->hashTable));
  memset(hc4->chainTable, 0xFF, sizeof(hc4->chainTable));

  hc4->nextToUpdate = MAXD3;
  hc4->base = start - MAXD3;
}

/* Update chains up to ip (excluded) */
static void LZ4HC_Insert (LZ4HC_Data_Structure* hc4, const BYTE* ip)
{
  U16* chainTable = hc4->chainTable;
  U32* HashTable  = hc4->hashTable;

  const BYTE* const base = hc4->base;
  const U32 target = (U32)(ip - base);
  U32 idx = hc4->nextToUpdate;

  while(idx < target)
  {
    U32 h = LZ4HC_hashPtr(base+idx);
    U32 delta = idx - HashTable[h];
    if (delta>MAX_DISTANCE) delta = MAX_DISTANCE;
    chainTable[idx & MAX_DISTANCE] = (U16)delta;
    HashTable[h] = idx;
    idx++;
  }

  hc4->nextToUpdate = target;

}
static void LZ4HC_Insert3 (LZ3HC_Data_Structure* hc4, const BYTE* ip)
{
  U16* chainTable = hc4->chainTable;
  U32* HashTable  = hc4->hashTable;

  const BYTE* const base = hc4->base;
  const U32 target = (U32)(ip - base);
  U32 idx = hc4->nextToUpdate;

  while(idx < target)
  {
    U32 h = LZ4HC_hashPtr3(base+idx);
    U32 delta = idx - HashTable[h];
    if (delta>MAX_DISTANCE3) delta = MAX_DISTANCE3;
    chainTable[idx & MAX_DISTANCE3] = (U16)delta;
    HashTable[h] = idx;
    idx++;
  }

  hc4->nextToUpdate = target;
}

static int LZ4HC_InsertAndFindBestMatch(LZ4HC_Data_Structure* hc4,   /* Index table will be updated */
                                               const BYTE* ip, const BYTE* const iLimit,
                                               const BYTE** matchpos)
{
  U16* const chainTable = hc4->chainTable;
  U32* const HashTable = hc4->hashTable;
  const BYTE* const base = hc4->base;
  const U32 lowLimit = (2 * MAXD > (U32)(ip-base)) ? MAXD : (U32)(ip - base) - (MAXD - 1);
  const BYTE* match;
  int nbAttempts = 650; //PNG 650//ENWIK 600/700
  size_t ml=3;

  /* HC4 match finder */
  LZ4HC_Insert(hc4, ip);
  U32 matchIndex = HashTable[LZ4HC_hashPtr(ip)];

  while ((matchIndex>=lowLimit) && nbAttempts)
  {
    nbAttempts--;
    match = base + matchIndex;
    if (*(unsigned*)(match+ml - 3) == *(unsigned*)(ip+ml - 3))
    {
      size_t mlt = GetMatch(ip, match, iLimit, iLimit - 8) - ip;

      if (mlt > ml) { ml = mlt; *matchpos = match;
        if (ml == ZOPFLI_MAX_MATCH){
          return ZOPFLI_MAX_MATCH;
        }
      }
    }
    matchIndex -= chainTable[matchIndex & MAX_DISTANCE];
  }

  return (int)ml;
}

static int LZ4HC_InsertAndFindBestMatch3 (LZ3HC_Data_Structure* hc4,   /* Index table will be updated */
                                         const BYTE* ip, const BYTE* const iLimit,
                                         const BYTE** matchpos)
{
  if (iLimit - ip < 3){
    return 0;
  }
  U16* const chainTable = hc4->chainTable;
  U32* const HashTable = hc4->hashTable;
  const BYTE* const base = hc4->base;
  const U32 lowLimit = (2 * MAXD3 > (U32)(ip-base)) ? MAXD3 : (U32)(ip - base) - (MAXD3 - 1);

  /* HC3 match finder */
  LZ4HC_Insert3(hc4, ip);
  U32 matchIndex = HashTable[LZ4HC_hashPtr3(ip)];
  unsigned val = (*(unsigned*)ip) & 0xFFFFFF;

  while ((matchIndex>=lowLimit))
  {
    const BYTE* match = base + matchIndex;
    if (((*(unsigned*)match) & 0xFFFFFF) == val) { *matchpos = match;  return 3;}
    matchIndex -= chainTable[matchIndex & MAX_DISTANCE3];
  }

  return 0;
}

#include "deflate.h"

size_t ZopfliLZ77LazyLauncher(const unsigned char* in,
                              size_t instart, size_t inend, unsigned fs);

size_t ZopfliLZ77LazyLauncher(const unsigned char* in,
                              size_t instart, size_t inend, unsigned fs) {
  ZopfliLZ77Store store;
  ZopfliInitLZ77Store(&store);

  ZopfliOptions options;
  ZopfliInitOptions(&options, 4, 0, 0);
  if (fs == 3){
    unsigned char x = 0;
    unsigned char* ou = 0;
    size_t back = 0;
    ZopfliDeflate(&options, 1, in + instart, inend - instart, &x, &ou, &back);
    free(ou);
    return back;
  }

  ZopfliLZ77Lazy(&options, in,
                 instart, inend,
                 &store);
  size_t ret = ZopfliCalculateBlockSize(store.litlens, store.dists, 0, store.size, 2, 0, 1);
  ZopfliCleanLZ77Store(&store);
  return ret;
}

void ZopfliLZ77Lazy(const ZopfliOptions* options, const unsigned char* in,
                      size_t instart, size_t inend,
                      ZopfliLZ77Store* store) {

  LZ4HC_Data_Structure mmc;
  LZ3HC_Data_Structure h3;
  size_t i = 0;
  unsigned short leng;
  unsigned short dist;
  unsigned lengthscore;
  size_t windowstart = instart > ZOPFLI_WINDOW_SIZE
      ? instart - ZOPFLI_WINDOW_SIZE : 0;

  unsigned prev_length = 0;
  unsigned prev_match = 0;
  unsigned char match_available = 0;

  LZ4HC_init(&mmc, &in[windowstart]);
  LZ4HC_init3(&h3, &in[instart > MAXD3 ? instart - MAXD3 : 0]);


  for (i = instart; i < inend; i++) {

    const BYTE* matchpos;
    int y = LZ4HC_InsertAndFindBestMatch(&mmc, &in[i], &in[inend] > &in[i] + ZOPFLI_MAX_MATCH ? &in[i] + ZOPFLI_MAX_MATCH : &in[inend], &matchpos);

    if (y >= 4 && i + 4 <= inend){
      dist = &in[i] - matchpos;
      leng = y;
    }
    else if (!match_available){
      y = LZ4HC_InsertAndFindBestMatch3(&h3, &in[i], &in[inend], &matchpos);
      if (y == 3){
      leng = 3;
      dist = &in[i] - matchpos;
      }
      else{
        leng = 0;
      }

    }

    lengthscore = leng;
    if (lengthscore == 3 && dist > 1024){
      --lengthscore;
    }
    if (lengthscore == 4 && dist > 2048){
      --lengthscore;
    }
    if (lengthscore == 5 && dist > 4096){
      --lengthscore;
    }

    if (match_available) {
      match_available = 0;
      if (lengthscore > prev_length + 1) {
        ZopfliStoreLitLenDist(in[i - 1], 0, store);

        if (lengthscore >= ZOPFLI_MIN_MATCH) {
          match_available = 1;
          prev_length = leng;
          prev_match = dist;
          continue;
        }
      } else {
        /* Add previous to output. */
        leng = prev_length;
        dist = prev_match;
        /* Add to output. */
#ifndef NDEBUG
        ZopfliVerifyLenDist(in, inend, i - 1, dist, leng);
#endif

        unsigned lls = ZopfliGetLengthSymbol(leng);
        ZopfliStoreLitLenDist(lls + ((leng - symtox(lls)) << 9), ZopfliGetDistSymbol(dist) + 1, store);
        i += leng - 2;
        continue;
      }
    }
    else if (lengthscore >= ZOPFLI_MIN_MATCH && leng < options->greed) {
      match_available = 1;
      prev_length = leng;
      prev_match = dist;
      continue;
    }

    /* Add to output. */
    if (lengthscore >= ZOPFLI_MIN_MATCH) {
#ifndef NDEBUG
        ZopfliVerifyLenDist(in, inend, i, dist, leng);
#endif
      unsigned lls = ZopfliGetLengthSymbol(leng);
      ZopfliStoreLitLenDist(lls + ((leng - symtox(lls)) << 9), ZopfliGetDistSymbol(dist) + 1, store);

    } else {
      leng = 1;
      ZopfliStoreLitLenDist(in[i], 0, store);
    }
    i += leng - 1;
  }
}

void ZopfliLZ77Counts(const unsigned short* litlens, const unsigned short* dists, size_t start, size_t end, size_t* ll_count, size_t* d_count, unsigned char symbols) {
  for (unsigned i = 0; i < 288; i++) {
    ll_count[i] = 0;
  }
  for (unsigned i = 0; i < 32; i++) {
    d_count[i] = 0;
  }
  ll_count[256] = 1;  /* End symbol. */

  size_t i;

  if (symbols){
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    size_t d_count1[32] = {0};
    size_t d_count2[32] = {0};
    size_t d_count3[32] = {0};

    const unsigned char* distc = (const unsigned char*)dists;

    size_t rstart = start + ((end - start) & 15);

    for (i = start; i < rstart; i++) {
      d_count[distc[i]]++;
      ll_count[litlens[i] & 511]++;
    }

#define ANDLLS (511LLU + (511LLU << 16) + (511LLU << 32) + (511LLU << 48))
    const unsigned char* ipo = &distc[rstart];
    while (ipo < distc + end)
    {
      size_t c = *(size_t*)ipo; ipo += 8;
      size_t c1 = *(size_t*)ipo; ipo += 8;
      d_count[(unsigned char) c     ]++;
      d_count1[(unsigned char)(c>>8) ]++;
      d_count2[(unsigned char)(c>>16)]++;
      d_count3[(unsigned char)(c>>24) ]++;
      d_count[(unsigned char) (c>>32)    ]++;
      d_count1[(unsigned char)(c>>40) ]++;
      d_count2[(unsigned char)(c>>48)]++;
      d_count3[       (c>>56) ]++;

      d_count[(unsigned char) c1     ]++;
      d_count1[(unsigned char)(c1>>8) ]++;
      d_count2[(unsigned char)(c1>>16)]++;
      d_count3[(unsigned char)(c1>>24) ]++;
      d_count[(unsigned char) (c1>>32)    ]++;
      d_count1[(unsigned char)(c1>>40) ]++;
      d_count2[(unsigned char)(c1>>48)]++;
      d_count3[       (c1>>56) ]++;
    }

    for (i = 0; i < 32; i++){
      d_count[i] += d_count1[i] + d_count2[i] + d_count3[i];
    }
    for (i = 0; i < 31; i++) {
      d_count[i] = d_count[i + 1];
    }

    size_t ll_count1[288] = {0};
    size_t ll_count2[288] = {0};
    size_t ll_count3[288] = {0};

    const unsigned short* ip = &litlens[rstart];
    while (ip < litlens + end)
    {
      size_t c = (*(size_t*)ip) & ANDLLS; ip += 4;
      size_t c1 = (*(size_t*)ip) & ANDLLS; ip += 4;

      ll_count[(unsigned short) c     ]++;
      ll_count1[(unsigned short)(c>>16) ]++;
      ll_count2[(unsigned short)(c>>32)]++;
      ll_count3[       c>>48 ]++;

      ll_count[(unsigned short) c1     ]++;
      ll_count1[(unsigned short)(c1>>16) ]++;
      ll_count2[(unsigned short)(c1>>32)]++;
      ll_count3[       c1>>48 ]++;

      c = (*(size_t*)ip) & ANDLLS; ip += 4;
      c1 = (*(size_t*)ip) & ANDLLS; ip += 4;

      ll_count[(unsigned short) c     ]++;
      ll_count1[(unsigned short)(c>>16) ]++;
      ll_count2[(unsigned short)(c>>32)]++;
      ll_count3[       c>>48 ]++;

      ll_count[(unsigned short) c1     ]++;
      ll_count1[(unsigned short)(c1>>16) ]++;
      ll_count2[(unsigned short)(c1>>32)]++;
      ll_count3[       c1>>48 ]++;
    }

    for (i = 0; i < 288; i++){
      ll_count[i] += ll_count1[i] + ll_count2[i] + ll_count3[i];
    }
#else

    const unsigned char* distc = (const unsigned char*)dists;
    for (i = start; i < end; i++) {
      d_count[distc[i]]++;
    }

    for (i = 0; i < 31; i++) {
      d_count[i] = d_count[i + 1];
    }

    for (i = start; i < end; i++) {
      ll_count[litlens[i] & 511]++;
    }
#endif

    return;
  }

  size_t lenarrything[515] = {0};
  size_t lenarrything2[515] = {0};
  size_t d_count2[64] = {0};

  size_t* dc = d_count2 + 1;
  size_t* dc2 = d_count2 + 32;

  if ((end - start) % 2){
    lenarrything[litlens[start] + !dists[start] * 259]++;
    dc[ZopfliGetDistSymbol(dists[start])]++;
    start++;
  }
  for (i = start; i < end; i++) {
    lenarrything[litlens[i] + !dists[i] * 259]++;
    dc[ZopfliGetDistSymbol(dists[i])]++;
    i++;
    lenarrything2[litlens[i] + !dists[i] * 259]++;
    dc2[ZopfliGetDistSymbol(dists[i])]++;
  }

  for (i = 0; i < 256; i++){
    ll_count[i] = lenarrything[i + 259] + lenarrything2[i + 259];
  }

  for (i = 3; i < 259; i++){
    ll_count[ZopfliGetLengthSymbol(i)] += lenarrything[i] + lenarrything2[i];
  }
  for (i = 0; i < 30; i++){
    d_count[i] = dc[i] + dc2[i];
  }
}
