//
//  zlibWrapper.cpp
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 30.05.15.
//  Copyright (c) 2015 Felix Hanau. All rights reserved.
//
#include "zlib/zlib.h"
#include "zlibWrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//TODO: BUFFER!

#define BUFFER_SIZE 8000000

//Based on zlib's compress2().
int zlibcompress (unsigned char **dest, size_t *destLen, const unsigned char * source, size_t sourceLen, int level, unsigned short chain_length)
{
    z_stream stream;
    stream.next_in = (z_const unsigned char *)source;
    stream.avail_in = sourceLen;
    stream.avail_out = UINT_MAX;
    stream.zalloc = NULL;
    stream.zfree = NULL;
    stream.opaque = NULL;

    int err = deflateInit2(&stream, level, Z_DEFLATED, -15, 8, Z_FILTERED);
    if (err != Z_OK) return err;

    deflateTune(&stream, 256, 258, 258, chain_length);
    unsigned char *buf = (unsigned char *)malloc(deflateBound(&stream, sourceLen));
    stream.next_out = buf;


    err = deflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        deflateEnd(&stream);
        return err == Z_OK ? Z_BUF_ERROR : err;
    }

    err = deflateEnd(&stream);
    *dest = buf;
    *destLen = stream.total_out;

    return err;
}

int zlibuncompress (unsigned char **dest, size_t *destLen, const unsigned char * source, size_t sourceLen){
    z_stream stream;

    stream.next_in = (z_const unsigned char *)source;
    stream.avail_in = sourceLen;
    stream.avail_out = BUFFER_SIZE;
    stream.zalloc = NULL;
    stream.zfree = NULL;
    // > -15 if wsize smaller (up to -9)
    int err = inflateInit2(&stream, -15);
    if (err != Z_OK) return err;
    unsigned char *buf = (unsigned char *)malloc(BUFFER_SIZE);
    stream.next_out = buf;

    while (err == Z_OK){
        err = inflate(&stream, Z_SYNC_FLUSH);
        memcpy(*dest,buf,BUFFER_SIZE - stream.avail_out);
        stream.avail_out = BUFFER_SIZE;
    }
    if (err != Z_STREAM_END) {
        inflateEnd(&stream);
        if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
            return Z_DATA_ERROR;
        return err;
    }

    err = inflateEnd(&stream);
    *dest = buf;
    *destLen = stream.total_out;
    return err;
}
