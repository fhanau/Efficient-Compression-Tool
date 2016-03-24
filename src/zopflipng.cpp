// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: lode.vandevenne@gmail.com (Lode Vandevenne)
// Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)

// Command line tool to recompress and optimize PNG images, using zopflipng_lib.

/*Modified by Felix Hanau*/

#include <cstdio>
#include <set>
#include <vector>
#include <string>

#include "lodepng/lodepng_util.h"
#include "zopfli/deflate.h"
#include "main.h"
#include "lodepng/lodepng.h"
#include <assert.h>

struct ZopfliPNGOptions {
  ZopfliPNGOptions();

  unsigned Mode;

  // Allow altering hidden colors of fully transparent pixels
  bool lossy_transparent;

  // Convert 16-bit per channel images to 8-bit per channel
  bool lossy_8bit;

  // remove PNG chunks
  bool strip;

  //Use per block multithreading
  unsigned multithreading;

};

ZopfliPNGOptions::ZopfliPNGOptions()
: lossy_transparent(true)
, lossy_8bit(false)
, strip(false)
{
}

// Deflate compressor passed as fuction pointer to LodePNG to have it use Zopfli
// as its compression backend.
static unsigned CustomPNGDeflate(unsigned char** out, size_t* outsize, const unsigned char* in, size_t insize, const LodePNGCompressSettings* settings) {
  const ZopfliPNGOptions* png_options = static_cast<const ZopfliPNGOptions*>(settings->custom_context);
  unsigned char bp = 0;
  ZopfliOptions options;
  ZopfliInitOptions(&options, png_options->Mode, png_options->multithreading, 1);
  ZopfliDeflate(&options, 1, in, insize, &bp, out, outsize);
  return 0;
}

// Returns 32-bit integer value for RGBA color.
static unsigned ColorIndex(const unsigned char* color) {
  return color[0] + (color[1] << 8) + (color[2] << 16) + (color[3] << 24);
}

// Counts amount of colors in the image, up to 257. If transparent_counts_as_one
// is enabled, any color with alpha channel 0 is treated as a single color with
// index 0.
static void CountColors(std::set<unsigned>* unique, const unsigned char* image, unsigned w, unsigned h, bool transparent_counts_as_one) {
  unique->clear();
  for (size_t i = 0; i < w * h; i++) {
    unsigned index = ColorIndex(&image[i * 4]);
    if (transparent_counts_as_one && image[i * 4 + 3] == 0) index = 0;
    unique->insert(index);
    if (i>256){
      if (unique->size() > 256) break;
    }
  }
}

// Remove RGB information from pixels with alpha=0
static void LossyOptimizeTransparent(lodepng::State* inputstate, unsigned char* image,
                                     unsigned w, unsigned h, int filter) {
  //TODO: Only set "palette" when palette is actually used. Optimize for the filter actually used in the row. (Move into Lodepng?)
  //      There are big improvements possible here.

  std::set<unsigned> count;  // Color count, up to 257.

  // If true, means palette is possible so avoid using different RGB values for
  // the transparent color.
  bool palette;
  if(inputstate->info_png.color.colortype == LCT_PALETTE) {
    palette = w * h < inputstate->info_png.color.palettesize * 2;
  }
  else{
    CountColors(&count, image, w, h, true);
    unsigned long colors = count.size();
    palette = colors <= 256 && w * h < colors * 2;
  }

  if (!filter && !palette) {
    for (size_t i = 0; i < w * h; i++) {
      // if alpha is 0, alter the RGB values to 0.
      if (image[i * 4 + 3] == 0) {
        image[i * 4] = 0;
        image[i * 4 + 1] = 0;
        image[i * 4 + 2] = 0;
      }
    }
  }
  else {
    // First check if we want to preserve potential color-key background color,
    // or instead use the last encountered RGB value all the time to save bytes.
    bool key = true;
    // Makes no difference if palette
    if (!palette){
      for (size_t i = 0; i < w * h; i++) {
        if (image[i * 4 + 3] > 0 && image[i * 4 + 3] < 255) {
          key = false;
          break;
        }
      }
    }
    unsigned char r = 0, g = 0, b = 0;
    if (palette && !filter){
      // Use RGB value of first encountered pixel. This can be
      // used as a valid color key, or in case of palette ensures a color
      // existing in the input image palette is used.
      r = image[0];
      g = image[1];
      b = image[2];
    }
    else if (key || palette){
      for (size_t i = 0; i < w * h; i++) {
        if (image[i * 4 + 3] == 0) {
          // Use RGB value of first encountered transparent pixel. This can be
          // used as a valid color key, or in case of palette ensures a color
          // existing in the input image palette is used.
          r = image[i * 4];
          g = image[i * 4 + 1];
          b = image[i * 4 + 2];
          break;
        }
      }
    }
    for (size_t i = 0; i < w * h; i++) {
      // if alpha is 0, alter the RGB value to a possibly more efficient one.
      if (!palette && i % w == 0){
        r = 0;
        g = 0;
        b = 0;
      }
      if (image[i * 4 + 3] == 0) {
        image[i * 4] = r;
        image[i * 4 + 1] = g;
        image[i * 4 + 2] = b;
      }
      else {
        if (!key && !palette){
          // Use the last encountered RGB value if no key or palette is used: that
          // way more values can be 0 thanks to the PNG filter types.
          r = image[i * 4];
          g = image[i * 4 + 1];
          b = image[i * 4 + 2];
        }
      }
    }
  }

  // If there are now less colors, update palette of input image to match this.
  if (palette && inputstate->info_png.color.palettesize > 0) {
    CountColors(&count, image, w, h, false);
    if (count.size() < inputstate->info_png.color.palettesize) {
      std::vector<unsigned char> palette_out;
      unsigned char* palette_in = inputstate->info_png.color.palette;
      for (size_t i = 0; i < inputstate->info_png.color.palettesize; i++) {
        if (count.count(ColorIndex(&palette_in[i * 4])) != 0) {
          palette_out.push_back(palette_in[i * 4]);
          palette_out.push_back(palette_in[i * 4 + 1]);
          palette_out.push_back(palette_in[i * 4 + 2]);
          palette_out.push_back(palette_in[i * 4 + 3]);
        }
      }
      inputstate->info_png.color.palettesize = palette_out.size() / 4;
      for (size_t i = 0; i < palette_out.size(); i++) {
        palette_in[i] = palette_out[i];
      }
    }
  }
}

// Tries to optimize given a single PNG filter strategy.
// Returns 0 if ok, other value for error
static unsigned TryOptimize(std::vector<unsigned char>& image, unsigned w, unsigned h, bool bit16, const lodepng::State& inputstate,
                            const ZopfliPNGOptions* png_options, std::vector<unsigned char>* out, int best_filter, std::vector<unsigned char> filters) {
  lodepng::State state;
  state.encoder.zlibsettings.custom_deflate = CustomPNGDeflate;
  state.encoder.zlibsettings.custom_context = png_options;
  state.encoder.clean_alpha = png_options->lossy_transparent;

  ZopfliOptions dummyoptions;
  ZopfliInitOptions(&dummyoptions, png_options->Mode, 0, 0);
  state.encoder.filter_style = dummyoptions.filter_style;
  state.encoder.text_compression = 0;
  if (bit16) {
    state.info_raw.bitdepth = 16;
  }

  if (best_filter == 0)
  {state.encoder.filter_strategy = LFS_ZERO;}
  else if (best_filter == 1)
  {state.encoder.filter_strategy = LFS_SUB;}
  else if (best_filter == 2)
  {state.encoder.filter_strategy = LFS_UP;}
  else if (best_filter == 3)
  {state.encoder.filter_strategy = LFS_AVG;}
  else if (best_filter == 4)
  {state.encoder.filter_strategy = LFS_PAETH;}
  else if (best_filter == 5)
  {state.encoder.filter_strategy = LFS_BRUTE_FORCE;}
  else if (best_filter == 6)
  {state.encoder.filter_strategy = LFS_PREDEFINED;
    state.encoder.predefined_filters = &filters[0];
    state.encoder.auto_convert = 0;
    lodepng_color_mode_copy(&state.info_png.color, &inputstate.info_png.color);
  }

  //Palette sorting (Should be in seperate function). This is an untested experiment and likely wont improve compression.
  /*if (state.info_png.color.colortype == LCT_PALETTE){
   unsigned char * pal = state.info_png.color.palette;
   lodepng_palette_clear(&state.info_png.color);
   std::vector<int> sort;
   for (size_t x = 0; x < state.info_png.color.palettesize; x++){
   int v=x*4;
   sort[x]=pal[v]+pal[v+1]+pal[v+2];
   }
   for(size_t f = 0; f < state.info_png.color.palettesize; f++){
   int d=0;
   int highest = -1;
   for(size_t y = 0; y < state.info_png.color.palettesize; y++){
   if (sort[y]>d){
   d = sort[y];
   highest = y;}
   }
   sort[highest]=-1;
   int t=d*4;
   lodepng_palette_add(&state.info_png.color, pal[t], pal[t+1], pal[t+2], pal[t+3]);
   }
   }*/

  unsigned error = lodepng::encode(*out, image, w, h, state);
  // For very small output, also try without palette, it may be smaller thanks
  // to no palette storage overhead.
  unsigned long testboth = out->size();
  if (!error && testboth < 4096 && w * h < 45000 && best_filter != 6) {
    lodepng::State teststate;
    std::vector<unsigned char> temp;
    lodepng::decode(temp, w, h, teststate, *out);
    LodePNGColorMode& color = teststate.info_png.color;
    if (color.colortype == LCT_PALETTE && (testboth < 2048 || color.palettesize>192) && (testboth < 1024 || color.palettesize>64) && (testboth < 512 || color.palettesize>40) && (testboth < 300 || color.palettesize>9))
    {
      std::vector<unsigned char> out2;
      state.encoder.auto_convert = 0;
      bool grey = true;
      unsigned has_alpha=lodepng_has_palette_alpha(&color);
      for (size_t i = 0; i < color.palettesize; i++) {
        if (color.palette[i * 4] != color.palette[i * 4 + 2]
            || color.palette[i * 4 + 1] != color.palette[i * 4 + 2]) {
          grey = false;
          break;
        }
      }
      if (grey){
        if (has_alpha){state.info_png.color.colortype = LCT_GREY_ALPHA;}
        else{state.info_png.color.colortype = LCT_GREY;}
      }

      else{if (has_alpha){state.info_png.color.colortype = LCT_RGBA;}
      else{state.info_png.color.colortype = LCT_RGB;}
      }
      error = lodepng::encode(out2, image, w, h, state);
      if (out2.size() < out->size()){
        out->swap(out2);
      }

    }
  }
  if (error) {
    printf("Encoding error %u: %s\n", error, lodepng_error_text(error));
    return error;
  }
  return 0;
}

static unsigned ZopfliPNGOptimize(const std::vector<unsigned char>& origpng, const ZopfliPNGOptions& png_options, std::vector<unsigned char>* resultpng, int best_filter, std::vector<unsigned char> filters) {
  std::vector<unsigned char> image;
  unsigned w, h;
  lodepng::State inputstate;

  unsigned error = lodepng::decode(image, w, h, inputstate, origpng);

  if (error) {
    printf("Decoding error %i: %s\n", error, lodepng_error_text(error));

    return error;
  }

  bool bit16 = false;  // Using 16-bit per channel raw image
  if (inputstate.info_png.color.bitdepth == 16 && !png_options.lossy_8bit) {
    // Decode as 16-bit
    image.clear();
    error = lodepng::decode(image, w, h, origpng, LCT_RGBA, 16);
    bit16 = true;
  }
  if (!error) {
    // If lossy_transparent, remove RGB information from pixels with alpha=0
    if (png_options.lossy_transparent && !bit16) {
      LossyOptimizeTransparent(&inputstate, &image[0], w, h, best_filter);
    }
  }
  std::vector<unsigned char> temp;
  error = TryOptimize(image, w, h, bit16, inputstate, &png_options, &temp, best_filter, filters);
  if (!error) {
    (*resultpng).swap(temp);  // Store best result so far in the output.
  }
  if (!png_options.strip) {
    std::vector<std::string> names[3];
    std::vector<std::vector<unsigned char> > chunks[3];
    lodepng::getChunks(names, chunks, origpng);
    lodepng::insertChunks(*resultpng, chunks);
  }
  return error;
}

int Zopflipng(bool strip, const char * Infile, bool strict, unsigned Mode, int filter, unsigned multithreading) {
  ZopfliPNGOptions png_options;
  png_options.Mode = Mode;
  png_options.multithreading = multithreading;
  png_options.lossy_transparent = !strict && filter != 6;
  png_options.strip = strip;
  std::vector<unsigned char> origpng;

  std::vector<unsigned char> filters;
  lodepng::load_file(origpng, Infile);
  if (filter == 6){
    lodepng::getFilterTypes(filters, origpng);
    assert(filters.size());
  }
  std::vector<unsigned char> resultpng;
  if (ZopfliPNGOptimize(origpng, png_options, &resultpng, filter, filters)) {return -1;}
  if (resultpng.size() >= origpng.size()) {return 1;}
  lodepng::save_file(resultpng, Infile);
  return 0;
}
