#include "libect.h"
#include "main.h"
#include <cstring>

extern "C" {

void ect_init_options(LibECTOptions* options) {
    memset(options, 0, sizeof(LibECTOptions));
    options->compression_level = 3;
}

static void ect_options_to_internal(const LibECTOptions* options, struct ECTOptions* internal) {
    internal->Mode = options->compression_level;
    internal->strip = options->strip;
    internal->Strict = options->strict_losslessness;
    internal->Reuse = options->png_reuse_filter_color_types;
    internal->Allfilters = options->png_allfilters;
    internal->Allfiltersbrute = options->png_allfilters_brute_force;
    internal->Allfilterscheap = 0;
    internal->palette_sort = options->palette_sort << 8;
    internal->Progressive = options->jpeg_progressive_encoding;
    internal->Autorotate = options->jpeg_autorotate_mode;
    internal->Arithmetic = options->arithmetic;
    internal->DeflateMultithreading = options->deflate_threads;
    internal->FileMultithreading = options->file_threads;
    internal->keep = options->keep_modification_time;
    internal->PNG_ACTIVE = true;
    internal->JPEG_ACTIVE = true;
    internal->Gzip = false;
    internal->Zip = false;
    internal->SavingsCounter = false;
    internal->Recurse = false;
}

int ect_optimize_png(const char* filepath, const LibECTOptions* options) {
    LibECTOptions default_options;
    if (!options) {
        ect_init_options(&default_options);
        options = &default_options;
    }

    struct ECTOptions internal_options;
    ect_options_to_internal(options, &internal_options);

    return OptimizePNG(filepath, internal_options);
}

int ect_optimize_jpeg(const char* filepath, const LibECTOptions* options) {
    LibECTOptions default_options;
    if (!options) {
        ect_init_options(&default_options);
        options = &default_options;
    }

    struct ECTOptions internal_options;
    ect_options_to_internal(options, &internal_options);

    return OptimizeJPEG(filepath, internal_options);
}

int ect_optimize_file(const char* filepath, const LibECTOptions* options) {
    LibECTOptions default_options;
    if (!options) {
        ect_init_options(&default_options);
        options = &default_options;
    }

    struct ECTOptions internal_options;
    ect_options_to_internal(options, &internal_options);

    return fileHandler(filepath, internal_options, 0);
}

} // extern "C"
