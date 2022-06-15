//
//  gztools.cpp
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 08.05.15.
//  Copyright (c) 2015 Felix Hanau. All rights reserved.
//

#include "gztools.h"
#include "zlib/zlib.h"
#include "leanify/zip.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif

int ungz(const char * Infile, const char * Outfile){
    gzFile r = gzopen(Infile, "rb");
    if (!r){
        return 1;
    }
    FILE * stream = fopen (Outfile, "wb");
    if (!stream){
        return 1;
    }
    char buf [65536];
    int bytes;
    do {
        bytes = gzread(r, buf, sizeof(buf));
      if(bytes){
        fwrite(buf, 1, bytes, stream);
      }
      else if (bytes == 0) {
        break;
      }
      else if (bytes < 0) {
        printf("%s: ungzip error\n", Infile);
        gzclose_r(r);
        fclose(stream);
        unlink(Outfile);
        return 1;
      }
    }
    while (!gzeof(r));
    gzclose_r(r);
    fclose(stream);
  return 0;
}

int IsGzip(const char * Infile){
    FILE * stream = fopen (Infile, "rb");
    if (!stream){
        return 2;
    }
    char buf [4];
    if (fread(&buf, 1, 4, stream) != 4){
        return 2;
    }
    fclose(stream);
    if (buf[0] == 31 && buf[1] == -117){
      // FHCRC is stripped
      //Requires big endian
      if(buf[3] & 0x20){ //Encrypted
        printf("%s: File is encrypted, can't be optimized\n", Infile);
        return 2;
      }
      if(buf[3] & 0x1c){ //extra field, file name or comment
        return 3;
      }
      return 1;
    }
    return 0;
}

int IsZIP(const char * Infile){
  FILE * stream = fopen (Infile, "rb");
  if (!stream){
    return -1;
  }
  unsigned char buf [4];
  if (fread(&buf, 1, 4, stream) != 4){
    return -1;
  }
  fclose(stream);

  return memcmp(buf, Zip::header_magic, 4) == 0;
}
