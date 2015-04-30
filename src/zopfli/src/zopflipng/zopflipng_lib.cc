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

// See zopflipng_lib.h

/*Modified by Felix Hanau*/

#include "zopflipng_lib.h"

#include <cstdio>
#include <set>
#include <vector>

#include "lodepng/lodepng.h"
#include "lodepng/lodepng_util.h"
#include "../zopfli/deflate.h"

ZopfliPNGOptions::ZopfliPNGOptions()
  : lossy_transparent(true)
  , lossy_8bit(false)
  , keepchunks(false)
  , num_iterations(15)
  {
}

// Deflate compressor passed as fuction pointer to LodePNG to have it use Zopfli
// as its compression backend.
unsigned CustomPNGDeflate(unsigned char** out, size_t* outsize, const unsigned char* in, size_t insize, const LodePNGCompressSettings* settings) {
  const ZopfliPNGOptions* png_options = static_cast<const ZopfliPNGOptions*>(settings->custom_context);
  unsigned char bp = 0;
  ZopfliOptions options;
  ZopfliInitOptions(&options);
    
    if (png_options->unlimited_blocksplitting){
        options.blocksplittingmax = 0;
    }
    options.numiterations = png_options->num_iterations;
    options.chain_length = png_options->chain_length;
    ZopfliDeflate(&options, 1, in, insize, &bp, out, outsize);
  return 0;  // OK
}

// Returns 32-bit integer value for RGBA color.
static unsigned ColorIndex(const unsigned char* color) {
  return color[0] + 256u * color[1] + 65536u * color[2] + 16777216u * color[3];
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
    if (unique->size() > 256) break;
  }
}

// Remove RGB information from pixels with alpha=0
static void LossyOptimizeTransparent(lodepng::State* inputstate, unsigned char* image,
    unsigned w, unsigned h) {
  // First check if we want to preserve potential color-key background color,
  // or instead use the last encountered RGB value all the time to save bytes.
  bool key = true;
  for (size_t i = 0; i < w * h; i++) {
    if (image[i * 4 + 3] > 0 && image[i * 4 + 3] < 255) {
      key = false;
      break;
    }
  }
  std::set<unsigned> count;  // Color count, up to 257.
  CountColors(&count, image, w, h, true);
  // If true, means palette is possible so avoid using different RGB values for
  // the transparent color.
  bool palette = count.size() <= 256;

  // Choose the color key or first initial background color.
  int r = 0, g = 0, b = 0;
  if (key || palette) {
    for (size_t i = 0; i < w * h; i++) {
      if (image[i * 4 + 3] == 0) {
        // Use RGB value of first encountered transparent pixel. This can be
        // used as a valid color key, or in case of palette ensures a color
        // existing in the input image palette is used.
        r = image[i * 4 + 0];
        g = image[i * 4 + 1];
        b = image[i * 4 + 2];
      }
    }
  }

  for (size_t i = 0; i < w * h; i++) {
    // if alpha is 0, alter the RGB value to a possibly more efficient one.
    if (image[i * 4 + 3] == 0) {
      image[i * 4 + 0] = r;
      image[i * 4 + 1] = g;
      image[i * 4 + 2] = b;
    } else {
      if (!key && !palette) {
        // Use the last encountered RGB value if no key or palette is used: that
        // way more values can be 0 thanks to the PNG filter types.
        r = image[i * 4 + 0];
        g = image[i * 4 + 1];
        b = image[i * 4 + 2];
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
          palette_out.push_back(palette_in[i * 4 + 0]);
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
static unsigned TryOptimize(
    const std::vector<unsigned char>& image, const std::vector<unsigned char>& origfile,unsigned w, unsigned h, const lodepng::State& inputstate, bool bit16, const ZopfliPNGOptions* png_options, std::vector<unsigned char>* out
                            /*, int best_filter*/) {
    lodepng::State state;
    state.encoder.zlibsettings.custom_deflate = CustomPNGDeflate;
    state.encoder.zlibsettings.custom_context = png_options;
    state.encoder.filter_palette_zero = 0;
    state.encoder.add_id = false;
    state.encoder.text_compression = 1;
    if (inputstate.info_png.color.colortype == LCT_PALETTE) {
        // Make it preserve the original palette order
        lodepng_color_mode_copy(&state.info_raw, &inputstate.info_png.color);
        state.info_raw.colortype = LCT_RGBA;
        state.info_raw.bitdepth = 8;
    }
    if (bit16) {
        state.info_raw.bitdepth = 16;
    }


  std::vector<unsigned char> filters;
    lodepng::getFilterTypes(filters, origfile);
    state.encoder.filter_strategy = LFS_PREDEFINED;
    state.encoder.predefined_filters = &filters[0];
    /*if (best_filter == 0)
    {state.encoder.filter_strategy = LFS_ZERO;}
    else if (best_filter== 5)
    {state.encoder.filter_strategy = LFS_MINSUM;}
    else {
        std::vector<unsigned char> filters;
        filters.resize(h, best_filter);
        state.encoder.filter_strategy = LFS_PREDEFINED;
        state.encoder.predefined_filters = &filters[0];
    }*/




    /*if (state.info_png.color.colortype == LCT_PALETTE){
        //const char * x=state.info_png.color.palette;
        unsigned char * pal = state.info_png.color.palette;
        lodepng_palette_clear(&state.info_png.color);
        std::vector<int> bigga;
        for (size_t x = 0; x < state.info_png.color.palettesize; x++){
            int v=x*4;
            bigga[x]=pal[v]+pal[v+1]+pal[v+2];
        }
        for(size_t f = 0; f < state.info_png.color.palettesize; f++){
        int d=0;
        int detwelve = -1;
        for(size_t y = 0; y < state.info_png.color.palettesize; y++){
            if (bigga[y]>d){
                d = bigga[y];
                detwelve = y;}
        }
        bigga[detwelve]=-1;
        //if (detwelve==-1){
        //    break;
        //}
            int t=d*4;
            lodepng_palette_add(&state.info_png.color, pal[t], pal[t+1], pal[t+2], pal[t+3]);
            //int v=f*4;
            //pal2[v]=pal[t];
            //pal2[v+1]=pal[t+1];
            //pal2[v+2]=pal[t+2];
            //pal2[v+3]=pal[t+4];
        }
        //state.info_png.color.palette = pal2;
    }*/




    unsigned error = lodepng::encode(*out, image, w, h, state);
    // For very small output, also try without palette, it may be smaller thanks
    // to no palette storage overhead.
    unsigned long testboth = out->size();
    if (!error && testboth < 3328) {
        lodepng::State teststate;
        std::vector<unsigned char> temp;
        lodepng::decode(temp, w, h, teststate, *out);
        LodePNGColorMode& color = teststate.info_png.color;
        if (color.colortype == LCT_PALETTE && (testboth < 2048 || color.palettesize>192) && (testboth < 1024 || color.palettesize>64) && (testboth < 512 || color.palettesize>40) && (testboth < 300 || color.palettesize>9))
        {
            //Todo:
            //Call zlib chain here again to get better filter and always force palette in zlib toolchain in other cases?
            std::vector<unsigned char> out2;
            state.encoder.auto_convert = 0;
            bool grey = true;
            unsigned has_alpha=lodepng_has_palette_alpha(&color);
            for (size_t i = 0; i < color.palettesize; i++) {
                if (color.palette[i * 4 + 0] != color.palette[i * 4 + 2]
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
        if (out2.size() < out->size()) out->swap(out2);

        }
    }
  if (error) {
    printf("Encoding error %u: %s\n", error, lodepng_error_text(error));
    return error;
  }
  return 0;
}

unsigned ZopfliPNGOptimize(const std::vector<unsigned char>& origpng,
    const ZopfliPNGOptions& png_options,
    std::vector<unsigned char>* resultpng, int best_filter
                           ) {
  std::vector<unsigned char> image;
  unsigned w, h;
  unsigned error;
  lodepng::State inputstate;

  error = lodepng::decode(image, w, h, inputstate, origpng);

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
        LossyOptimizeTransparent(&inputstate, &image[0], w, h);
    }
  }
    std::vector<unsigned char> temp;
    error = TryOptimize(image, origpng, w, h, inputstate, bit16, &png_options, &temp/*, best_filter*/);
    if (!error) {
        (*resultpng).swap(temp);  // Store best result so far in the output.
    }
    if (png_options.keepchunks==false) {
        std::vector<std::string> names[3];
        std::vector<std::vector<unsigned char> > chunks[3];
        lodepng::getChunks(names, chunks, origpng);
        lodepng::insertChunks(*resultpng, chunks);
    }
    return error;
}
