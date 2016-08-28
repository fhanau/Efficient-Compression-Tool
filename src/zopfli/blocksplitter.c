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

#include "blocksplitter.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "deflate.h"
#include "lz77.h"
#include "util.h"

typedef struct SplitCostContext {
  const unsigned short* litlens;
  const unsigned short* dists;
  size_t start;
  size_t end;
  unsigned char symbols;
} SplitCostContext;

/*
 Gets the cost which is the sum of the cost of the left and the right section
 of the data.
 */
static double SplitCost(size_t i, SplitCostContext* c, unsigned char searchext, unsigned entropysplit, const size_t* ll_count, const size_t* d_count, const size_t* ll_count2, const size_t* d_count2, size_t pos2) {
  double result = 3;
  unsigned ll_lengths[288];
  unsigned d_lengths[32];
  unsigned dummy;
  if(i == c->end){
    result += entropysplit ? GetDynamicLengths2(ll_lengths, d_lengths, ll_count, d_count) : GetDynamicLengthsuse(ll_lengths, d_lengths, ll_count, d_count);
    result += CalculateTreeSize(ll_lengths, d_lengths, searchext, &dummy);
    return result;
  }
  size_t ll_counts[288];
  size_t d_counts[32];
  unsigned x = i - c->start < c->end - i;
  unsigned dist = x ? i - c->start : c->end - i;
  unsigned dist2 = i > pos2 ? i - pos2 : pos2 - i;
  if(dist2 < dist && dist2){
    x = i > pos2;
    if(x){
      ZopfliLZ77Counts(c->litlens, c->dists, pos2, i, ll_counts, d_counts, c->symbols);
      for(size_t ix = 0; ix < 286; ix++){
        ll_counts[ix] = ll_count2[ix] + ll_counts[ix];
      }
      for(size_t ix = 0; ix < 30; ix++){
        d_counts[ix] = d_count2[ix] + d_counts[ix];
      }
    }
    else{
      ZopfliLZ77Counts(c->litlens, c->dists, i, pos2, ll_counts, d_counts, c->symbols);
      for(size_t ix = 0; ix < 286; ix++){
        ll_counts[ix] = ll_count2[ix] - ll_counts[ix];
      }
      for(size_t ix = 0; ix < 30; ix++){
        d_counts[ix] = d_count2[ix] - d_counts[ix];
      }
    }
  }
  else{
    ZopfliLZ77Counts(c->litlens, c->dists, x ? c->start : i, x ? i : c->end, ll_counts, d_counts, c->symbols);
  }
  ll_counts[256] = 1;

  result += entropysplit ? GetDynamicLengths2(ll_lengths, d_lengths, ll_counts, d_counts) : GetDynamicLengthsuse(ll_lengths, d_lengths, ll_counts, d_counts);
  result += CalculateTreeSize(ll_lengths, d_lengths, searchext, &dummy);
  result += 3;
  for(size_t ix = 0; ix < 286; ix++){
    ll_counts[ix] = ll_count[ix] - ll_counts[ix];
  }
  for(size_t ix = 0; ix < 30; ix++){
    d_counts[ix] = d_count[ix] - d_counts[ix];
  }
  ll_counts[256] = 1;
  result += entropysplit ? GetDynamicLengths2(ll_lengths, d_lengths, ll_counts, d_counts) : GetDynamicLengthsuse(ll_lengths, d_lengths, ll_counts, d_counts);
  result += CalculateTreeSize(ll_lengths, d_lengths, searchext, &dummy);
  return result;
}

/*
Finds minimum of function f(i) where is is of type size_t, f(i) is of type
double, i is in range start-end (excluding end).
*/
static size_t FindMinimum(SplitCostContext* context, size_t start, size_t end, unsigned char* enough, const ZopfliOptions* options) {
  //Count LZ77 symbols once, then, on later runs, just for 1st potential block and substract
  size_t ll_count[288];
  size_t d_count[32];
  size_t ll_count2[288];
  size_t d_count2[32];
  ZopfliLZ77Counts(context->litlens, context->dists, context->start, context->end, ll_count, d_count, context->symbols);
  size_t pos2 = context->end - (context->end - context->start) / 2;
  ZopfliLZ77Counts(context->litlens, context->dists, context->start, pos2, ll_count2, d_count2, context->symbols);


  size_t startsize = end - start;
  /* Try to find minimum by recursively checking multiple points. */
#define NUM 9  /* Good value: 9. */
  size_t i;
  size_t p[NUM];
  double vp[NUM];
  double prevstore = -1;
  size_t besti;
  double best;
  double lastbest = ZOPFLI_LARGE_FLOAT;
  size_t pos = start;
  size_t ostart = start;
  for (;;) {
    if (end - start <= options->num){
      if (options->numiterations > 50){
        for (unsigned j = 0; j < end - start; j++){
          double cost = SplitCost(start + j, context, options->searchext & 2, options->entropysplit, ll_count, d_count, ll_count2, d_count2, pos2);
          if (cost < best){
            best = cost;
            pos = start + j;
          }
        }
      }
      break;
    }
    if (end - start <= startsize/100 && startsize > 600 && options->num == 3) break;

    for (i = 0; i < options->num; i++) {
      p[i] = start + (i + 1) * ((end - start) / (options->num + 1));
      if (pos == p[i] || (i == (options->num - 1) / 2 && prevstore != -1 && options->num == 3)){
        vp[i] = best;
        continue;
      }
      vp[i] = SplitCost(p[i], context, options->searchext & 2, options->entropysplit, ll_count, d_count, ll_count2, d_count2, pos2);
    }
    besti = 0;
    best = vp[0];
    prevstore = best;
    for (i = 1; i < options->num; i++) {
      if (vp[i] < best) {
        best = vp[i];
        besti = i;
      }
    }
    if (best > lastbest) break;

    start = besti == 0 ? start : p[besti - 1];
    end = besti == options->num - 1 ? end : p[besti + 1];

    pos = p[besti];
    lastbest = best;
  }
  double origcost = SplitCost(context->end, context, options->searchext & 2, options->entropysplit, ll_count, d_count, ll_count2, d_count2, pos2);
  if(origcost <= best){
    pos = ostart;
  }
  else if (options->entropysplit && best + 200 >= origcost){
    *enough = 1;
  }
  return pos;
#undef NUM
}

static void AddSorted(size_t value, size_t** out, size_t* outsize) {
  size_t i;
  ZOPFLI_APPEND_DATA(value, out, outsize);
  for (i = 0; i + 1 < *outsize; i++) {
    if ((*out)[i] > value) {
      size_t j;
      for (j = *outsize - 1; j > i; j--) {
        (*out)[j] = (*out)[j - 1];
      }
      (*out)[i] = value;
      break;
    }
  }
}

/*
Finds next block to try to split, the largest of the available ones.
The largest is chosen to make sure that if only a limited amount of blocks is
requested, their sizes are spread evenly.
llsize: the size of the LL77 data, which is the size of the done array here.
done: array indicating which blocks starting at that position are no longer
    splittable (splitting them increases rather than decreases cost).
splitpoints: the splitpoints found so far.
npoints: the amount of splitpoints found so far.
lstart: output variable, giving start of block.
lend: output variable, giving end of block.
returns 1 if a block was found, 0 if no block found (all are done).
*/
static int FindLargestSplittableBlock(
    size_t llsize, const unsigned char* done,
    const size_t* splitpoints, size_t npoints,
    size_t* lstart, size_t* lend) {
  size_t longest = 0;
  int found = 0;
  size_t i;
  for (i = 0; i <= npoints; i++) {
    size_t start = i == 0 ? 0 : splitpoints[i - 1];
    size_t end = i == npoints ? llsize - 1 : splitpoints[i];
    if (!done[start] && end - start > longest) {
      found = 1;
      *lstart = start;
      *lend = end;
      longest = end - start;
    }
  }
  return found;
}

static void ZopfliBlockSplitLZ77(const unsigned short* litlens,
                          const unsigned short* dists,
                          size_t llsize, size_t** splitpoints,
                          size_t* npoints, const ZopfliOptions* options, unsigned char symbols) {
  if (llsize < options->noblocksplitlz) return;  /* This code fails on tiny files. */

  size_t llpos;
  int splittingleft = 0;
  unsigned char* done = (unsigned char*)calloc(llsize, 1);
  if (!done) exit(1); /* Allocation failed. */
  size_t lstart = 0;
  size_t lend = llsize;
  for (;;) {
    SplitCostContext c;

    c.litlens = litlens;
    c.dists = dists;
    c.start = lstart;
    c.end = lend;
    c.symbols = symbols;
    assert(lstart < lend);
    unsigned char enough = 0;
    llpos = FindMinimum(&c, lstart + 1, lend, &enough, options);
    assert(llpos > lstart || !llpos);
    assert(llpos < lend);

    if (llpos == lstart + 1 || llpos == lend) {
      done[lstart] = 1;
    } else {
      AddSorted(llpos, splitpoints, npoints);
      if(enough){
        done[llpos] = 1;
      }
    }

    splittingleft = FindLargestSplittableBlock(llsize, done, *splitpoints, *npoints, &lstart, &lend);
    if (!splittingleft) {
      break;  /* No further split will probably reduce compression. */
    }

    if (lend - lstart < options->noblocksplitlz) {
      break;
    }
  }

  free(done);
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

void ZopfliBlockSplit(const ZopfliOptions* options,
                      const unsigned char* in, size_t instart, size_t inend,
                      size_t** splitpoints, size_t* npoints, SymbolStats** stats, unsigned char twiceMode, ZopfliLZ77Store twiceStore) {
  size_t pos = 0;
  size_t i;
  size_t* lz77splitpoints = 0;
  size_t nlz77points = 0;
  size_t prevpoints = *npoints;
  ZopfliLZ77Store store;

  /* Unintuitively, Using a simple LZ77 method here instead of ZopfliLZ77Optimal
  results in better blocks. */
  ZopfliInitLZ77Store(&store);
  if (!(twiceMode & 2)){
    ZopfliLZ77Lazy(options, in, instart, inend, &store);
    store.symbols = 1;
  }
  else{
    store.size = twiceStore.size;
    store.litlens = twiceStore.litlens;
    store.dists = twiceStore.dists;
    store.symbols = twiceStore.symbols;
  }

  /* Blocksplitting likely wont improve compression on small files */
  if (inend - instart < options->noblocksplit){
    *stats = (SymbolStats*)malloc(sizeof(SymbolStats));
    GetStatistics(&store, *stats);
    ZopfliCleanLZ77Store(&store);
    return;
  }

  ZopfliBlockSplitLZ77(store.litlens, store.dists, store.size, &lz77splitpoints, &nlz77points, options, store.symbols);

  *stats = (SymbolStats*)realloc(*stats, (nlz77points + prevpoints + 1) * sizeof(SymbolStats));
  if (!(*stats)){
    exit(1);
  }

  /* Convert LZ77 positions to positions in the uncompressed input. */
  pos = instart;
  if (nlz77points) {
    for (i = 0; i < store.size; i++) {
      size_t length = store.symbols ? store.litlens[i] < 256 ? 1 : symtox(store.litlens[i] & 511) + (store.litlens[i] >> 9) : store.dists[i] == 0 ? 1 : store.litlens[i];
      if (lz77splitpoints[(*npoints) - prevpoints] == i) {
        size_t temp = store.size;
        size_t shift = (*npoints) - prevpoints ? lz77splitpoints[*npoints - prevpoints - 1] : 0;
        store.size = i - shift;
        if (store.symbols){
          store.dists = (unsigned short*)(((unsigned char*)(store.dists)) + shift);
        }
        else{
          store.dists += shift;
        }
        store.litlens += shift;
        GetStatistics(&store, &((*stats)[*npoints]));
        store.size = temp;
        if (store.symbols){
          store.dists = (unsigned short*)(((unsigned char*)(store.dists)) - shift);
        }
        else{
          store.dists -= shift;
        }
        store.litlens -= shift;
        ZOPFLI_APPEND_DATA(pos, splitpoints, npoints);
        if (*npoints - prevpoints == nlz77points) break;
      }
      pos += length;
    }
  }
  assert(*npoints - prevpoints == nlz77points);

  size_t shift = *npoints - prevpoints ? lz77splitpoints[*npoints - prevpoints - 1] : 0;
  store.size -= shift;
  if (store.symbols){
    store.dists = (unsigned short*)(((unsigned char*)(store.dists)) + shift);
  }
  else{
    store.dists += shift;
  }
  store.litlens += shift;

  GetStatistics(&store, &((*stats)[*npoints]));
  store.size += shift;
  if (store.symbols){
    store.dists = (unsigned short*)(((unsigned char*)(store.dists)) - shift);
  }
  else{
    store.dists -= shift;
  }
  store.litlens -= shift;

  free(lz77splitpoints);
  ZopfliCleanLZ77Store(&store);
}
