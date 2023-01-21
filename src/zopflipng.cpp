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
#include <cassert>
#include <set>
#include <unordered_set>
#include <vector>
#include <string>

#include "lodepng/lodepng_util.h"
#include "zopfli/deflate.h"
#include "main.h"
#include "lodepng/lodepng.h"

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

  unsigned quiet;
};

ZopfliPNGOptions::ZopfliPNGOptions()
: lossy_transparent(true)
, lossy_8bit(false)
, strip(false)
{
}

// Deflate compressor passed as function pointer to LodePNG to have it use Zopfli
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
static void CountColors(std::unordered_set<unsigned>* unique, const unsigned char* image, unsigned w, unsigned h) {
  unique->reserve(512);
  unsigned prev = ColorIndex(image) + 1;
  for (size_t i = 0; i < w * h; i++) {
    unsigned index = ColorIndex(&image[i * 4]);
    if (image[i * 4 + 3] == 0) index = 0;
    if(prev != index) {
      unique->insert(index);
      prev = index;
      if (unique->size() > 256) break;
    }
  }
}

static unsigned FindUnusedColor(unsigned char* image, unsigned w, unsigned h) {
  std::set<unsigned> opaque;
  unsigned prev = 0xFFFFFFFF;
   for (size_t i = 0; i < w * h; i++) {
     if(image[i * 4 + 3] == 0) {continue;}
     unsigned index = ColorIndex(&image[i * 4]) & 0xFFFFFF;
     if(prev != index) {
       opaque.insert(index);
       prev = index;
     }
   }

   // Try greyscale colors first, might also be easier to compress
   for (unsigned i = 0; i <= 0xFFFFFF; i += 0x010101) {
     if (!opaque.count(i)) {
       return i;
     }
   }

   // Find first unused color
   unsigned expected = 0;
   for(unsigned p : opaque) {
     if (p != expected) {
       return expected;
     }
     expected++;
   }

   // only reachable if every possible color is used, forego key in that case
   return 0xFF000000;
}

// Remove RGB information from pixels with alpha=0
static void LossyOptimizeTransparent(lodepng::State* inputstate, unsigned char* image,
                                     unsigned w, unsigned h, int filter) {
  std::unordered_set<unsigned> count;  // Color count, up to 257.

  // If true, means palette is possible so avoid using different RGB values for
  // the transparent color.
  CountColors(&count, image, w, h);
  unsigned long colors = count.size();
  bool palette_possible = colors <= 256;
  if(12 + colors * 4 > w * h / 2) palette_possible = false;

  // First check if we want to preserve potential color-key background color,
  // or instead use the last encountered RGB value all the time to save bytes.
  bool key_possible = true;
  for (size_t i = 0; i < w * h; i++) {
    if (image[i * 4 + 3] > 0 && image[i * 4 + 3] < 255) {
      key_possible = false;
      break;
    }
  }

  unsigned unused_color = 0xFF000000;
  if (key_possible && (unused_color = FindUnusedColor(image, w, h)) != 0xFF000000) {
    unsigned char* uc = (unsigned char*)&unused_color;
    unsigned char r = uc[0];
    unsigned char g = uc[1];
    unsigned char b = uc[2];

    for (size_t i = 0; i < w * h; i++) {
      if (image[i * 4 + 3] == 0) {
        image[i * 4] = r;
        image[i * 4 + 1] = g;
        image[i * 4 + 2] = b;
      }
    }
  } else {
    int pre = 0, pgr = 0, pbl = 0;

    if (!filter || palette_possible){
      for (size_t i = 0; i < w * h; i++) {
        // if alpha is 0, alter the RGB values to 0.
        if (image[i * 4 + 3] == 0) {
          image[i * 4] = 0;
          image[i * 4 + 1] = 0;
          image[i * 4 + 2] = 0;
        }
      }
    }
    else if (filter == 1){
      for (size_t i = 0; i < ((w << 2) * h); i += (w << 2)) {
        for (size_t j = 3; j < (w << 2); j += 4) {
          // if alpha is 0, set the RGB values to those of the pixel on the
          // left.
          if (image[i + j] == 0) {
            image[i + j - 3] = pre;
            image[i + j - 2] = pgr;
            image[i + j - 1] = pbl;
          } else {
            // Use the last encountered RGB value.
            pre = image[i + j - 3];
            pgr = image[i + j - 2];
            pbl = image[i + j - 1];
          }
        }
        if (w > 1) {
          for (size_t j = ((w - 2) << 2) + 3; j + 1 > 0; j -= 4) {
            // if alpha is 0, set the RGB values to those of the pixel on the
            // right.
            if (image[i + j] == 0) {
              image[i + j - 3] = pre;
              image[i + j - 2] = pgr;
              image[i + j - 1] = pbl;
            } else {
              // Use the last encountered RGB value.
              pre = image[i + j - 3];
              pgr = image[i + j - 2];
              pbl = image[i + j - 1];
            }
          }
        }
        pre = pgr = pbl = 0;   // reset to zero at each new line
      }

    }
    else if (filter == 2){
      for (size_t j = 3; j < (w << 2); j += 4) {
        // if alpha is 0, set the RGB values to zero (black), first line only.
        if (image[j] == 0) {
          image[j - 3] = 0;
          image[j - 2] = 0;
          image[j - 1] = 0;
        }
      }
      if (h > 1) {
        for (size_t j = 3; j < (w << 2); j += 4) {
          for (size_t i = (w << 2); i < ((w << 2) * h); i += (w << 2)) {
            // if alpha is 0, set the RGB values to those of the upper pixel.
            if (image[i + j] == 0) {
              image[i + j - 3] = image[i + j - 3 - (w << 2)];
              image[i + j - 2] = image[i + j - 2 - (w << 2)];
              image[i + j - 1] = image[i + j - 1 - (w << 2)];
            }
          }
          for (size_t i = (w << 2) * (h - 2); i + (w << 2) > 0; i -= (w << 2)) {
            // if alpha is 0, set the RGB values to those of the lower pixel.
            if (image[i + j] == 0) {
              image[i + j - 3] = image[i + j - 3 + (w << 2)];
              image[i + j - 2] = image[i + j - 2 + (w << 2)];
              image[i + j - 1] = image[i + j - 1 + (w << 2)];
            }
          }
        }
      }
    }
    else if (filter == 3){
      for (size_t j = 3; j < (w << 2); j += 4) {
        // if alpha is 0, set the RGB values to the half of those of the pixel
        // on the left, first line only.
        if (image[j] == 0) {
          pre = pre >> 1;
          pgr = pgr >> 1;
          pbl = pbl >> 1;
          image[j - 3] = pre;
          image[j - 2] = pgr;
          image[j - 1] = pbl;
        } else {
          pre = image[j - 3];
          pgr = image[j - 2];
          pbl = image[j - 1];
        }
      }
      if (h > 1) {
        for (size_t i = (w << 2); i < ((w << 2) * h); i += (w << 2)) {
          pre = pgr = pbl = 0;   // reset to zero at each new line
          for (size_t j = 3; j < (w << 2); j += 4) {
            // if alpha is 0, set the RGB values to the half of the sum of the
            // pixel on the left and the upper pixel.
            if (image[i + j] == 0) {
              pre = (pre + (int)image[i + j - (3 + (w << 2))]) >> 1;
              pgr = (pgr + (int)image[i + j - (2 + (w << 2))]) >> 1;
              pbl = (pbl + (int)image[i + j - (1 + (w << 2))]) >> 1;
              image[i + j - 3] = pre;
              image[i + j - 2] = pgr;
              image[i + j - 1] = pbl;
            } else {
              pre = image[i + j - 3];
              pgr = image[i + j - 2];
              pbl = image[i + j - 1];
            }
          }
        }
      }
    }
    else if (filter == 4){
      for (size_t j = 3; j < (w << 2); j += 4) {  // First line (border effects)
        // if alpha is 0, alter the RGB value to a possibly more efficient one.
        if (image[j] == 0) {
          image[j - 3] = pre;
          image[j - 2] = pgr;
          image[j - 1] = pbl;
        } else {
          pre = image[j - 3];
          pgr = image[j - 2];
          pbl = image[j - 1];
        }
      }
      if (h > 1) {
        int a, b, c, pa, pb, pc, p;
        for (size_t i = (w << 2); i < ((w << 2) * h); i += (w << 2)) {
          pre = pgr = pbl = 0;   // reset to zero at each new line
          for (size_t j = 3; j < (w << 2); j += 4) {
            // if alpha is 0, set the RGB values to the Paeth predictor.
            if (image[i + j] == 0) {
              if (j != 3) {  // not in first column
                a = pre;
                b = (int)image[i + j - (3 + (w << 2))];
                c = (int)image[i + j - (7 + (w << 2))];
                p = b - c;
                pc = a - c;
                pa = abs(p);
                pb = abs(pc);
                pc = abs(p + pc);
                pre = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;

                a = pgr;
                b = (int)image[i + j - (2 + (w << 2))];
                c = (int)image[i + j - (6 + (w << 2))];
                p = b - c;
                pc = a - c;
                pa = abs(p);
                pb = abs(pc);
                pc = abs(p + pc);
                pgr = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;

                a = pbl;
                b = (int)image[i + j - (1 + (w << 2))];
                c = (int)image[i + j - (5 + (w << 2))];
                p = b - c;
                pc = a - c;
                pa = abs(p);
                pb = abs(pc);
                pc = abs(p + pc);
                pbl = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;

                image[i + j - 3] = pre;
                image[i + j - 2] = pgr;
                image[i + j - 1] = pbl;
              } else {
                // first column, set the RGB values to those of the upper pixel.
                pre = (int)image[i + j - (3 + (w << 2))];
                pgr = (int)image[i + j - (2 + (w << 2))];
                pbl = (int)image[i + j - (1 + (w << 2))];
                image[i + j - 3] = pre;
                image[i + j - 2] = pgr;
                image[i + j - 1] = pbl;
              }
            } else {
              pre = image[i + j - 3];
              pgr = image[i + j - 2];
              pbl = image[i + j - 1];
            }
          }
        }
      }
    }
  }
}

// Tries to optimize given a single PNG filter strategy.
// Returns 0 if ok, other value for error
static unsigned TryOptimize(unsigned char* image, size_t imagesize, unsigned w, unsigned h, bool bit16, const lodepng::State& inputstate,
                            const ZopfliPNGOptions* png_options, std::vector<unsigned char>* out, int best_filter, std::vector<unsigned char> filters, unsigned palette_filter) {
  lodepng::State state;
  state.encoder.zlibsettings.custom_deflate = CustomPNGDeflate;
  state.encoder.zlibsettings.custom_context = png_options;
  state.encoder.clean_alpha = png_options->lossy_transparent;
  state.encoder.quiet = png_options->quiet;

  ZopfliOptions dummyoptions;
  ZopfliInitOptions(&dummyoptions, png_options->Mode, 0, 0);
  state.encoder.filter_style = dummyoptions.filter_style;
  state.encoder.text_compression = 0;
  if (bit16) {
    state.info_raw.bitdepth = 16;
  }

  state.encoder.filter_strategy = (LodePNGFilterStrategy)best_filter;
  if (best_filter == 6)
  {
    state.encoder.predefined_filters = &filters[0];
    state.encoder.auto_convert = 0;
    lodepng_color_mode_copy(&state.info_png.color, &inputstate.info_png.color);
  }

  LodePNGPaletteSettings p;
  p.order = LPOS_NONE;
  state.div = png_options->Mode == 2 ? 6 : png_options->Mode < 8 ? 3 : 2;
  unsigned error = lodepng::encode(*out, image, imagesize, w, h, state, p);
  LodePNGColorMode ref_color;
  lodepng_color_mode_init(&ref_color);
  lodepng_color_mode_copy(&ref_color, &state.out_mode);

  // Try different ways to sort palette
  if (!error && state.out_mode.colortype == LCT_PALETTE && palette_filter && state.out_mode.palettesize > 1) {
    p._first = 1;
    std::vector<unsigned char> out2;
    unsigned tries = 0;
    for (int k4 = 0; k4 < 4; k4++){
      p.order = (LodePNGPaletteOrderStrategy)k4;
      for (int k3 = 0; k3 < 5; k3++){
        p.priority = (LodePNGPalettePriorityStrategy)k3;
        for (int k2 = 0; k2 < 3; k2++){
          p.trans = (LodePNGPaletteTransparencyStrategy)k2;
          if (!lodepng_can_have_alpha(&state.out_mode)){
            k2 = 3;
          }

          for (int k1 = 0; k1 < 2; k1++){
            p.direction = (LodePNGPaletteDirectionStrategy)k1;

            lodepng_color_mode_cleanup(&state.out_mode);
            p._first += ((tries + 1) == palette_filter) << 1;
            lodepng::encode(out2, image, imagesize, w, h, state, p);
            p._first = 0;

            if (out2.size() < out->size() && !state.note){
              out->swap(out2);
            }
            out2.clear();
            if (++tries == palette_filter){
              k1 = k2 = k3 = k4 = 5;
            }
          }
        }
      }
    }
  }

  // For very small output, also try without palette, it may be smaller thanks
  // to no palette storage overhead.
  unsigned long testboth = out->size();
  if (!error && testboth < 3800 && w * h < 100000 && best_filter != 6 && state.out_mode.colortype == LCT_PALETTE) {
    LodePNGColorMode& color = ref_color;
    unsigned ux = color.palettesize;
    int wh_ok = (ux + 2) * 390 + 370;
    int size_ok = (ux + 2) * 40;

    if ((wh_ok > w*h || ux > 170)
        && (size_ok > testboth || ux > 180)
        && (size_ok / 2 > testboth || ux < 24)
        && (png_options->Mode > 2 || (testboth < 3400 && w*h < 20000 &&(size_ok * 7/ 20 > testboth || ux < 64))))
    {
      std::vector<unsigned char> out2;
      state.encoder.auto_convert = 0;
      bool grey = true;
      unsigned has_alpha = lodepng_has_palette_alpha(&color);
      for (size_t i = 0; i < color.palettesize; i++) {
        if (color.palette[i * 4] != color.palette[i * 4 + 2]
            || color.palette[i * 4 + 1] != color.palette[i * 4 + 2]) {
          grey = false;
          break;
        }
      }
      if (grey) {
        state.info_png.color.colortype = has_alpha ? LCT_GREY_ALPHA : LCT_GREY;
      } else{
        state.info_png.color.colortype = has_alpha ? LCT_RGBA : LCT_RGB;
      }
      error = lodepng::encode(out2, image, imagesize, w, h, state, p);
      if (out2.size() < out->size()){
        out->swap(out2);
      }

    }
  }
  if (error) {
    return error;
  }
  if(best_filter != 6){
    lodepng_color_mode_cleanup(&state.out_mode);
  }
  return 0;
}

static unsigned ZopfliPNGOptimize(const char * Infile, const std::vector<unsigned char>& origpng, const ZopfliPNGOptions& png_options, std::vector<unsigned char>* resultpng, int best_filter,
                                  std::vector<unsigned char> filters, unsigned palette_filter) {
  unsigned char* image = 0;
  size_t imagesize = 0;
  unsigned w, h;
  lodepng::State inputstate;

  unsigned error = lodepng::decode(&image, imagesize, w, h, inputstate, &origpng[0], origpng.size());

  bool bit16 = false;  // Using 16-bit per channel raw image
  if (!error && inputstate.info_png.color.bitdepth == 16 && !png_options.lossy_8bit) {
    // Decode as 16-bit
    free(image);
    error = lodepng::decode(&image, imagesize, w, h, &origpng[0], origpng.size(), LCT_RGBA, 16);
    bit16 = true;
  }

  if (error) {
    fprintf(stderr, "%s decoding error %i: %s\n", Infile, error, lodepng_error_text(error));
    return error;
  }
  // If lossy_transparent, remove RGB information from pixels with alpha=0
  if (png_options.lossy_transparent && !bit16 && lodepng_can_have_alpha(&inputstate.info_png.color)) {
    LossyOptimizeTransparent(&inputstate, image, w, h, best_filter < 5 ? best_filter : 1);
  }

  error = TryOptimize(image, imagesize, w, h, bit16, inputstate, &png_options, resultpng, best_filter, filters, palette_filter);
  free(image);
  if (error) {
    fprintf(stderr, "%s encoding error %u: %s\n", Infile, error, lodepng_error_text(error));
    return error;
  }
  if (!png_options.strip) {
    std::vector<std::string> names[3];
    std::vector<std::vector<unsigned char> > chunks[3];
    lodepng::getChunks(names, chunks, origpng);
    lodepng::insertChunks(*resultpng, chunks);
  }
  return 0;
}

int Zopflipng(bool strip, const char * Infile, bool strict, unsigned Mode, int filter, unsigned multithreading, unsigned quiet) {
  ZopfliPNGOptions png_options;
  png_options.Mode = Mode;
  png_options.multithreading = multithreading;
  png_options.quiet = quiet;
  unsigned palette_filter = (filter & 0xFF00) >> 8;
  filter &= 0xFF;
  png_options.lossy_transparent = !strict && filter != 6;
  png_options.strip = strip;
  std::vector<unsigned char> origpng;

  std::vector<unsigned char> filters;
  unsigned error = lodepng::load_file(origpng, Infile);
  if (error) {
    fprintf(stderr, "Could not load PNG %s\n", Infile);
    return -1;
  }
  if (filter == 6){
    lodepng::getFilterTypes(filters, origpng);
    if(!filters.size()){
      fprintf(stderr, "%s: Could not load PNG filters\n", Infile);
      return -1;
    }
  }
  std::vector<unsigned char> resultpng;
  if (ZopfliPNGOptimize(Infile, origpng, png_options, &resultpng, filter, filters, palette_filter)) {return -1;}
  if (resultpng.size() >= origpng.size()) {return 1;}
  if (lodepng::save_file(resultpng, Infile) != 0) {
    fprintf(stderr, "Failed to write to file %s\n", Infile);
    return -1;
  }
  return 0;
}
