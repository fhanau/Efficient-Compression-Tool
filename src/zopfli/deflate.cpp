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

#include "deflate.h"
#include "util.h"
#include "blocksplitter.h"
#include "lz77.h"
#include "squeeze.h"
#include "katajainen.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#ifndef NOMULTI
#include <thread>
#include <vector>
#include <mutex>
#endif

/*
bp = bitpointer, always in range [0, 7].
The outsize is number of necessary bytes to encode the bits.
Given the value of bp and the amount of bytes, the amount of bits represented
is not simply bytesize * 8 + bp because even representing one bit requires a
whole byte. It is: (bp == 0) ? (bytesize * 8) : ((bytesize - 1) * 8 + bp)
*/
static void AddBit(int bit,
                   unsigned char* bp, unsigned char** out, size_t* outsize) {
  if (*bp == 0) {(*outsize)++;}
  (*out)[*outsize - 1] |= bit << *bp;
  *bp = (*bp + 1) & 7;
}

static void AddBits(unsigned symbol, unsigned length,
                    unsigned char* bp, unsigned char* out, size_t* outsize) {
//Needs:
  //1. 32-bit arch
  //2. 32-bit unaligned read/write
  //3. Little endian architecture
//As this code is not that much faster it should be ok to only enable it on x86 with MSVC, GCC and clang
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
  //Length must be nonzero.
    *(unsigned*)(&(out[*outsize - (!!(*bp))])) |= (symbol << (*bp));
    (*outsize) += ((*bp) + length - 1) / 8 + (!(*bp));
    *bp = (((*bp) + length) & 7);
}

//Like AddBits, but uses "bits" for posititon
static void AddBits2(unsigned symbol, unsigned length,
                     unsigned char* out, size_t* bits) {
  //Length may be zero.
  unsigned pos = (*bits) >> 3;
  unsigned char bitties = ((*bits) & 7);
  *(unsigned*)(&(out[pos])) |= (symbol << bitties);
  (*bits) += length;

#else
  for (unsigned i = 0; i < length; i++) {
    unsigned bit = (symbol >> i) & 1;
    if (*bp == 0) {(*outsize)++;}
    out[*outsize - 1] |= bit << *bp;
    *bp = (*bp + 1) & 7;
  }
#endif
}

/*
Adds bits, like AddBits, but the order is inverted. The deflate specification
uses both orders in one standard.
*/
static void AddHuffmanBits(unsigned symbol, unsigned length,
                           unsigned char* bp, unsigned char* out,
                           size_t* outsize) {
  for (unsigned i = 0; i < length; i++) {
    unsigned bit = (symbol >> (length - i - 1)) & 1;
    if (*bp == 0) {(*outsize)++;}
    out[*outsize - 1] |= bit << *bp;
    *bp = (*bp + 1) & 7;
  }
}

/*
Ensures there are at least 2 distance codes to support buggy decoders.
Zlib 1.2.1 and below have a bug where it fails if there isn't at least 1
distance code (with length > 0), even though it's valid according to the
deflate spec to have 0 distance codes. On top of that, some mobile phones
require at least two distance codes. To support these decoders too (but
potentially at the cost of a few bytes), add dummy code lengths of 1.
References to this bug can be found in the changelog of
Zlib 1.2.2 and here: http://www.jonof.id.au/forum/index.php?topic=515.0.
Note that this change has a minimal detrimental effect on the compression
ratio, amounting to 6 bytes across a 6MB PNG test set of 600 files.

d_lengths: the 32 lengths of the distance codes.
*/
static void PatchDistanceCodesForBuggyDecoders(unsigned* d_lengths) {
  int num_dist_codes = 0; /* Amount of non-zero distance codes */
  for (int i = 0; i < 30 /* Ignore the two unused codes from the spec */; i++) {
    if (d_lengths[i]) num_dist_codes++;
    if (num_dist_codes >= 2) return; /* Two or more codes is fine. */
  }

  if (num_dist_codes == 0) {
    d_lengths[0] = d_lengths[1] = 1;
  } else if (num_dist_codes == 1) {
    d_lengths[d_lengths[0] ? 1 : 0] = 1;
  }
}

/*
 Converts a series of Huffman tree bitlengths to the bit values of the symbols.
 */
static void ZopfliLengthsToSymbols(const unsigned* lengths, size_t n, unsigned maxbits,
                            unsigned* symbols) {
  unsigned* bl_count = (unsigned*)calloc(maxbits + 1, sizeof(unsigned));
  unsigned* next_code = (unsigned*)malloc(sizeof(unsigned) * (maxbits + 1));
  if (!bl_count || !next_code){
    exit(1);
  }
  unsigned i;

  /* 1) Count the number of codes for each code length. Let bl_count[N] be the
   number of codes of length N, N >= 1. */
  for (i = 0; i < n; i++) {
    assert(lengths[i] <= maxbits);
    bl_count[lengths[i]]++;
  }
  /* 2) Find the numerical value of the smallest code for each code length. */
  unsigned code = 0;
  bl_count[0] = 0;
  for (unsigned bits = 1; bits <= maxbits; bits++) {
    code = (code + bl_count[bits-1]) << 1;
    next_code[bits] = code;
  }
  /* 3) Assign numerical values to all codes, using consecutive values for all
   codes of the same length with the base values determined at step 2. */
  for (i = 0;  i < n; i++) {
    unsigned len = lengths[i];
    if (len) {
      symbols[i] = next_code[len];
      next_code[len]++;
    }
  }

  free(bl_count);
  free(next_code);
}

/*
Encodes the Huffman tree and returns how many bits its encoding takes. If out
is a null pointer, only returns the size and runs faster.
*/
static size_t EncodeTree(const unsigned* ll_lengths,
                         const unsigned* d_lengths,
                         int use_16, int use_17, int use_18, int fuse_8, int fuse_7,
                         unsigned char* bp,
                         unsigned char* out, size_t* outsize) {
  /* Runlength encoded version of lengths of litlen and dist trees. */
  unsigned* rle = 0;
  unsigned* rle_bits = 0;  /* Extra bits for rle values 16, 17 and 18. */
  size_t rle_size = 0;  /* Size of rle array. */
  size_t rle_bits_size = 0;  /* Should have same value as rle_size. */
  unsigned hlit = 29;  /* 286 - 257 */
  unsigned hdist = 29;  /* 32 - 1, but gzip does not like hdist > 29.*/
  size_t i, j;
  size_t clcounts[19] = {0};
  unsigned clcl[19];  /* Code length code lengths. */
  /* The order in which code length code lengths are encoded as per deflate. */
  static const unsigned order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
  };
  int size_only = !out;

  /* Trim zeros. */
  while (hlit && ll_lengths[257 + hlit - 1] == 0) hlit--;
  while (hdist && d_lengths[hdist] == 0) hdist--;
  unsigned hlit2 = hlit + 257;

  unsigned lld_total = hlit2 + hdist + 1; /* Total amount of literal, length, distance codes. */

  for (i = 0; i < lld_total; i++) {
    /* This is an encoding of a huffman tree, so now the length is a symbol */
    unsigned char symbol = i < hlit2 ? ll_lengths[i] : d_lengths[i - hlit2];
    unsigned count = 1;
    if(use_16 || (symbol == 0 && (use_17 || use_18))) {
      for (j = i + 1; j < lld_total && symbol ==
          (j < hlit2 ? ll_lengths[j] : d_lengths[j - hlit2]); j++) {
        count++;
      }
      i += count - 1;
    }

    /* Repetitions of zeroes */
    if (symbol == 0 && count >= 3) {
      if (use_18) {
        while (count >= 11) {
          unsigned count2 = count > 138 ? 138 : count;
          if (!size_only) {
            ZOPFLI_APPEND_DATA(18, &rle, &rle_size);
            ZOPFLI_APPEND_DATA(count2 - 11, &rle_bits, &rle_bits_size);
          }
          clcounts[18]++;
          count -= count2;
        }
      }
      if (use_17) {
        while (count >= 3) {
          unsigned count2 = count > 10 ? 10 : count;
          if (!size_only) {
            ZOPFLI_APPEND_DATA(17, &rle, &rle_size);
            ZOPFLI_APPEND_DATA(count2 - 3, &rle_bits, &rle_bits_size);
          }
          clcounts[17]++;
          count -= count2;
        }
      }
    }

    /* Repetitions of any symbol */
    if (use_16 && count >= 4) {
      count--;  /* Since the first one is hardcoded. */
      clcounts[symbol]++;
      if (!size_only) {
        ZOPFLI_APPEND_DATA(symbol, &rle, &rle_size);
        ZOPFLI_APPEND_DATA(0, &rle_bits, &rle_bits_size);
      }
      while (count >= 3) {
				if (fuse_8 && count == 8) { /* record 8 as 4+4 not as 6+single+single */
					if (!size_only) {
						ZOPFLI_APPEND_DATA(16, &rle, &rle_size);
						ZOPFLI_APPEND_DATA(1, &rle_bits, &rle_bits_size);
						ZOPFLI_APPEND_DATA(16, &rle, &rle_size);
						ZOPFLI_APPEND_DATA(1, &rle_bits, &rle_bits_size);
					}
					clcounts[16] += 2;
					count = 0;
				} else if (fuse_7 && count == 7) { /* record 7 as 4+3 not as 6+single */
					if (!size_only) {
						ZOPFLI_APPEND_DATA(16, &rle, &rle_size);
						ZOPFLI_APPEND_DATA(1, &rle_bits, &rle_bits_size);
						ZOPFLI_APPEND_DATA(16, &rle, &rle_size);
						ZOPFLI_APPEND_DATA(0, &rle_bits, &rle_bits_size);
					}
					clcounts[16] += 2;
					count = 0;
				} else {
					unsigned count2 = count > 6 ? 6 : count;
					if (!size_only) {
						ZOPFLI_APPEND_DATA(16, &rle, &rle_size);
						ZOPFLI_APPEND_DATA(count2 - 3, &rle_bits, &rle_bits_size);
					}
					clcounts[16]++;
					count -= count2;
				}
      }
    }

    /* No or insufficient repetition */
    clcounts[symbol] += count;
    if (!size_only) {
      while (count) {
        ZOPFLI_APPEND_DATA(symbol, &rle, &rle_size);
        ZOPFLI_APPEND_DATA(0, &rle_bits, &rle_bits_size);
        count--;
      }
    }
  }
  ZopfliLengthLimitedCodeLengths(clcounts, 19, 7, clcl);

  unsigned hclen = 15;
  /* Trim zeros. */
  while (hclen && clcounts[order[hclen + 4 - 1]] == 0) hclen--;

  //It would be possible to check if the rle code use is actually profitable, but the current implementation is good enough that this approach would only yield a single saved byte on enwik8.
  if (!size_only) {
    unsigned clsymbols[19];
    ZopfliLengthsToSymbols(clcl, 19, 7, clsymbols);

    AddBits(hlit, 5, bp, out, outsize);
    AddBits(hdist, 5, bp, out, outsize);
    AddBits(hclen, 4, bp, out, outsize);

    for (i = 0; i < hclen + 4; i++) {
      AddBits(clcl[order[i]], 3, bp, out, outsize);
    }

    for (i = 0; i < rle_size; i++) {
      unsigned symbol = clsymbols[rle[i]];
      AddHuffmanBits(symbol, clcl[rle[i]], bp, out, outsize);
      /* Extra bits. */
      if (rle[i] == 16) AddBits(rle_bits[i], 2, bp, out, outsize);
      else if (rle[i] == 17) AddBits(rle_bits[i], 3, bp, out, outsize);
      else if (rle[i] == 18) AddBits(rle_bits[i], 7, bp, out, outsize);
    }

    free(rle);
    free(rle_bits);
  }

  size_t result_size = 14;  /* hlit, hdist, hclen bits */
  result_size += (hclen + 4) * 3;  /* clcl bits */
  for(i = 0; i < 19; i++) {
    result_size += clcl[i] * clcounts[i];
  }
  /* Extra bits. */
  result_size += clcounts[16] * 2;
  result_size += clcounts[17] * 3;
  result_size += clcounts[18] * 7;

  return result_size;
}

/*
Gives the exact size of the tree, in bits, as it will be encoded in DEFLATE.
*/
size_t CalculateTreeSize(const unsigned* ll_lengths,
                                unsigned* d_lengths, unsigned char hq, unsigned* best) {
  PatchDistanceCodesForBuggyDecoders(d_lengths);
  size_t result = 0;
  if (hq){
    for(unsigned i = 0; i < (hq == 2 ? 32 : 10); i++) {
      if (!(i & 1) && (i & 8 || i & 16)){
        continue;
      }
      size_t size = EncodeTree(ll_lengths, d_lengths,
                               i & 1, i & 2, i & 4, i & 8, i & 16 || (hq == 1 && i == 9),
                               0, 0, 0);
      if (result == 0 || size < result){
        result = size;
        *best = i;
      }
    }

    return result;
  }
  *best = 7;
  return EncodeTree(ll_lengths, d_lengths, 1, 1, 1, 0, 0, 0, 0, 0);
}

/*
Adds all lit/len and dist codes from the lists as huffman symbols. Does not add
end code 256. expected_data_size is the uncompressed block size, used for
assert, but you can set it to 0 to not do the assertion.
*/
//TODO: Rewrite for x86_64 where bitbuffer is refilled once per len/dist pair
static void AddLZ77Data(const unsigned short* litlens,
                        const unsigned short* dists,
                        size_t lstart, size_t lend,
                        size_t expected_data_size,
                        unsigned* ll_symbols, const unsigned* ll_lengths,
                        unsigned* d_symbols, const unsigned* d_lengths,
                        unsigned char* bp,
                        unsigned char* out, size_t* outsize) {
  size_t testlength = 0;
  size_t i;
  //Revert some codes so we can choose the fast AddBits() function
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)

  for (i = 0; i < 30; i++){
    for (unsigned j = 0; j < d_lengths[i] / 2; j++){
      unsigned bit = d_symbols[i] & (1 << j);
      unsigned bit2 = d_symbols[i] & (1 << (d_lengths[i] - j - 1));
      d_symbols[i] &= UINT_MAX - (1 << j) - (1 << (d_lengths[i] - j - 1));
      d_symbols[i] |= (!!bit) << (d_lengths[i] - j - 1);
      d_symbols[i] |= (!!bit2) << j;
    }
  }
  for (i = 0; i < 286; i++){
    for (unsigned j = 0; j < ll_lengths[i] / 2; j++){
      unsigned bit = ll_symbols[i] & (1 << j);
      unsigned bit2 = ll_symbols[i] & (1 << (ll_lengths[i] - j - 1));
      ll_symbols[i] &= UINT_MAX - (1 << j) - (1 << (ll_lengths[i] - j - 1));
      ll_symbols[i] |= (!!bit) << (ll_lengths[i] - j - 1);
      ll_symbols[i] |= (!!bit2) << j;
    }
  }
  unsigned len_symbols[259];
  unsigned len_lengths[259];
  for (i = 3; i < 259; i++){
    unsigned lls = ZopfliGetLengthSymbol(i);
    if(ll_lengths[lls]){
      unsigned bitlen = ll_lengths[lls];
      len_lengths[i] = bitlen + ZopfliGetLengthExtraBits(i);
      assert(len_lengths[i] <= 25);
      len_symbols[i] = ll_symbols[lls] + (ZopfliGetLengthExtraBitsValue(i) << bitlen);
    }
  }

  size_t bits = (*outsize) * 8 + *bp - ((*bp) != 0) * 8;

#define setbits AddBits
#define FAST_BITWRITER
#else
#define setbits AddHuffmanBits
#endif

  for (i = lstart; i < lend; i++) {
    unsigned dist = dists[i];
    unsigned litlen = litlens[i];
    if (dist == 0) {
      assert(litlen < 256);
      assert(ll_lengths[litlen] > 0);
#ifdef FAST_BITWRITER
      AddBits2(ll_symbols[litlen], ll_lengths[litlen], out, &bits);
#else 
      AddHuffmanBits(ll_symbols[litlen], ll_lengths[litlen], bp, out, outsize);
#endif

      testlength++;
    } else {
      assert(litlen >= 3 && litlen <= ZOPFLI_MAX_MATCH);
      unsigned ds = ZopfliGetDistSymbol(dist);
      assert(d_lengths[ds]);


#ifdef FAST_BITWRITER
      AddBits2(len_symbols[litlen],
               len_lengths[litlen],
               out, &bits);

      AddBits2(d_symbols[ds], d_lengths[ds], out, &bits);

      AddBits2(ZopfliGetDistExtraBitsValue(dist),
               ZopfliGetDistExtraBits(dist),
               out, &bits);
#else
      unsigned lls = ZopfliGetLengthSymbol(litlen);
      assert(ll_lengths[lls]);

      AddHuffmanBits(ll_symbols[lls], ll_lengths[lls], bp, out, outsize);
      AddBits(ZopfliGetLengthExtraBitsValue(litlen),
              ZopfliGetLengthExtraBits(litlen),
              bp, out, outsize);

      AddHuffmanBits(d_symbols[ds], d_lengths[ds], bp, out, outsize);
      AddBits(ZopfliGetDistExtraBitsValue(dist),
              ZopfliGetDistExtraBits(dist),
              bp, out, outsize);
#endif

      testlength += litlen;
    }
  }

#ifdef FAST_BITWRITER
  *bp = bits & 7;
  (*outsize) = (bits / 8) + ((bits & 7) != 0);
#endif

  assert(testlength == expected_data_size);
}

static size_t AbsDiff(size_t x, size_t y) {
  if (x > y)
    return x - y;
  return y - x;
}
/*
 Change the population counts in a way that the consequent Hufmann tree
 compression, especially its rle-part will be more likely to compress this data
 more efficiently. length containts the size of the histogram.
 */
static void OptimizeHuffmanCountsForRlezop(int length, size_t* counts) {
  int i, k, stride;
  size_t symbol, sum, limit;


  /* 1) We don't want to touch the trailing zeros. We may break the
   rules of the format by adding more data in the distance codes. */
  for (; length >= 0; --length) {
    if (length == 0) {
      return;
    }
    if (counts[length - 1] != 0) {
      /* Now counts[0..length - 1] does not have trailing zeros. */
      break;
    }
  }

  /* 2) Let's mark all population counts that already can be encoded
   with an rle code.*/
  unsigned char* good_for_rle = (unsigned char*)calloc(length, 1);

  /* Let's not spoil any of the existing good rle codes.
   Mark any seq of 0's that is longer than 5 as a good_for_rle.
   Mark any seq of non-0's that is longer than 7 as a good_for_rle.*/
  symbol = counts[0];
  stride = 0;
  for (i = 0; i < length + 1; ++i) {
    if (i == length || counts[i] != symbol) {
      if ((symbol == 0 && stride >= 5) || stride >= 7) {
        for (k = 0; k < stride; ++k) {
          good_for_rle[i - k - 1] = 1;
        }
      }
      stride = 1;
      if (i != length) {
        symbol = counts[i];
      }
    } else {
      ++stride;
    }
  }

  /* 3) Let's replace those population counts that lead to more rle codes. */
  stride = 0;
  limit = counts[0];
  sum = 0;
  for (i = 0; i < length + 1; ++i) {
    if (i == length || good_for_rle[i]
        /* Heuristic for selecting the stride ranges to collapse. */
        || AbsDiff(counts[i], limit) >= 4) {
      if (stride >= 4) {
        /* The stride must end, collapse what we have, if we have enough (4). */
        int count = (sum + stride / 2) / stride;
        if (count < 1 && sum) count = 1;
        for (k = 0; k < stride; ++k) {
          /* We don't want to change value at counts[i],
           that is already belonging to the next stride. Thus - 1. */
          counts[i - k - 1] = count;
        }
      }
      stride = 0;
      sum = 0;
      if (i < length - 3) {
        /* All interesting strides have a count of at least 4,
         at least when non-zeros. */
        limit = (counts[i] + counts[i + 1] +
                 counts[i + 2] + counts[i + 3] + 2) / 4;
      } else if (i < length) {
        limit = counts[i];
      } else {
        limit = 0;
      }
    }
    ++stride;
    if (i != length) {
      sum += counts[i];
    }
  }

  free(good_for_rle);
}

//From brotli.
void OptimizeHuffmanCountsForRle(int length, size_t* counts) {
  // Let's make the Huffman code more compatible with rle encoding.
  for (;; --length) {
    if (!length) {
      return;  // All zeros.
    }
    if (counts[length - 1]) {
      // Now counts[0..length - 1] does not have trailing zeros.
      break;
    }
  }
  int i;

  // 2) Let's mark all population counts that already can be encoded
  // with an rle code.
  unsigned char* good_for_rle = (unsigned char*)calloc(length, 1);
  if (!good_for_rle) {
    exit(1);
  }

  // Let's not spoil any of the existing good rle codes.
  // Mark any seq of 0's that is longer as 5 as a good_for_rle.
  // Mark any seq of non-0's that is longer as 7 as a good_for_rle.
  size_t symbol = counts[0];
  int stride = 0;
  for (i = 0; i < length + 1; ++i) {
    if (i == length || counts[i] != symbol) {
      if ((!symbol && stride >= 5) || stride >= 7) {
        for (int k = 0; k < stride; ++k) {
          good_for_rle[i - k - 1] = 1;
        }
      }
      stride = 1;
      symbol = counts[i];
    } else {
      ++stride;
    }
  }

  // 3) Let's replace those population counts that lead to more rle codes.
  // Math here is in 24.8 fixed point representation.
  const int streak_limit = 1240;
  stride = 0;
  long limit = 256 * (counts[0] + counts[1] + counts[2]) / 3 + 420;
  int sum = 0;
  for (i = 0; i < length + 1; ++i) {
    if (i == length || good_for_rle[i] ||
        (i && good_for_rle[i - 1]) ||
        labs(((long)(256 * counts[i])) - limit) >= streak_limit) {
      if (stride >= 4) {
        // The stride must end, collapse what we have, if we have enough (4).
        int count = (sum + stride / 2) / stride;
        if (count < 1 && sum) {
          count = 1;
        }
        for (int k = 0; k < stride; ++k) {
          // We don't want to change value at counts[i],
          // that is already belonging to the next stride. Thus - 1.
          counts[i - k - 1] = count;
        }
      }
      stride = 0;
      sum = 0;
      if (i < length - 2) {
        // All interesting strides have a count of at least 4,
        // at least when non-zeros.
        limit = 256 * (counts[i] + counts[i + 1] + counts[i + 2]) / 3 + 420;
      } else {
        limit = 256 * counts[i];
      }
    }
    ++stride;
    if (i != length) {
      sum += counts[i];
      if (stride >= 4) {
        limit = (256 * sum + stride / 2) / stride;
      }
      if (stride == 4) {
        limit += 120;
      }
    }
  }
  free(good_for_rle);
}

static size_t CalculateBlockSymbolSize(const size_t* ll_counts, const size_t* d_counts, const unsigned* ll_lengths, const unsigned* d_lengths){
  size_t result = 0;
  size_t i = 0;
  for (i = 0; i < 286; i++){
    result += ll_lengths[i] * ll_counts[i];
  }
  for (i = 265; i < 269; i++){
    result += ll_counts[i];
  }
  for (i = 269; i < 273; i++){
    result += ll_counts[i] * 2;
  }
  for (i = 273; i < 277; i++){
    result += ll_counts[i] * 3;
  }
  for (i = 277; i < 281; i++){
    result += ll_counts[i] * 4;
  }
  for (i = 281; i < 285; i++){
    result += ll_counts[i] * 5;
  }
  for (i = 0; i < 30; i++){
    result += d_lengths[i] * d_counts[i];
  }
  for (i = 4; i < 30; i++){
    result += ((i - 2) / 2) * d_counts[i];
  }
  return result;
}
/*
Calculates the bit lengths for the symbols for dynamic blocks. Chooses bit
lengths that give the smallest size of tree encoding + encoding of all the
symbols to have smallest output size. This are not necessarily the ideal Huffman
bit lengths.
*/
static size_t GetAdvancedLengths(const unsigned short* litlens,
                                 const unsigned short* dists,
                                 size_t lstart, size_t lend,
                                 unsigned* ll_lengths, unsigned* d_lengths, unsigned char symbols){
  size_t ll_counts[288];
  size_t d_counts[32];
  size_t ll_counts2[288];
  size_t d_counts2[32];
  size_t ll_counts3[288];
  size_t d_counts3[32];
  unsigned dummy;

  ZopfliLZ77Counts(litlens, dists, lstart, lend, ll_counts, d_counts, symbols);
  memcpy(ll_counts2, ll_counts, 288 * sizeof(size_t));
  memcpy(d_counts2, d_counts, 32 * sizeof(size_t));
  memcpy(ll_counts3, ll_counts, 288 * sizeof(size_t));
  memcpy(d_counts3, d_counts, 32 * sizeof(size_t));

  OptimizeHuffmanCountsForRle(32, d_counts);
  OptimizeHuffmanCountsForRle(288, ll_counts);
  ZopfliLengthLimitedCodeLengths(ll_counts, 288, 15, ll_lengths);
  ZopfliLengthLimitedCodeLengths(d_counts, 32, 15, d_lengths);
  size_t best = CalculateBlockSymbolSize(ll_counts2, d_counts2, ll_lengths, d_lengths);
  unsigned nix = CalculateTreeSize(ll_lengths, d_lengths, 2, &dummy);
  best += nix;

  size_t* lcounts = ll_counts;
  size_t* dcounts = d_counts;


  unsigned ll_lengths2[288];
  unsigned d_lengths2[32];
  OptimizeHuffmanCountsForRlezop(32, d_counts3);
  OptimizeHuffmanCountsForRlezop(288, ll_counts3);
  ZopfliLengthLimitedCodeLengths(ll_counts3, 288, 15, ll_lengths2);
  ZopfliLengthLimitedCodeLengths(d_counts3, 32, 15, d_lengths2);
  size_t next = CalculateBlockSymbolSize(ll_counts2, d_counts2, ll_lengths2, d_lengths2);
  unsigned nextnix = CalculateTreeSize(ll_lengths2, d_lengths2, 2, &dummy);
  next += nextnix;

  if(next < best){
    best = next;
    lcounts = ll_counts3;
    dcounts = d_counts3;
    memcpy(ll_lengths, ll_lengths2, sizeof(unsigned) * 286);
    memcpy(d_lengths, d_lengths2, sizeof(unsigned) * 30);
    nix = nextnix;
  }

  ZopfliLengthLimitedCodeLengths(ll_counts2, 288, 15, ll_lengths2);
  ZopfliLengthLimitedCodeLengths(d_counts2, 32, 15, d_lengths2);
  next = CalculateBlockSymbolSize(ll_counts2, d_counts2, ll_lengths2, d_lengths2);
  nextnix = CalculateTreeSize(ll_lengths2, d_lengths2, 2, &dummy);
  next += nextnix;

  if(next < best){
    best = next;
    lcounts = ll_counts2;
    dcounts = d_counts2;
    memcpy(ll_lengths, ll_lengths2, sizeof(unsigned) * 286);
    memcpy(d_lengths, d_lengths2, sizeof(unsigned) * 30);
    nix = nextnix;
  }

  unsigned maxbits = 15;
  while(--maxbits > 8){
    ZopfliLengthLimitedCodeLengths(lcounts, 288, maxbits, ll_lengths2);
    ZopfliLengthLimitedCodeLengths(dcounts, 32, maxbits, d_lengths2);
    next = CalculateBlockSymbolSize(ll_counts2, d_counts2, ll_lengths2, d_lengths2);
    nextnix = CalculateTreeSize(ll_lengths2, d_lengths2, 2, &dummy);
    next += nextnix;

    if(next < best){
      best = next;
      memcpy(ll_lengths, ll_lengths2, sizeof(unsigned) * 286);
      memcpy(d_lengths, d_lengths2, sizeof(unsigned) * 30);
      nix = nextnix;
    }
    else if (best < next){
      break;
    }
  }

  best -= nix;
  return best;
}

size_t GetDynamicLengthsuse(unsigned* ll_lengths, unsigned* d_lengths, const size_t* ll_counts, const size_t* d_counts) {
  size_t ll_counts2[288];
  size_t d_counts2[32];

  memcpy(ll_counts2, ll_counts, 288 * sizeof(size_t));
  memcpy(d_counts2, d_counts, 32 * sizeof(size_t));
  OptimizeHuffmanCountsForRle(32, d_counts2);
  OptimizeHuffmanCountsForRle(288, ll_counts2);

  ZopfliLengthLimitedCodeLengths(ll_counts2, 288, 15, ll_lengths);
  ZopfliLengthLimitedCodeLengths(d_counts2, 32, 15, d_lengths);
  return CalculateBlockSymbolSize(ll_counts, d_counts, ll_lengths, d_lengths);
}

static size_t GetDynamicLengths(const unsigned short* litlens,
                                const unsigned short* dists,
                                size_t lstart, size_t lend,
                                unsigned* ll_lengths, unsigned* d_lengths, unsigned char symbols) {
  size_t ll_counts[288];
  size_t d_counts[32];

  ZopfliLZ77Counts(litlens, dists, lstart, lend, ll_counts, d_counts, symbols);
  return GetDynamicLengthsuse(ll_lengths, d_lengths, ll_counts, d_counts);
}

static double ZopfliCalculateEntropy2(const size_t* count, size_t n, unsigned* lengths) {
  unsigned sum = 0;
  unsigned i;
  for (i = 0; i < n; ++i) {
    sum += count[i];
  }
  if (!sum){
    memset(lengths, 0, n * sizeof(unsigned));
    return 0;
  }
  float log2sum = log2f(sum);
  double result = 0;

  for (i = 0; i < n; ++i) {
    /* When the count of the symbol is 0, but its cost is requested anyway, it
     means the symbol will appear at least once anyway, so give it the cost as if
     its count is 1.*/
    float val;
    if (!count[i]) val = log2sum;//TODO: log2sum for ENWIK, 0 for PNG. Makes a big difference
    else {
      val = log2sum - log2f(count[i]);
    }
    if (val > 15){
      val = 15;
    }
    lengths[i] = val;
    result += val * count[i];
  }
  return result;
}

size_t GetDynamicLengths2(unsigned* ll_lengths, unsigned* d_lengths, const size_t* ll_counts, const size_t* d_counts) {
  unsigned i;
  size_t result = 0;

  result += ZopfliCalculateEntropy2(ll_counts, 288, ll_lengths);
  result += ZopfliCalculateEntropy2(d_counts, 32, d_lengths);
  for (i = 265; i < 269; i++){
    result += ll_counts[i];
  }
  for (i = 269; i < 273; i++){
    result += ll_counts[i] * 2;
  }
  for (i = 273; i < 277; i++){
    result += ll_counts[i] * 3;
  }
  for (i = 277; i < 281; i++){
    result += ll_counts[i] * 4;
  }
  for (i = 281; i < 285; i++){
    result += ll_counts[i] * 5;
  }
  for (i = 4; i < 30; i++){
    result += ((i - 2) / 2) * d_counts[i];
  }
  return result;
}

double ZopfliCalculateBlockSize(const unsigned short* litlens,
                                const unsigned short* dists,
                                size_t lstart, size_t lend, int btype, unsigned char hq, unsigned char symbols) {
  double result = 3; /* bfinal and btype bits */

  if(btype == 1) {
    size_t i;
    result += 7;
    result += 8 * (lend - lstart);
    for (i = lstart; i < lend; i++) {
      if (dists[i] == 0) {
        result += litlens[i] >= 144;
      }
      else {
        result += 5 - (litlens[i] < 115);
        result += ZopfliGetLengthExtraBits(litlens[i]);
        result += ZopfliGetDistExtraBits(dists[i]);
      }
    }
    return result;
  }
  unsigned ll_lengths[288];
  unsigned d_lengths[32];
  unsigned dummy;
  //TODO: Better for PNG, worse for enwik
  //result += GetAdvancedLengths(litlens, dists, lstart, lend, ll_lengths, d_lengths, symbols);
  result += GetDynamicLengths(litlens, dists, lstart, lend, ll_lengths, d_lengths, symbols);
  result += CalculateTreeSize(ll_lengths, d_lengths, hq, &dummy);
  return result;
}

static unsigned char ReplaceBadCodes(unsigned short** litlens,
                            unsigned short** dists,
                            size_t* lend, const unsigned char* in, size_t instart, unsigned* ll_lengths, unsigned* d_lengths){
  size_t end = *lend;

  unsigned short* litlens2 = (unsigned short*)malloc(end * 3 * sizeof(unsigned short));
  unsigned short* dists2 = (unsigned short*)malloc(end * 3 * sizeof(unsigned short));
  if (!(litlens2 && dists2)){
    exit(1);
  }

  size_t pos = instart;
  size_t k = 0;
  for (size_t i = 0; i < end; i++){
    unsigned char change = 0;
    size_t length = (*dists)[i] == 0 ? 1 : (*litlens)[i];
    if (length >= 3 && length <= 7){
      /*Check if the match is cheaper than several literals*/
      const unsigned char* litplace = &in[pos - (*dists)[i]];
      unsigned litprice = 0;
      unsigned char cont = 1;
      for(unsigned j = 0; j < length; j++){
        if (!ll_lengths[*litplace]){
          //Bail out.
          cont = 0;
          break;
        }
        litprice += ll_lengths[*litplace++];
      }
      if (cont){
        unsigned char distprice = ll_lengths[ZopfliGetLengthSymbol(length)] + ZopfliGetLengthExtraBits(length) + ZopfliGetDistExtraBits((*dists)[i])
          + d_lengths[ZopfliGetDistSymbol((*dists)[i])];
        if (litprice < distprice){
          litplace = &in[pos - (*dists)[i]];
          change = 1;
          for(unsigned j = 0; j < length; j++){
            litlens2[i + k] = *litplace;
            dists2[i + k] = 0;
            k++;
            litplace++;
          }
          k--;
          (*lend) += length - 1;
        }
      }
    }
    if (!change){
      litlens2[i + k] = (*litlens)[i];
      dists2[i + k] = (*dists)[i];
    }
    pos += length;
  }

  (*litlens) = litlens2;
  (*dists) = dists2;
  return end != *lend;
}

/*
Adds a deflate block with the given LZ77 data to the output.
options: global program options
btype: the block type, must be 1 or 2
final: whether to set the "final" bit on this block, must be the last block
litlens: literal/length array of the LZ77 data, in the same format as in
    ZopfliLZ77Store.
dists: distance array of the LZ77 data, in the same format as in
    ZopfliLZ77Store.
lstart: where to start in the LZ77 data
lend: where to end in the LZ77 data (not inclusive)
expected_data_size: the uncompressed block size, used for assert, but you can
  set it to 0 to not do the assertion.
bp: output bit pointer
out: dynamic output array to append to
outsize: dynamic output array size
*/
static void AddLZ77Block(int btype, int final,
                         unsigned short* litlens,
                         unsigned short* dists,
                         size_t lend,
                         size_t expected_data_size,
                         unsigned char* bp,
                         unsigned char** out, size_t* outsize, unsigned hq, const unsigned char* in,
                         size_t instart, unsigned replaceCodes, unsigned char advanced) {
  unsigned ll_lengths[288];
  unsigned d_lengths[32];
  unsigned ll_symbols[288];
  unsigned d_symbols[32];
  unsigned best = 0;

  //Calculate final outsize, symbols and huffman tree
  size_t outpred;
  if (btype == 1) {
    /* Fixed block. */
    size_t i;
    for (i = 0; i < 144; i++) ll_lengths[i] = 8;
    for (i = 144; i < 256; i++) ll_lengths[i] = 9;
    for (i = 256; i < 280; i++) ll_lengths[i] = 7;
    for (i = 280; i < 288; i++) ll_lengths[i] = 8;
    for (i = 0; i < 32; i++) d_lengths[i] = 5;
    outpred = ZopfliCalculateBlockSize(litlens, dists, 0, lend, 1, 0, 0);
  } else{
    /* Dynamic block. */
    outpred = 3;
    outpred += GetDynamicLengths(litlens, dists, 0, lend, ll_lengths, d_lengths, 0);
    outpred += CalculateTreeSize(ll_lengths, d_lengths, hq, &best);

    unsigned char change = 1;
    for (unsigned i = 0; i < replaceCodes; i++){
      if (!(i & 1)){
        unsigned short * free1 = litlens;
        unsigned short * free2 = dists;
        change = ReplaceBadCodes(&litlens, &dists, &lend, in, instart, ll_lengths, d_lengths);
        if (!change && i + 1 != replaceCodes && i){
          outpred += CalculateTreeSize(ll_lengths, d_lengths, hq, &best);
        }
        free(free1);
        free(free2);
        if (!change){
          break;
        }
      }
      else{
        //TODO: This may make compression worse due to bigger huffman headers.
        outpred = 3;
        outpred += GetDynamicLengths(litlens, dists, 0, lend, ll_lengths, d_lengths, 0);
        if (replaceCodes - i < 3 || advanced){
          outpred += CalculateTreeSize(ll_lengths, d_lengths, hq, &best);
        }
      }
    }
  }
  outpred += *outsize * 8 + *bp -((*bp != 0) * 8);
  (*out) = (unsigned char*)realloc(*out, outpred / 8 + 1 + 8);
  if (!(*out)){
    exit(1);
  }
  memset(&((*out)[*outsize]), 0, outpred / 8 + (!!(outpred & 7)) - (*outsize) + 8);

  AddBit(final, bp, out, outsize);
  AddBit(btype & 1, bp, out, outsize);
  AddBit((btype & 2) >> 1, bp, out, outsize);

  if (btype == 2){
    if(advanced){
      outpred = *outsize * 8 + *bp -((*bp != 0) * 8) + GetAdvancedLengths(litlens, dists, 0, lend, ll_lengths, d_lengths, 0);
      outpred += CalculateTreeSize(ll_lengths, d_lengths, 2, &best);
    }
    PatchDistanceCodesForBuggyDecoders(d_lengths);
    EncodeTree(ll_lengths, d_lengths,
               best & 1, best & 2, best & 4, best & 8 , best & 16 || (hq == 1 && best == 9 && !advanced),
               bp, *out, outsize);
  }
  ZopfliLengthsToSymbols(ll_lengths, 288, 15, ll_symbols);
  ZopfliLengthsToSymbols(d_lengths, 32, 15, d_symbols);
  AddLZ77Data(litlens, dists, 0, lend
              , expected_data_size
              , ll_symbols, ll_lengths, d_symbols, d_lengths,
              bp, *out, outsize);
  setbits(ll_symbols[256], ll_lengths[256], bp, *out, outsize);
#undef setbits

  if (!(replaceCodes & 1)){
    assert(outpred == *outsize * 8 + *bp - (*bp != 0) * 8);
  }
  if (replaceCodes){
    free(litlens);
    free(dists);
  }
}

static void DeflateDynamicBlock(const ZopfliOptions* options, int final,
                                const unsigned char* in,
                                size_t instart, size_t inend,
                                unsigned char* bp,
                                unsigned char** out, size_t* outsize, unsigned char* costmodelnotinited, SymbolStats* statsp, unsigned char twiceMode, ZopfliLZ77Store* twiceStore, unsigned mfinexport) {
  size_t blocksize = inend - instart;
  ZopfliLZ77Store store;
  int btype = 2;

  ZopfliInitLZ77Store(&store);

  if (blocksize <= options->skipdynamic){
    btype = 1;
    ZopfliLZ77OptimalFixed(options, in, instart, inend, &store, mfinexport);
  }
  else{
    ZopfliLZ77Optimal2(options, in, instart, inend, &store, *costmodelnotinited, statsp, mfinexport);
  }
  *costmodelnotinited = 0;

  /* For small block, encoding with fixed tree can be smaller. For large block,
  don't bother doing this expensive test, dynamic tree will be better.*/
  if (blocksize > options->skipdynamic && store.size < options->trystatic){
    ZopfliLZ77Store fixedstore;
    ZopfliInitLZ77Store(&fixedstore);
    ZopfliLZ77OptimalFixed(options, in, instart, inend, &fixedstore, 0);
    double dyncost = ZopfliCalculateBlockSize(store.litlens, store.dists, 0, store.size, 2, options->searchext, store.symbols);
    double fixedcost = ZopfliCalculateBlockSize(fixedstore.litlens, fixedstore.dists, 0, fixedstore.size, 1, options->searchext, store.symbols);
    if (fixedcost <= dyncost) {
      btype = 1;
      ZopfliCleanLZ77Store(&store);
      store = fixedstore;
    } else {
      ZopfliCleanLZ77Store(&fixedstore);
    }
  }

  if (twiceMode & 1){
    ZopfliInitLZ77Store(twiceStore);
    twiceStore->dists = store.dists;
    twiceStore->litlens = store.litlens;
    twiceStore->size = store.size;
  }
  else{
    AddLZ77Block(btype, final,
                 store.litlens, store.dists, store.size,
                 blocksize, bp, out, outsize, options->searchext, in, instart, options->replaceCodes, options->advanced);

    if (!options->replaceCodes){
      ZopfliCleanLZ77Store(&store);
    }
  }
}

#ifndef NOMULTI
struct BlockData {
  int btype;
  ZopfliLZ77Store store;
  size_t start;
  size_t end;
  SymbolStats* statsp;
};

static void DeflateDynamicBlock2(const ZopfliOptions* options, const unsigned char* in,
                                 BlockData** instore, BlockData* blockend, std::mutex& mtx) {
  for(;;) {
    mtx.lock();
    BlockData* store = *instore;
    if(store == blockend){
      mtx.unlock();
      return;
    }
    (*instore)++;
    mtx.unlock();
    size_t instart = store->start;
    size_t inend = store->end;
    size_t blocksize = inend - instart;
    store->btype = 2;
    
    ZopfliInitLZ77Store(&store->store);
    
    if (blocksize <= options->skipdynamic){
      store->btype = 1;
      ZopfliLZ77OptimalFixed(options, in, instart, inend, &store->store, 0);
    }
    else{
      ZopfliLZ77Optimal2(options, in, instart, inend, &store->store, 1, store->statsp, 0);
    }
    
    /* For small block, encoding with fixed tree can be smaller. For large block,
     don't bother doing this expensive test, dynamic tree will be better.*/
    if (blocksize > options->skipdynamic && store->store.size < options->trystatic){
      double dyncost, fixedcost;
      ZopfliLZ77Store fixedstore;
      ZopfliInitLZ77Store(&fixedstore);
      ZopfliLZ77OptimalFixed(options, in, instart, inend, &fixedstore, 0);
      dyncost = ZopfliCalculateBlockSize(store->store.litlens, store->store.dists, 0, store->store.size, 2, options->searchext, store->store.symbols);
      fixedcost = ZopfliCalculateBlockSize(fixedstore.litlens, fixedstore.dists, 0, fixedstore.size, 1, options->searchext, fixedstore.symbols);
      if (fixedcost <= dyncost) {
        store->btype = 1;
        ZopfliCleanLZ77Store(&store->store);
        store->store = fixedstore;
      } else {
        ZopfliCleanLZ77Store(&fixedstore);
      }
    }
  }
}

static void DeflateSplittingFirst2(
  const ZopfliOptions* options, int final,
  const unsigned char* in, size_t inend,
  unsigned char* bp, unsigned char** out, size_t* outsize,
  size_t npoints, size_t* splitpoints, SymbolStats* statsp,
 unsigned char twiceMode, ZopfliLZ77Store* twiceStore, size_t msize)
{
  size_t mnext = msize;
  unsigned numblocks = npoints + 1;

  unsigned threads = options->multithreading;
  if(threads > numblocks){
    threads = numblocks;
  }
  std::vector<std::thread> multi (threads);
  std::vector<BlockData> d (numblocks);
  size_t i;

  for (i = 0; i < numblocks; i++) {
    d[i].start = i == 0 ? 0 : splitpoints[i - 1];
    d[i].end = i == npoints ? inend : splitpoints[i];
    d[i].statsp = &statsp[i];
  }
  BlockData* data = &d[0];
  BlockData* blockend = data + numblocks;
  std::mutex mtx;
  for (i = 0; i < threads; i++) {
    multi[i] = std::thread(DeflateDynamicBlock2,options, in, &data, blockend, std::ref(mtx));
  }
  for(std::thread& t : multi) {
    t.join();
  }

  if (twiceMode & 1){
    unsigned j = 0;

    for(;;){
      ZopfliInitLZ77Store(twiceStore);
      for(; j < numblocks; j++){
        twiceStore->litlens = (unsigned short*)realloc(twiceStore->litlens, sizeof(unsigned short) * (twiceStore->size + d[j].store.size));
        twiceStore->dists = (unsigned short*)realloc(twiceStore->dists, sizeof(unsigned short) * (twiceStore->size + d[j].store.size));
        memcpy(twiceStore->litlens + twiceStore->size, d[j].store.litlens, d[j].store.size * sizeof(unsigned short));
        memcpy(twiceStore->dists + twiceStore->size, d[j].store.dists, d[j].store.size * sizeof(unsigned short));
        free(d[j].store.dists);
        free(d[j].store.litlens);
        twiceStore->size += d[j].store.size;
        if(d[j].end == mnext){
          mnext += msize;
          j++;
          break;
        }
      }

      if(j == numblocks){
        break;
      }
      twiceStore++;
    }
  }
  else{
    for (i = 0; i < numblocks; i++) {
      size_t start = i == 0 ? 0 : splitpoints[i - 1];
      size_t end = i == npoints ? inend : splitpoints[i];

      AddLZ77Block(d[i].btype, i == npoints && final,
                   d[i].store.litlens, d[i].store.dists, d[i].store.size,
                   end - start, bp, out, outsize, options->searchext, in, start, options->replaceCodes, options->advanced);
      if (!options->replaceCodes){
        ZopfliCleanLZ77Store(&d[i].store);
      }
    }
  }

  free(splitpoints);
  free(statsp);
}

static void ZopfliDeflateMulti(const ZopfliOptions* options, int final,
                               const unsigned char* in, const size_t insize,
                               unsigned char* bp, unsigned char** out, size_t* outsize){
  size_t msize = ZOPFLI_MASTER_BLOCK_SIZE;

  if (!options->isPNG && options->numiterations == 1){
    msize /= 5;
  }
  ZopfliLZ77Store* lf = 0;//!
  ZopfliLZ77Store dummy;
  if(options->twice){
    lf = (ZopfliLZ77Store*)malloc(((insize / msize) + 1) * sizeof(ZopfliLZ77Store));
    if(!lf){
      return;
    }
  }

  for (unsigned it = 0; it <= options->twice; it++) {
    size_t i = 0;
    size_t npoints = 0;
    size_t* splitpoints = 0;
    SymbolStats* stats = 0;

    unsigned mblocks = 0;
    while (i < insize) {
      if(it == 0 && options->twice){
        ZopfliInitLZ77Store(lf + mblocks);
      }

      int masterfinal = (i + msize >= insize);
      size_t size = masterfinal ? insize - i : msize;
      ZopfliBlockSplit(options, in, i, i + size, &splitpoints, &npoints, &stats, 1 + (!!it), it ? lf[mblocks] : dummy);
      if(i + size < insize){
        ZOPFLI_APPEND_DATA(i + size, &splitpoints, &npoints);
      }
      mblocks++;
      i += size;
    }

    DeflateSplittingFirst2(options, final, in, insize, bp,
                           out, outsize, npoints, splitpoints, stats,
                           options->twice && it != options->twice, lf, msize);
  }
  if(options->twice){
    free(lf);
  }
}
#endif

/*
 Does squeeze strategy where first block splitting is done, then each block is
 squeezed.
 Parameters: see description of the ZopfliDeflate function.
 */
static void DeflateSplittingFirst(const ZopfliOptions* options,
                                  int final,
                                  const unsigned char* in,
                                  size_t instart, size_t inend,
                                  unsigned char* bp,
                                  unsigned char** out, size_t* outsize, unsigned char* costmodelnotinited, unsigned char twiceMode, ZopfliLZ77Store* twiceStore) {
  size_t* splitpoints = 0;
  size_t npoints = 0;
  SymbolStats* statsp = 0;
  ZopfliBlockSplit(options, in, instart, inend, &splitpoints, &npoints, &statsp, twiceMode, *twiceStore);

  ZopfliLZ77Store* stores;
  if (twiceMode & 1){
    stores = (ZopfliLZ77Store*)malloc((npoints + 1) * sizeof(ZopfliLZ77Store));
    if(!stores){
      exit(1);
    }
  }
  for (size_t i = 0; i <= npoints; i++) {
    size_t start = i == 0 ? instart : splitpoints[i - 1];
    size_t end = i == npoints ? inend : splitpoints[i];
    unsigned x = npoints == 0 ? 0 : i == 0 ? 2 : i == npoints ? 1 : 3;
    DeflateDynamicBlock(options, i == npoints && final, in, start, end,
                        bp, out, outsize, costmodelnotinited, &(statsp[i]), twiceMode, stores + i, x);
  }
  if (twiceMode & 1){
    ZopfliInitLZ77Store(twiceStore);
    for(size_t i = 0; i < npoints + 1; i++){
      twiceStore->litlens = (unsigned short*)realloc(twiceStore->litlens, sizeof(unsigned short) * (twiceStore->size + stores->size));
      twiceStore->dists = (unsigned short*)realloc(twiceStore->dists, sizeof(unsigned short) * (twiceStore->size + stores->size));
      memcpy(twiceStore->litlens + twiceStore->size, stores->litlens, stores->size * sizeof(unsigned short));
      memcpy(twiceStore->dists + twiceStore->size, stores->dists, stores->size * sizeof(unsigned short));
      free(stores->dists);
      free(stores->litlens);
      twiceStore->size += stores->size;
      stores++;
    }
    free(stores - (npoints + 1));
  }

  free(splitpoints);
  free(statsp);
}

/*
Deflate a part, to allow ZopfliDeflate() to use multiple master blocks if
needed.
It is possible to call this function multiple times in a row, shifting
instart and inend to next bytes of the data. If instart is larger than 0, then
previous bytes are used as the initial dictionary for LZ77.
This function will usually output multiple deflate blocks. If final is 1, then
the final bit will be set on the last block.
*/
static void ZopfliDeflatePart(const ZopfliOptions* options, int final,
                       const unsigned char* in, size_t instart, size_t inend,
                       unsigned char* bp, unsigned char** out,
                       size_t* outsize, unsigned char* costmodelnotinited, unsigned char twiceMode, ZopfliLZ77Store* twiceStore) {
  DeflateSplittingFirst(options, final, in, instart, inend, bp, out, outsize, costmodelnotinited, twiceMode, twiceStore);
}

/*TODO: in needs to be alloc'd 8 bytes past inend. This may cause crashes if code is modified and nonstandard alloc function is used for allocation of in*/
void ZopfliDeflate(const ZopfliOptions* options, int final,
                   const unsigned char* in, size_t insize,
                   unsigned char* bp, unsigned char** out, size_t* outsize) {
  if (!insize){
    (*out) = (unsigned char*)realloc(*out, *outsize + 10);
    AddBit(final, bp, out, outsize);
    AddBits(1, 2, bp, *out, outsize);  // btype 01
    AddBits(0, 7, bp, *out, outsize);
    return;
  }
#ifndef NOMULTI
  if(options->multithreading > 1 && insize >= options->noblocksplit){
    ZopfliDeflateMulti(options, final, in, insize, bp, out, outsize);
    return;
  }
#endif
#if ZOPFLI_MASTER_BLOCK_SIZE == 0
  ZopfliDeflatePart(options, final, in, 0, insize, bp, out, outsize, &costmodelnotinited);
#else

  size_t i = 0;
  size_t msize = ZOPFLI_MASTER_BLOCK_SIZE;
  unsigned char costmodelnotinited = 1;
  if (!options->isPNG && options->numiterations == 1){
    msize /= 5;
  }
  while (i < insize) {
    int masterfinal = (i + msize >= insize);
    int final2 = final && masterfinal;
    size_t size = masterfinal ? insize - i : msize;
    ZopfliLZ77Store lf;
    ZopfliInitLZ77Store(&lf);
    if (!options->twice){
      ZopfliDeflatePart(options, final2, in, i, i + size, bp, out, outsize, &costmodelnotinited, 0, &lf);
    }
    else{
      unsigned char cache = costmodelnotinited;
      ZopfliDeflatePart(options, final2, in, i, i + size, bp, out, outsize, &costmodelnotinited, 1, &lf);
      for (unsigned it = 0; it < options->twice; it++) {
        costmodelnotinited = cache;
        ZopfliDeflatePart(options, final2, in, i, i + size, bp, out, outsize, &costmodelnotinited, 2 + (it != options->twice - 1), &lf);
      }
    }
    i += size;
  }
#endif
}
