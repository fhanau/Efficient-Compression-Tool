/*
LodePNG Utils

Copyright (c) 2005-2014 Lode Vandevenne

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

/*Modified by Felix Hanau to remove unused functions*/

#include "lodepng_util.h"

namespace lodepng {

unsigned getChunks(std::vector<std::string> names[3],
                   std::vector<std::vector<unsigned char> > chunks[3],
                   const std::vector<unsigned char>& png) {
  const unsigned char *chunk, *next, *end;
  end = &png.back() + 1;
  chunk = &png.front() + 8;

  int location = 0;

  while(chunk < end && end - chunk >= 8) {
    char type[5];
    lodepng_chunk_type(type, chunk);
    std::string name(type);
    if(name.size() != 4) return 1;

    next = lodepng_chunk_next_const(chunk, end);

    if(name == "IHDR") location = 0;
    else if(name == "PLTE") location = 1;
    else if(name == "IDAT") location = 2;
    else if(name == "IEND") break; /*anything after IEND is not part of the PNG or the 3 groups here.*/
    else {
      names[location].push_back(name);
      chunks[location].push_back(std::vector<unsigned char>(chunk, next));
    }

    chunk = next;
  }
  return 0;
}


unsigned insertChunks(std::vector<unsigned char>& png,
                      const std::vector<std::vector<unsigned char> > chunks[3]) {
  const unsigned char *chunk, *begin, *end;
  end = &png.back() + 1;
  begin = chunk = &png.front() + 8;

  long l0 = 0; /*location 0: IHDR-l0-PLTE (or IHDR-l0-l1-IDAT)*/
  long l1 = 0; /*location 1: PLTE-l1-IDAT (or IHDR-l0-l1-IDAT)*/
  long l2 = 0; /*location 2: IDAT-l2-IEND*/

  while(chunk < end && end - chunk >= 8) {
    char type[5];
    lodepng_chunk_type(type, chunk);
    std::string name(type);
    if(name.size() != 4) return 1;

    if(name == "PLTE" && l0 == 0) l0 = chunk - begin + 8;
    else if(name == "IDAT")
    {
      if(l0 == 0) l0 = chunk - begin + 8;
      if(l1 == 0) l1 = chunk - begin + 8;
    }
    else if(name == "IEND" && l2 == 0) l2 = chunk - begin + 8;

    chunk = lodepng_chunk_next_const(chunk, end);
  }

  std::vector<unsigned char> result;
  result.insert(result.end(), png.begin(), png.begin() + l0);
  for(size_t i = 0; i < chunks[0].size(); i++) result.insert(result.end(), chunks[0][i].begin(), chunks[0][i].end());
  result.insert(result.end(), png.begin() + l0, png.begin() + l1);
  for(size_t i = 0; i < chunks[1].size(); i++) result.insert(result.end(), chunks[1][i].begin(), chunks[1][i].end());
  result.insert(result.end(), png.begin() + l1, png.begin() + l2);
  for(size_t i = 0; i < chunks[2].size(); i++) result.insert(result.end(), chunks[2][i].begin(), chunks[2][i].end());
  result.insert(result.end(), png.begin() + l2, png.end());

  png = result;
  return 0;
}

#include <stdlib.h>
static unsigned decompress(std::vector<unsigned char>& out, const unsigned char* in, size_t insize) {
  unsigned char* buffer = 0;
  size_t buffersize = 0;
  unsigned error = lodepng_zlib_decompress(&buffer, &buffersize, in, insize);
  if(buffer) {
    out.insert(out.end(), &buffer[0], &buffer[buffersize]);
    free(buffer);
  }
  return error;
}

static unsigned getFilterTypesInterlaced(std::vector<std::vector<unsigned char> >& filterTypes,
                                  const std::vector<unsigned char>& png) {
  /*Get color type and interlace type*/
  lodepng::State state;
  unsigned w, h;
  unsigned error;
  error = lodepng_inspect(&w, &h, &state, png.empty() ? 0 : &png[0], png.size());

  if(error) return 1;

  /*Read literal data from all IDAT chunks*/
  const unsigned char *chunk, *begin, *end;
  end = &png.back() + 1;
  begin = chunk = &png.front() + 8;

  std::vector<unsigned char> zdata;

  while(chunk < end && end - chunk >= 8) {
    char type[5];
    lodepng_chunk_type(type, chunk);
    if(std::string(type).size() != 4) break; /*Probably not a PNG file*/

    if(std::string(type) == "IDAT") {
      const unsigned char* cdata = lodepng_chunk_data_const(chunk);
      unsigned clength = lodepng_chunk_length(chunk);
      if(chunk + clength + 12 > end || clength > png.size() || chunk + clength + 12 < begin) return 1; /*corrupt chunk length*/

      for(unsigned i = 0; i < clength; i++) zdata.push_back(cdata[i]);
    }

    chunk = lodepng_chunk_next_const(chunk, end);
  }

  /*Decompress all IDAT data*/
  std::vector<unsigned char> data;
  error = lodepng::decompress(data, zdata.empty() ? 0 : &zdata[0], zdata.size());

  if(error) return 1;

  if(state.info_png.interlace_method == 0) {
    filterTypes.resize(1);

    /*A line is 1 filter byte + all pixels*/
    size_t linebytes = 1 + lodepng_get_raw_size(w, 1, &state.info_png.color);

    for(size_t i = 0; i < data.size(); i += linebytes) filterTypes[0].push_back(data[i]);
  } else {
    /*Interlaced*/
    filterTypes.resize(7);
    static const unsigned ADAM7_IX[7] = { 0, 4, 0, 2, 0, 1, 0 }; /*x start values*/
    static const unsigned ADAM7_IY[7] = { 0, 0, 4, 0, 2, 0, 1 }; /*y start values*/
    static const unsigned ADAM7_DX[7] = { 8, 8, 4, 4, 2, 2, 1 }; /*x delta values*/
    static const unsigned ADAM7_DY[7] = { 8, 8, 8, 4, 4, 2, 2 }; /*y delta values*/
    size_t pos = 0;
    for(size_t j = 0; j < 7; j++) {
      unsigned w2 = (w - ADAM7_IX[j] + ADAM7_DX[j] - 1) / ADAM7_DX[j];
      unsigned h2 = (h - ADAM7_IY[j] + ADAM7_DY[j] - 1) / ADAM7_DY[j];
      if(ADAM7_IX[j] >= w) w2 = 0;
      if(ADAM7_IY[j] >= h) h2 = 0;
      size_t linebytes = 1 + lodepng_get_raw_size(w2, 1, &state.info_png.color);
      for(size_t i = 0; i < h2; i++) {
        filterTypes[j].push_back(data[pos]);
        pos += linebytes;
      }
    }
  }
  return 0; /* OK */
}


unsigned getFilterTypes(std::vector<unsigned char>& filterTypes, const std::vector<unsigned char>& png) {
  std::vector<std::vector<unsigned char> > passes;
  unsigned error = getFilterTypesInterlaced(passes, png);
  if(error) return error;

  if(passes.size() == 1) {
    filterTypes.swap(passes[0]);
  } else {
    /* Simplify interlaced filter types to get a single filter value per scanline:
       put pass 6 and 7 alternating in the one vector, these filters
       correspond to the closest to what it would be for non-interlaced
       image. If the image is only 1 pixel wide, pass 6 doesn't exist so the
       alternative values column0 are used. The shift values are to match
       the y position in the interlaced sub-images.
       NOTE: the values 0-6 match Adam7's passes 1-7. */
    const unsigned column0[8] = {0, 6, 4, 6, 2, 6, 4, 6};
    const unsigned column1[8] = {5, 6, 5, 6, 5, 6, 5, 6};
    const unsigned shift0[8] = {3, 1, 2, 1, 3, 1, 2, 1};
    const unsigned shift1[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    lodepng::State state;
    unsigned w, h;
    lodepng_inspect(&w, &h, &state, png.empty() ? 0 : &png[0], png.size());
    const unsigned* column = w > 1 ? column1 : column0;
    const unsigned* shift = w > 1 ? shift1 : shift0;
    for(size_t i = 0; i < h; i++) {
      filterTypes.push_back(passes[column[i & 7u]][i >> shift[i & 7u]]);
    }
  }
  return 0; /* OK */
}

} /*namespace lodepng*/
