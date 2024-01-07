/* LzFind.c -- Match finder for LZ algorithms
2009-04-22 : Igor Pavlov : Public domain */

/* Modified by Felix Hanau*/
#include <string.h>
#include <stdlib.h>

#include "LzFind.h"
#include "zopfli/util.h"
#include "zopfli/match.h"

void MatchFinder_Free(CMatchFinder *p)
{
  free(p->hash);
}

void MatchFinder_Create(CMatchFinder *p)
{
  //256kb hash, 256kb binary tree
  p->hash = (UInt32*)malloc(((2 * ZOPFLI_WINDOW_SIZE) + LZFIND_HASH_SIZE) * sizeof(UInt32));
  if (!p->hash)
  {
    exit(1);
  }
  p->son = p->hash + LZFIND_HASH_SIZE;

  memset(p->hash, 0, LZFIND_HASH_SIZE * sizeof(unsigned));
  p->cyclicBufferPos = 0;
  p->pos = ZOPFLI_WINDOW_SIZE;
}

static unsigned short * GetMatches(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, UInt32 *son,
                         UInt32 _cyclicBufferPos, unsigned short *distances)
{
  UInt32 maxLen = 2;
  UInt32 *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  UInt32 *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    if (delta >= ZOPFLI_WINDOW_SIZE)
    {
      *ptr0 = *ptr1 = 0;
      return distances;
    }
    UInt32 *pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - delta) & 32767) << 1);
    const Byte *pb = cur - delta;
    UInt32 len = (len0 < len1 ? len0 : len1);
    if (pb[len] == cur[len])
    {
      ++len;
      if (pb[len] == cur[len])
      {
        len = GetMatch(&cur[len], &pb[len], cur + lenLimit, cur + lenLimit - 8) - cur;
      }

      if (maxLen < len)
      {
        *distances++ = maxLen = len;
        *distances++ = delta;
        if (len == lenLimit)
        {
          *ptr1 = pair[0];
          *ptr0 = pair[1];
          return distances;
        }
      }
    }
    if (pb[len] < cur[len])
    {
      *ptr1 = curMatch;
      ptr1 = pair + 1;
      curMatch = *ptr1;
      len1 = len;
    }
    else
    {
      *ptr0 = curMatch;
      ptr0 = pair;
      curMatch = *ptr0;
      len0 = len;
    }
  }
}

static unsigned short * GetMatches3(UInt32 curMatch, UInt32 pos, const Byte *cur, UInt32 *son,
                         UInt32 _cyclicBufferPos, unsigned short *distances, UInt32 dist_258)
{
  UInt32 maxLen = 2;
  UInt32 *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  UInt32 *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    UInt32 *pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - delta) & 32767) << 1);
    if (delta == dist_258) {
        *distances++ = ZOPFLI_MAX_MATCH;
        *distances++ = delta;
        *ptr1 = pair[0];
        *ptr0 = pair[1];
        return distances;
    }
    const Byte *pb = cur - delta;
    UInt32 len = (len0 < len1 ? len0 : len1);
    if (pb[len] == cur[len])
    {
      ++len;
      if (pb[len] == cur[len])
      {
        len = GetMatch(&cur[len], &pb[len], cur + ZOPFLI_MAX_MATCH, cur + ZOPFLI_MAX_MATCH - 8) - cur;
      }

      if (maxLen < len)
      {
        *distances++ = maxLen = len;
        *distances++ = delta;
        if (len == ZOPFLI_MAX_MATCH)
        {
          *ptr1 = pair[0];
          *ptr0 = pair[1];
          return distances;
        }
      }
    }
    if (pb[len] < cur[len])
    {
      *ptr1 = curMatch;
      ptr1 = pair + 1;
      curMatch = *ptr1;
      len1 = len;
    }
    else
    {
      *ptr0 = curMatch;
      ptr0 = pair;
      curMatch = *ptr0;
      len0 = len;
    }
  }
}

static unsigned short * GetMatches2(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, UInt32 *son,
                         UInt32 _cyclicBufferPos, unsigned short *distances)
{
  UInt32 *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  UInt32 *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  UInt32 *pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - 1) & 32767) << 1);
  UInt32 rle_len = GetMatch(cur, cur - 1, cur + lenLimit, cur + lenLimit - 8) - cur;
  UInt32 maxLen = rle_len;
  *distances++ = rle_len;
  *distances++ = 1;
  if (rle_len == lenLimit)
  {
    *ptr1 = pair[0];
    *ptr0 = pair[1];
    return distances;
  }
  if (cur[rle_len - 1] < cur[rle_len])
  {
    *ptr1 = curMatch;
    ptr1 = pair + 1;
    curMatch = *ptr1;
    len1 = rle_len;
  }
  else
  {
    *ptr0 = curMatch;
    ptr0 = pair;
    curMatch = *ptr0;
    len0 = rle_len;
  }
  unsigned char ref_byte = *cur;
  unsigned starter = *(unsigned*)(cur - 1);
  uint64_t starter_full = (uint64_t)starter + (((uint64_t)starter) << 32);

  const unsigned char* min = pos > 65535 ? cur - 32767 : cur - (pos - 32768);
  min += 8;
  for (;;) {
    UInt32 delta = pos - curMatch;
    if (delta >= ZOPFLI_WINDOW_SIZE)
    {
      *ptr0 = *ptr1 = 0;
      return distances;
    }
    UInt32 *pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - delta) & 32767) << 1);
    const Byte *pb = cur - delta;
    UInt32 len = (len0 < len1 ? len0 : len1);
    if (pb[len] == cur[len])
    {
      ++len;
      if (pb[len] == cur[len])
      {
        len = GetMatch(&cur[len], &pb[len], cur + lenLimit, cur + lenLimit - 8) - cur;
      }

      if (maxLen < len)
      {
        *distances++ = maxLen = len;
        *distances++ = delta;
        if (len == lenLimit)
        {
          *ptr1 = pair[0];
          *ptr0 = pair[1];
          return distances;
        }
      }
    }
    //determine fast path bytes, if any
    UInt32 curMatch_rle = curMatch;

    if (pb[len] < cur[len])
    {
      *ptr1 = curMatch;
      ptr1 = pair + 1;
      curMatch = *ptr1;
      len1 = len;
    }
    else
    {
      *ptr0 = curMatch;
      ptr0 = pair;
      curMatch = *ptr0;
      len0 = len;
    }
    const unsigned char* rle_pos = pb;
    const unsigned char* _min = min;
    unsigned cnt = 0;
    if (_min < pb - (rle_len - len)) {_min = pb - (rle_len - len);}
    while (rle_pos > _min && *(uint64_t*)(rle_pos - 8) == starter_full) {rle_pos-=8; cnt+=8;}
    if (cnt) {
      if (cnt + len > rle_len - 1) {cnt = rle_len - 1 - len;}
      curMatch_rle -= cnt;
      if (pb[len] < ref_byte) {
        while (curMatch >= curMatch_rle) {
          delta = pos - curMatch;
          pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - delta) & 32767) << 1);
          *ptr1 = curMatch;
          ptr1 = pair + 1;
          curMatch = *ptr1;
          len1++;
        }
      } else {
        while (curMatch >= curMatch_rle) {
          delta = pos - curMatch;
          pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - delta) & 32767) << 1);
          *ptr0 = curMatch;
          ptr0 = pair;
          curMatch = *ptr0;
          len0++;
        }
      }
      //Could also handle further positions within an RLE run here, but this seems to offer little to no performance benefits.
    }
  }
}

static void SkipMatches(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, UInt32 *son,
                        UInt32 _cyclicBufferPos)
{
  UInt32 *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  UInt32 *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    if (delta >= ZOPFLI_WINDOW_SIZE)
    {
      *ptr0 = *ptr1 = 0;
      return;
    }

    UInt32 *pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - delta) & 32767) << 1);
    const Byte *pb = cur - delta;
    UInt32 len = (len0 < len1 ? len0 : len1);
    if (pb[len] == cur[len])
    {
      len = GetMatch(&cur[len], &pb[len], cur + lenLimit, cur + lenLimit - 8) - cur;

      if (len == lenLimit)
      {
        *ptr1 = pair[0];
        *ptr0 = pair[1];
        return;
      }
    }
    if (pb[len] < cur[len])
    {
      *ptr1 = curMatch;
      ptr1 = pair + 1;
      curMatch = *ptr1;
      len1 = len;
    }
    else
    {
      *ptr0 = curMatch;
      ptr0 = pair;
      curMatch = *ptr0;
      len0 = len;
    }
  }
}

static void SkipMatches2(UInt32 *son, UInt32 _cyclicBufferPos)
{
  UInt32 *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  UInt32 *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 *pair = son + (((_cyclicBufferPos + ZOPFLI_WINDOW_SIZE - 1) & 32767) << 1);
  *ptr1 = pair[0];
  *ptr0 = pair[1];
}

#ifdef __SSE4_2__
#include "nmmintrin.h"
#define HASH(cur) unsigned v = 0xffffff & *(const unsigned*)cur; UInt32 hashValue = _mm_crc32_u32(0, v) & LZFIND_HASH_MASK;
#else
#define HASH(cur) UInt32 hashValue = ((cur[2] | ((UInt32)cur[0] << 8)) ^ crc[cur[1]]) & LZFIND_HASH_MASK;
#endif

#define MOVE_POS \
  ++p->cyclicBufferPos; \
  p->cyclicBufferPos &= ZOPFLI_WINDOW_MASK; \
  p->buffer++; \
  ++p->pos;

#define MF_PARAMS(p) p->pos, p->buffer, p->son, p->cyclicBufferPos

unsigned short Bt3Zip_MatchFinder_GetMatches(CMatchFinder *p, unsigned short *distances)
{
  unsigned lenl = p->bufend - p->buffer; { if (lenl < ZOPFLI_MIN_MATCH) {return 0;}}
  const Byte *cur = p->buffer;
  HASH(cur);
  UInt32 curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  UInt32 offset = (UInt32)(GetMatches(lenl > ZOPFLI_MAX_MATCH ? ZOPFLI_MAX_MATCH : lenl, curMatch, MF_PARAMS(p), distances) - distances);
  MOVE_POS;
  return offset;
}

//Same as above, but assumes an RLE-style match is available.
unsigned short Bt3Zip_MatchFinder_GetMatches2(CMatchFinder *p, unsigned short *distances)
{
  const Byte *cur = p->buffer;
  HASH(cur);
  UInt32 curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  UInt32 offset = (UInt32)(GetMatches2(ZOPFLI_MAX_MATCH, curMatch, MF_PARAMS(p), distances) - distances);
  MOVE_POS;
  return offset;
}

//Same as above, but assumes a ZOPFLI_MAX_MATCH len match is available at dist_258.
unsigned short Bt3Zip_MatchFinder_GetMatches3(CMatchFinder *p, unsigned short *distances, unsigned dist_258)
{
  const Byte *cur = p->buffer;
  HASH(cur);
  UInt32 curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  UInt32 offset = (UInt32)(GetMatches3(curMatch, MF_PARAMS(p), distances, dist_258) - distances);
  MOVE_POS;
  return offset;
}

void Bt3Zip_MatchFinder_Skip(CMatchFinder* p, UInt32 num)
{
  while (num--)
  {
    const Byte *cur = p->buffer;
    HASH(cur);
    UInt32 curMatch = p->hash[hashValue];
    p->hash[hashValue] = p->pos;
    unsigned lenlimit = p->bufend - p->buffer;
    SkipMatches(lenlimit > ZOPFLI_MAX_MATCH ? ZOPFLI_MAX_MATCH : lenlimit, curMatch, MF_PARAMS(p));
    MOVE_POS;
  }
}

// Same as above, but optimized for case where there is a ZOPFLI_MAX_MATCH len match at distance 1.
// This implies that the match finder has processed at least one byte so far – otherwise there
// can't be available matches.
void Bt3Zip_MatchFinder_Skip2(CMatchFinder* p, UInt32 num)
{
  const Byte *cur = p->buffer;
  HASH(cur);
  while (num--)
  {
    p->hash[hashValue] = p->pos;
    SkipMatches2(p->son, p->cyclicBufferPos);
    MOVE_POS;
  }
}

void CopyMF(const CMatchFinder *p, CMatchFinder* copy){
  copy->hash = (UInt32*)malloc(((2 * ZOPFLI_WINDOW_SIZE) + LZFIND_HASH_SIZE) * sizeof(UInt32));
  if (!copy->hash)
  {
    exit(1);
  }
  copy->son = copy->hash + LZFIND_HASH_SIZE;
  memcpy(copy->hash, p->hash, ((2 * ZOPFLI_WINDOW_SIZE) + LZFIND_HASH_SIZE) * sizeof(UInt32));

  copy->cyclicBufferPos = p->cyclicBufferPos;
  copy->pos = p->pos;
  copy->buffer = p->buffer;
}
