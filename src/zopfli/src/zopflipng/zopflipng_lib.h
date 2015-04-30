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

// Library to recompress and optimize PNG images. Uses Zopfli as the compression
// backend, chooses optimal PNG color model, and tries out several PNG filter
// strategies.

/*Modified by Felix Hanau.*/

#ifndef ZOPFLIPNG_LIB_H_
#define ZOPFLIPNG_LIB_H_

#include <string>
#include <vector>

struct ZopfliPNGOptions {
  ZopfliPNGOptions();

  // Allow altering hidden colors of fully transparent pixels
  bool lossy_transparent;
  // Convert 16-bit per channel images to 8-bit per channel
  bool lossy_8bit;

  // keep PNG chunks
  bool keepchunks;

  bool unlimited_blocksplitting;

  // Zopfli number of iterations
  int num_iterations;

  int chain_length;
};

unsigned ZopfliPNGOptimize(const std::vector<unsigned char>& origpng, const ZopfliPNGOptions& png_options, std::vector<unsigned char>* resultpng, int best_filter);

#endif  // ZOPFLIPNG_LIB_H_
