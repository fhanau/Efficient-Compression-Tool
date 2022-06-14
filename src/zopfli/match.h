//
//  match.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 31.12.15.
//  Copyright © 2015 Felix Hanau.
//

/*
 Finds how long the match of scan and match is. Can be used to find how many
 bytes starting from scan, and from match, are equal. Returns the last byte
 after scan, which is still equal to the correspondinb byte after match.
 scan is the position to compare
 match is the earlier position to compare.
 end is the last possible byte, beyond which to stop looking.
 safe_end is a few (8) bytes before end, for comparing multiple bytes at once.
 */
#include <stdint.h>

#ifdef __GNUC__
__attribute__ ((always_inline, hot))
#endif
static inline const unsigned char* GetMatch(const unsigned char* scan,
                                            const unsigned char* match,
                                            const unsigned char* end
                                            , const unsigned char* safe_end) {
#ifdef __GNUC__
  /* Optimized Function based on cloudflare's zlib fork.*/
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
  } while (scan < end);

  if (unlikely(scan > end))
    scan = end;
  return scan;

#else

  if (sizeof(size_t) == 8) {
    /* 8 checks at once per array bounds check (size_t is 64-bit). */
    while (scan < safe_end && *((size_t*)scan) == *((size_t*)match)) {
      scan += 8;
      match += 8;
    }
  } else if (sizeof(unsigned) == 4) {
    /* 4 checks at once per array bounds check (unsigned is 32-bit). */
    while (scan < safe_end
           && *((unsigned*)scan) == *((unsigned*)match)) {
      scan += 4;
      match += 4;
    }
  } else {
    /* do 8 checks at once per array bounds check. */
    while (scan < safe_end && *scan == *match && *++scan == *++match
           && *++scan == *++match && *++scan == *++match
           && *++scan == *++match && *++scan == *++match
           && *++scan == *++match && *++scan == *++match) {
      scan++; match++;
    }
  }
  /* The remaining few bytes. */
  while (scan != end && *scan == *match) {
    scan++; match++;
  }
  return scan;
#endif
}
