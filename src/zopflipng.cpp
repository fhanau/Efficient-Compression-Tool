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
static void CountColors(std::unordered_set<unsigned>* unique, const unsigned char* image, unsigned w, unsigned h, bool transparent_counts_as_one) {
  unique->clear();
  unique->reserve(512);
  unsigned prev = ~*(unsigned*)(image);
  for (size_t i = 0; i < w * h; i++) {
    unsigned index = ColorIndex(&image[i * 4]);
    if (transparent_counts_as_one && image[i * 4 + 3] == 0) index = 0;
    if(prev!=index) {
      unique->insert(index);
    }
    prev = index;
    if (unique->size() > 256) break;
  }
}

// Remove RGB information from pixels with alpha=0
static void LossyOptimizeTransparent(lodepng::State* inputstate, unsigned char* image,
                                     unsigned w, unsigned h, int filter) {
  std::unordered_set<unsigned> count;  // Color count, up to 257.

  // If true, means palette is possible so avoid using different RGB values for
  // the transparent color.
  CountColors(&count, image, w, h, true);
  unsigned long colors = count.size();
  bool palette = colors <= 256 && w * h < colors * 2;

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

  if (!palette && !key) {
    int pre = 0, pgr = 0, pbl = 0;

    if (!filter){
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
  else {
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
      if (!palette && !key && i % w == 0){
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
  // For very small output, also try without palette, it may be smaller thanks
  // to no palette storage overhead.

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

  unsigned long testboth = out->size();
  if (!error && testboth < 3800 && w * h < 100000 && best_filter != 6 && state.out_mode.colortype == LCT_PALETTE) {
    LodePNGColorMode& color = state.out_mode;
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

      else if (has_alpha) {
        state.info_png.color.colortype = LCT_RGBA;
      }
      else{
        state.info_png.color.colortype = LCT_RGB;
      }
      error = lodepng::encode(out2, image, imagesize, w, h, state, p);
      if (out2.size() < out->size()){
        out->swap(out2);
      }

    }
  }
  if (error) {
    printf("Encoding error %u: %s\n", error, lodepng_error_text(error));
    return error;
  }
  if(best_filter != 6){
    lodepng_color_mode_cleanup(&state.out_mode);
  }
  return 0;
}

static unsigned ZopfliPNGOptimize(const std::vector<unsigned char>& origpng, const ZopfliPNGOptions& png_options, std::vector<unsigned char>* resultpng, int best_filter,
                                  std::vector<unsigned char> filters, unsigned palette_filter) {
  unsigned char* image = 0;
  size_t imagesize = 0;
  unsigned w, h;
  lodepng::State inputstate;

  unsigned error = lodepng::decode(&image, imagesize, w, h, inputstate, &origpng[0], origpng.size());

  if (error) {
    printf("Decoding error %i: %s\n", error, lodepng_error_text(error));

    return error;
  }

  bool bit16 = false;  // Using 16-bit per channel raw image
  if (inputstate.info_png.color.bitdepth == 16 && !png_options.lossy_8bit) {
    // Decode as 16-bit
    free(image);
    image = 0;
    error = lodepng::decode(&image, imagesize, w, h, &origpng[0], origpng.size(), LCT_RGBA, 16);
    bit16 = true;
  }
  if (!error) {
    // If lossy_transparent, remove RGB information from pixels with alpha=0
    if (png_options.lossy_transparent && !bit16) {
      LossyOptimizeTransparent(&inputstate, image, w, h, best_filter < 5 ? best_filter : 1);
    }
  }
  std::vector<unsigned char> temp;
  error = TryOptimize(image, imagesize, w, h, bit16, inputstate, &png_options, &temp, best_filter, filters, palette_filter);
  free(image);
  image = 0;
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
  lodepng::load_file(origpng, Infile);
  if (filter == 6){
    lodepng::getFilterTypes(filters, origpng);
    if(!filters.size()){
      printf("Could not load PNG filters\n");
      return -1;
    }
  }
  std::vector<unsigned char> resultpng;
  if (ZopfliPNGOptimize(origpng, png_options, &resultpng, filter, filters, palette_filter)) {return -1;}
  if (resultpng.size() >= origpng.size()) {return 1;}
  lodepng::save_file(resultpng, Infile);
  return 0;
}
