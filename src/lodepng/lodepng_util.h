/*
LodePNG Utils

Copyright (c) 2005-2022 Lode Vandevenne

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

/*
Extra C++ utilities for LodePNG, for convenience.
Not part of the stable API of lodepng, more loose separate utils.
*/

#ifndef LODEPNG_UTIL_H
#define LODEPNG_UTIL_H

#include <string>
#include <vector>
#include "lodepng.h"

#pragma once

namespace lodepng
{

/*
Inserts chunks into the given png file. The chunks must be fully encoded,
including length, type, content and CRC.
The array index determines where it goes:
0: between IHDR and PLTE, 1: between PLTE and IDAT, 2: between IDAT and IEND.
They're appended at the end of those locations within the PNG.
Returns 0 if ok, non-0 if error happened.
*/
unsigned insertChunks(std::vector<unsigned char>& png,
                    const std::vector<std::vector<unsigned char> > chunks[3]);

/*
Returns the names and full chunks (including the name and everything else that
makes up the chunk) for all chunks except IHDR, PLTE, IDAT and IEND.
It separates the chunks into 3 separate lists, representing the chunks between
certain critical chunks: 0: IHDR-PLTE, 1: PLTE-IDAT, 2: IDAT-IEND
Returns 0 if ok, non-0 if error happened.
*/
unsigned getChunks(std::vector<std::string> names[3],
                   std::vector<std::vector<unsigned char> > chunks[3],
                   const std::vector<unsigned char>& png);

unsigned getFilterTypes(std::vector<unsigned char>& filterTypes, const std::vector<unsigned char>& png);

} // namespace lodepng

#endif /*LODEPNG_UTIL_H inclusion guard*/
