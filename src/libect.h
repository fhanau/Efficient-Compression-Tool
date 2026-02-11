#ifndef LIBECT_H
#define LIBECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct {
	int compression_level;
	int strip;
	int jpeg_progressive_encoding;
	int jpeg_autorotate_mode;
	int jpeg_arithmetic_encoding;
	int keep_modification_time;
	int strict_losslessness;
	int png_reuse_filter_color_types;
	int png_allfilters;
	int png_allfilters_brute_force;
	int palette_sort;
    unsigned char deflate_threads;
    unsigned char file_threads;
} LibECTOptions;

void ect_init_options(LibECTOptions* options);
int ect_optimize_png(const char* filepath, const LibECTOptions* options);
int ect_optimize_jpeg(const char* filepath, const LibECTOptions* options);
int ect_optimize_file(const char* filepath, const LibECTOptions* options);

#ifdef __cplusplus
}
#endif

#endif // LIBECT_H
