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

#include <cstdio>
#include <sys/stat.h>

#include "util.h"
#include "zopfli.h"
#include "../zlib/zlib.h"
#include "deflate.h"
#include "zopfli.h"
#include "zlib_container.h"
#include "../main.h"

/*
Compresses the data according to the gzip specification.
*/
static void ZopfliGzipCompress(const ZopfliOptions* options,
                        const unsigned char* in, size_t insize,  time_t time,
                        unsigned char** out, size_t* outsize) {
  unsigned crcvalue = crc32(0, in, insize);
  unsigned char bp = 0;

  (*out) = (unsigned char*)malloc(20);
  (*out)[*outsize] = 31; (*outsize)++;  /* ID1 */
  (*out)[*outsize] = 139; (*outsize)++; /* ID2 */
  (*out)[*outsize] = 8; (*outsize)++;   /* CM  */
  (*out)[*outsize] = 0; (*outsize)++;   /* FLG */

  /* MTIME */
  *(unsigned*)(&(*out)[*outsize]) = time & UINT_MAX; (*outsize) += 4;

  (*out)[*outsize] = 2; (*outsize)++;  /* XFL, 2 indicates best compression. */
  (*out)[*outsize] = 3; (*outsize)++;  /* OS follows Unix conventions. */

  ZopfliDeflate(options, 1, in, insize, &bp, out, outsize);
  (*out) = (unsigned char*)realloc(*out, *outsize + 8);

  /* CRC */
  *(unsigned*)(&(*out)[*outsize]) = crcvalue; (*outsize) += 4;

  /* ISIZE */
  *(unsigned*)(&(*out)[*outsize]) = insize % UINT_MAX; (*outsize) += 4;
}

static void ZopfliCompress(const ZopfliOptions* options, ZopfliFormat output_type,
                    const unsigned char* in, size_t insize, time_t time,
                    unsigned char** out, size_t* outsize) {
  if (output_type == ZOPFLI_FORMAT_GZIP) {
    ZopfliGzipCompress(options, in, insize, time, out, outsize);
  } else if (output_type == ZOPFLI_FORMAT_ZLIB) {
    //ZopfliZlibCompress(options, in, insize, out, outsize);
  } else if (output_type == ZOPFLI_FORMAT_DEFLATE) {
    unsigned char bp = 0;
    ZopfliDeflate(options, 1, in, insize, &bp, out, outsize);
  }
}

/*
 Loads a file into a memory array.
 */
static void LoadFile(const char* filename,
                     unsigned char** out, long long* outsize) {
  FILE* file = fopen(filename, "rb");
  if (!file) return;

  fseek(file , 0 , SEEK_END);
  *outsize = ftell(file);
  rewind(file);

  *out = (unsigned char*)malloc(*outsize);
  if (!*out && *outsize){
    exit(1);
  }
  if (*outsize) {
    size_t testsize = fread(*out, 1, *outsize, file);
    if (testsize != *outsize) {
      /* It could be a directory */
      free(*out);
      *out = 0;
      *outsize = -1;
    }
  }

  fclose(file);
}

/*
 Saves a file from a memory array, overwriting the file if it existed.
 */
static void SaveFile(const char* filename,
                     const unsigned char* in, size_t insize) {
  FILE* file = fopen(filename, "wb");
  if (!file){
    printf ("Can't write to file");
  }
  else {
    fwrite((char*)in, 1, insize, file);
    fclose(file);
  }
}

/*
 outfilename: filename to write output to, or 0 to write to stdout instead
 */
static void CompressFile(const ZopfliOptions* options,
                         ZopfliFormat output_type,
                         const char* infilename,
                         const char* outfilename) {
  unsigned char* in;
  long long insize = -1;
  unsigned char* out = 0;
  size_t outsize = 0;
  LoadFile(infilename, &in, &insize);
  if (insize < 0) {
    fprintf(stderr, "Invalid filename: %s\n", infilename);
    return;
  }

  struct stat st;
  stat(infilename, &st);
  time_t time = st.st_mtime;
  ZopfliCompress(options, output_type, in, insize, time, &out, &outsize);
  free(in);

  SaveFile(outfilename, out, outsize);

  free(out);
}

int ZopfliGzip(const char* filename, const char* outname, unsigned mode, unsigned multithreading) {
  ZopfliOptions options;
  //ZopfliFormat output_type = ZOPFLI_FORMAT_GZIP;
  //output_type = ZOPFLI_FORMAT_ZLIB;
  //output_type = ZOPFLI_FORMAT_DEFLATE;

  ZopfliInitOptions(&options, mode, multithreading, 0);
  //Append ".gz" ".zlib" ".deflate"

  CompressFile(&options, ZOPFLI_FORMAT_GZIP, filename, outname ? outname : ((std::string)filename).append(".gz").c_str());
  return 0;
}
