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

#include "lodepng/lodepng.h"
#include "zopflipng_lib.h"

int Zopflipng(int iterationnum, bool Metaremove, const char * Input, bool strict, int Mode, int best_filter) {
    ZopfliPNGOptions png_options;
    if (strict){png_options.lossy_transparent = false;}
    if (Mode == 2){
        png_options.chain_length = 256;
    }
    else{
        png_options.unlimited_blocksplitting = true;
        if (Mode == 3){
            png_options.chain_length = 600;
        }
        else if(Mode == 4){
            png_options.chain_length = 8192;
        }
        else{
            png_options.chain_length = 32768;
        }
    }
    png_options.num_iterations = iterationnum;
    png_options.keepchunks = Metaremove;
    std::vector<unsigned char> origpng;
    lodepng::load_file(origpng, Input);
    std::vector<unsigned char> resultpng;
    if (ZopfliPNGOptimize(origpng, png_options, &resultpng, best_filter, Mode, Input)) {return 2;}
    if (resultpng.size() >= origpng.size()) {return 1;}
    lodepng::save_file(resultpng, Input);
    return 0;
}
