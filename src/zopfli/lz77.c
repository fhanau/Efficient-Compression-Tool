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
#include "disttable.h"
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
static void ZopfliStoreLitLenDist(unsigned short length, unsigned short dist,
                           ZopfliLZ77Store* store) {
  size_t size2 = store->size;  /* Needed for using ZOPFLI_APPEND_DATA twice. */
  ZOPFLI_APPEND_DATA(length, &store->litlens, &store->size);
  ZOPFLI_APPEND_DATA(dist, &store->dists, &size2);
}

#ifndef NDEBUG
void ZopfliVerifyLenDist(const unsigned char* data, size_t datasize, size_t pos,
                         unsigned short dist, unsigned short length) {
  assert(pos + length <= datasize);
  for (size_t i = 0; i < length; i++) {
    if (data[pos - dist + i] != data[pos + i]) {
      assert(data[pos - dist + i] == data[pos + i]);
      break;
    }
  }
}
#endif

static void ZopfliFindLongestMatch(const ZopfliOptions* options, const ZopfliHash* h,
                            const unsigned char* array,
                            size_t pos, size_t size,
                            unsigned short* distance, unsigned short* length) {
  if (size - pos < ZOPFLI_MIN_MATCH) {
    /* The rest of the code assumes there are at least ZOPFLI_MIN_MATCH bytes to
     try. */
    *length = 0;
    *distance = 0;
    return;
  }
  unsigned short chain_counter = options->chain_length;   /*For quitting early. */

  unsigned limit = ZOPFLI_MAX_MATCH;
  if (pos + limit > size) {
    limit = size - pos;
  }
  const unsigned char* arrayend = &array[pos] + limit;
  const unsigned char* arrayend_safe = arrayend - 8;
  unsigned short hpos = pos & ZOPFLI_WINDOW_MASK, p, pp;
  unsigned short* hprev = h->prev;
  unsigned char hhead = 0;
  pp = hpos;  /* During the whole loop, p == hprev[pp]. */
  p = hprev[pp];

  unsigned dist = p < pp ? pp - p : ((ZOPFLI_WINDOW_SIZE - p) + pp); /* Not unsigned short on purpose. */

  unsigned short bestlength = 2;
  unsigned short bestdist = 0;
  const unsigned char* scan;
  const unsigned char* match;
  const unsigned char* new = &array[pos];
  unsigned short same0 = h->same[hpos];
  if (same0 > limit) same0 = limit;
  /* Go through all distances. */
  while (dist < ZOPFLI_WINDOW_SIZE) {
    scan = new;
    match = new - dist;

    /* Testing the byte at position bestlength first, goes slightly faster. */
    if (unlikely(*(unsigned short*)(scan + bestlength - 1) == *(unsigned short*)(match + bestlength - 1))) {
#ifdef ZOPFLI_HASH_SAME
      if (same0 > 2) {
        unsigned short same1 = h->same[(pos - dist) & ZOPFLI_WINDOW_MASK];
        unsigned short same = same0 < same1 ? same0 : same1;
        scan += same;
        match += same;
      }
#endif
      scan = GetMatch(scan, match, arrayend, arrayend_safe);
      unsigned short currentlength = scan - new;  /* The found length. */
      if (currentlength > bestlength) {
        bestdist = dist;
        bestlength = currentlength;
        if (currentlength >= limit) break;
      }
    }

#ifdef ZOPFLI_HASH_SAME_HASH
    /* Switch to the other hash once this will be more efficient. */
    if (!hhead && bestlength >= h->same[hpos] &&
        h->val2 == h->hashval2[p]) {
      /* Now use the hash that encodes the length and first byte. */
      hhead = 1;
      hprev = h->prev2;
    }
#endif

    pp = p;
    p = hprev[p];

    dist += likely(p < pp) ? pp - p : ((ZOPFLI_WINDOW_SIZE - p) + pp);
    chain_counter--;
    if (!chain_counter) break;
  }

  *distance = bestdist;
  *length = bestlength;
}


static unsigned symtox(unsigned lls){
  if (lls <= 279){
    return 0;
  }
  else if (lls <= 283){
    return 100;
  }
  else{
    return 200;
  }
}

void ZopfliLZ77Greedy(const ZopfliOptions* options, const unsigned char* in,
                      size_t instart, size_t inend,
                      ZopfliLZ77Store* store) {
  size_t i = 0, j;
  unsigned short leng;
  unsigned short dist;
  unsigned lengthscore;
  size_t windowstart = instart > ZOPFLI_WINDOW_SIZE
      ? instart - ZOPFLI_WINDOW_SIZE : 0;

  ZopfliHash hash;
  ZopfliHash* h = &hash;

  unsigned prev_length = 0;
  unsigned prev_match = 0;
  unsigned prevlengthscore;
  unsigned char match_available = 0;

  ZopfliInitHash(h);
  ZopfliWarmupHash(in, windowstart, inend, h);
  LoopedUpdateHash(in, windowstart, inend, h, instart - windowstart);

  for (i = instart; i < inend; i++) {
    ZopfliUpdateHash(in, i, inend, h);

    ZopfliFindLongestMatch(options, h, in, i, inend, &dist, &leng);

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

    /* Lazy matching. */

    prevlengthscore = prev_length;
    if (prevlengthscore == 3 && prev_match > 8192){
      --prevlengthscore;
    }

    if (match_available) {
      match_available = 0;
      if (lengthscore > prevlengthscore + 1) {
        ZopfliStoreLitLenDist(in[i - 1], 0, store);
        if (lengthscore >= ZOPFLI_MIN_MATCH && leng < ZOPFLI_MAX_MATCH) {
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
        ZopfliStoreLitLenDist(lls + ((leng - symtox(lls)) << 9), disttable[dist] + 1, store);

        for (j = 2; j < leng; j++) {
          i++;
          ZopfliUpdateHash(in, i, inend, h);
        }
        continue;
      }
    }
    else if (lengthscore >= ZOPFLI_MIN_MATCH && leng < ZOPFLI_MAX_MATCH) {
      match_available = 1;
      prev_length = leng;
      prev_match = dist;
      continue;
    }
    /* End of lazy matching. */

    /* Add to output. */
    if (lengthscore >= ZOPFLI_MIN_MATCH) {
#ifndef NDEBUG
        ZopfliVerifyLenDist(in, inend, i, dist, leng);
#endif
      unsigned lls = ZopfliGetLengthSymbol(leng);
      ZopfliStoreLitLenDist(lls + ((leng - symtox(lls)) << 9), disttable[dist] + 1, store);

    } else {
      leng = 1;
      ZopfliStoreLitLenDist(in[i], 0, store);
    }
    for (j = 1; j < leng; j++) {
      assert(i < inend);
      i++;
      ZopfliUpdateHash(in, i, inend, h);
    }
  }

  ZopfliCleanHash(h);
}

void ZopfliLZ77Counts(const unsigned short* litlens, const unsigned short* dists, size_t start, size_t end, size_t* ll_count, size_t* d_count, unsigned char symbols) {
  for (unsigned i = 0; i < 288; i++) {
    ll_count[i] = 0;
  }
  for (unsigned i = 0; i < 32; i++) {
    d_count[i] = 0;
  }
  size_t i;


  if (symbols){

#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    size_t d_count1[32] = {0};
    size_t d_count2[32] = {0};
    size_t d_count3[32] = {0};

    size_t rstart = start + ((end - start) & 15);
    for (i = start; i < rstart; i++) {
      d_count[dists[i]]++;
      ll_count[litlens[i] & 511]++;
    }

#define ANDLLS 511LU + (511LU << 16) + (511LU << 32) + (511LU << 48)
    const unsigned short* ip = &dists[rstart];
    size_t cached = *(size_t*)ip;ip += 4;
    while (ip < dists + end)
    {
      size_t c = cached; cached = *(size_t*)ip; ip += 4;
      d_count[(unsigned short) c     ]++;
      d_count1[(unsigned short)(c>>16) ]++;
      d_count2[(unsigned short)(c>>32)]++;
      d_count3[       c>>48 ]++;
      c = cached; cached = *(size_t*)ip; ip += 4;
      d_count[(unsigned short) c     ]++;
      d_count1[(unsigned short)(c>>16) ]++;
      d_count2[(unsigned short)(c>>32)]++;
      d_count3[       c>>48 ]++;
      c = cached; cached = *(size_t*)ip; ip += 4;
      d_count[(unsigned short) c     ]++;
      d_count1[(unsigned short)(c>>16) ]++;
      d_count2[(unsigned short)(c>>32)]++;
      d_count3[       c>>48 ]++;
      c = cached; cached = *(size_t*)ip; ip += 4;
      d_count[(unsigned short) c     ]++;
      d_count1[(unsigned short)(c>>16) ]++;
      d_count2[(unsigned short)(c>>32)]++;
      d_count3[       c>>48 ]++;
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

    ip = &litlens[rstart];
    cached = (*(size_t*)ip) & ANDLLS;ip += 4;
    while (ip < litlens + end)
    {
      size_t c = cached; cached = (*(size_t*)ip) & ANDLLS; ip += 4;
      ll_count[(unsigned short) c     ]++;
      ll_count1[(unsigned short)(c>>16) ]++;
      ll_count2[(unsigned short)(c>>32)]++;
      ll_count3[       c>>48 ]++;
      c = cached; cached = (*(size_t*)ip) & ANDLLS; ip += 4;
      ll_count[(unsigned short) c     ]++;
      ll_count1[(unsigned short)(c>>16) ]++;
      ll_count2[(unsigned short)(c>>32)]++;
      ll_count3[       c>>48 ]++;
      c = cached; cached = (*(size_t*)ip) & ANDLLS; ip += 4;
      ll_count[(unsigned short) c     ]++;
      ll_count1[(unsigned short)(c>>16) ]++;
      ll_count2[(unsigned short)(c>>32)]++;
      ll_count3[       c>>48 ]++;
      c = cached; cached = (*(size_t*)ip) & ANDLLS; ip += 4;
      ll_count[(unsigned short) c     ]++;
      ll_count1[(unsigned short)(c>>16) ]++;
      ll_count2[(unsigned short)(c>>32)]++;
      ll_count3[       c>>48 ]++;
    }

    for (i = 0; i < 288; i++){
      ll_count[i] += ll_count1[i] + ll_count2[i] + ll_count3[i];
    }
#else

    for (i = start; i < end; i++) {
      d_count[dists[i]]++;
    }

    for (i = 0; i < 31; i++) {
      d_count[i] = d_count[i + 1];
    }

    for (i = start; i < end; i++) {
      ll_count[litlens[i] & 511]++;
    }
#endif

    ll_count[256] = 1;  /* End symbol. */

    return;
  }

  size_t lenarrything[515] = {0};
  size_t lenarrything2[515] = {0};
  size_t d_count2[32] = {0};

  if ((end - start) % 2){
    lenarrything[litlens[start] + !dists[start] * 259]++;
    d_count[disttable[dists[start]]]++;
    start++;
  }
  for (i = start; i < end; i++) {
    lenarrything[litlens[i] + !dists[i] * 259]++;
    d_count[disttable[dists[i]]]++;
    i++;
    lenarrything2[litlens[i] + !dists[i] * 259]++;
    d_count2[disttable[dists[i]]]++;
  }

  for (i = 0; i < 256; i++){
    ll_count[i] = lenarrything[i + 259] + lenarrything2[i + 259];
  }

  for (i = 3; i < 259; i++){
    ll_count[ZopfliGetLengthSymbol(i)] += lenarrything[i] + lenarrything2[i];
  }
  for (i = 0; i < 32; i++){
    d_count[i] += d_count2[i];
  }

  d_count[30] = 0;
  ll_count[256] = 1;  /* End symbol. */
}
