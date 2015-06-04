/*
Copyright 2013 Google Inc. All Rights Reserved.

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

#include "deflate.h"
#include "zlib_container.h"
#include "util.h"
#include "../zopflipng/lodepng/lodepng.h"

void ZopfliZlibCompress(const ZopfliOptions* options,
                        const unsigned char* in, size_t insize,
                        unsigned char** out, size_t* outsize) {
  unsigned char bitpointer = 0;
  unsigned checksum = adler32(in, (unsigned)insize);
  unsigned cmf = 120;  /* CM 8, CINFO 7. See zlib spec.*/
  unsigned flevel = 0;
  unsigned fdict = 0;
  unsigned cmfflg = 256 * cmf + fdict * 32 + flevel * 64;
  unsigned fcheck = 31 - cmfflg % 31;
  cmfflg += fcheck;

  ZOPFLI_APPEND_DATA(cmfflg / 256, out, outsize);
  ZOPFLI_APPEND_DATA(cmfflg % 256, out, outsize);

  ZopfliDeflate(options, 1 /* final */,
                in, insize, &bitpointer, out, outsize);

  ZOPFLI_APPEND_DATA((checksum >> 24) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((checksum >> 16) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((checksum >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA(checksum % 256, out, outsize);
}
