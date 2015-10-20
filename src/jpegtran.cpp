/*
 * jpegtran.c
 *
 * This file was part of the Independent JPEG Group's software:
 * Copyright (C) 1995-2010, Thomas G. Lane, Guido Vollbeding.
 * libjpeg-turbo Modifications:
 * Copyright (C) 2010, 2014, D. R. Commander.
 * mozjpeg Modifications:
 * Copyright (C) 2014, Mozilla Corporation.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains a command-line user interface for JPEG transcoding.
 * It is very similar to cjpeg.c, and partly to djpeg.c, but provides
 * lossless transcoding between different JPEG file formats.  It also
 * provides some lossless and sort-of-lossless transformations of JPEG data.
 */

/* Modified by Felix Hanau. */

#define JPEG_INTERNAL_OPTIONS   /* cjpeg.c, djpeg.c need to see xxx_SUPPORTED */

#include "mozjpeg/jinclude.h"
#include "mozjpeg/jpeglib.h"
#include "main.h"

#define INPUT_BUF_SIZE  4096

static void jcopy_markers_execute (j_decompress_ptr srcinfo, j_compress_ptr dstinfo)
{
    for (jpeg_saved_marker_ptr marker = srcinfo->marker_list; marker; marker = marker->next) {
        if (dstinfo->write_JFIF_header &&
            marker->marker == JPEG_APP0 &&
            marker->data_length >= 5 &&
            GETJOCTET(marker->data[0]) == 0x4A &&
            GETJOCTET(marker->data[1]) == 0x46 &&
            GETJOCTET(marker->data[2]) == 0x49 &&
            GETJOCTET(marker->data[3]) == 0x46 &&
            GETJOCTET(marker->data[4]) == 0)
            continue;                 /* reject duplicate JFIF */
        if (dstinfo->write_Adobe_marker &&
            marker->marker == JPEG_APP0+14 &&
            marker->data_length >= 5 &&
            GETJOCTET(marker->data[0]) == 0x41 &&
            GETJOCTET(marker->data[1]) == 0x64 &&
            GETJOCTET(marker->data[2]) == 0x6F &&
            GETJOCTET(marker->data[3]) == 0x62 &&
            GETJOCTET(marker->data[4]) == 0x65)
            continue;                 /* reject duplicate Adobe */
        jpeg_write_marker(dstinfo, marker->marker, marker->data, marker->data_length);
    }
}

int mozjpegtran (bool arithmetic, bool progressive, bool strip, const char * Infile, const char * Outfile)
{
    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    struct jpeg_error_mgr jsrcerr, jdsterr;
    FILE * fp;
    unsigned char *inbuffer = 0;
    unsigned long insize = 0;
    unsigned char *outbuffer = 0;
    unsigned long outsize = 0;
    const char * progname = "ECT (JPEG)";
    /* Initialize the JPEG decompression object with default error handling. */
    srcinfo.err = jpeg_std_error(&jsrcerr);
    jpeg_create_decompress(&srcinfo);
    /* Initialize the JPEG compression object with default error handling. */
    dstinfo.err = jpeg_std_error(&jdsterr);
    jpeg_create_compress(&dstinfo);
    if (!progressive){
        jpeg_c_set_int_param(&dstinfo, JINT_COMPRESS_PROFILE, JCP_FASTEST);
    }

    /* Open the input file. */
    if (!(fp = fopen(Infile, "rb"))) {
        fprintf(stderr, "%s: can't open %s for reading\n", progname, Infile);
        return 2;
    }

    size_t nbytes;
    do {
        inbuffer = (unsigned char *)realloc(inbuffer, insize + INPUT_BUF_SIZE);
        if (!inbuffer) {
            fprintf(stderr, "%s: memory allocation failure\n", progname);
            exit(1);
      }
        nbytes = JFREAD(fp, &inbuffer[insize], INPUT_BUF_SIZE);
        if (nbytes < INPUT_BUF_SIZE && ferror(fp)) {
            fprintf(stderr, "%s: can't read from %s\n", progname, Infile);
        }
        insize += (unsigned long)nbytes;
    } while (nbytes == INPUT_BUF_SIZE);
    fclose(fp);

    jpeg_mem_src(&srcinfo, inbuffer, insize);

    /* Enable saving of extra markers that we want to copy */
    if (!strip) {
        jpeg_save_markers(&srcinfo, JPEG_COM, 0xFFFF);
        for (unsigned m = 0; m < 16; m++)
            jpeg_save_markers(&srcinfo, JPEG_APP0 + m, 0xFFFF);
    }

    /* Read file header */
    jpeg_read_header(&srcinfo, 1);

    /* Any space needed by a transform option must be requested before
       jpeg_read_coefficients so that memory allocation will be done right.
     */

    /* Read source file as DCT coefficients */
    jvirt_barray_ptr * coef_arrays = jpeg_read_coefficients(&srcinfo);

    /* Initialize destination compression parameters from source values */
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    /* Adjust default compression parameters by re-parsing the options */
    dstinfo.optimize_coding = !progressive && !arithmetic;
    dstinfo.arith_code = arithmetic;
    if (dstinfo.num_scans && progressive) {
        jpeg_simple_progression(&dstinfo);
    }
    else{
        dstinfo.num_scans = 0;
        dstinfo.scan_info = 0;
    }

    /* Specify data destination for compression */
    jpeg_mem_dest(&dstinfo, &outbuffer, &outsize);

    /* Start compressor (note no image data is actually written here) */
    jpeg_write_coefficients(&dstinfo, coef_arrays);

    /* Copy to the output file any extra markers that we want to preserve */
    jcopy_markers_execute(&srcinfo, &dstinfo);

    /* Finish compression and release memory */
    jpeg_finish_compress(&dstinfo);
    free(inbuffer);

    bool x = 0;

    if (insize < outsize || (progressive && insize == outsize)){
        x = 1;
    }

    /* Better file than before */
    else{
        /* Open the output file. */
        if (!(fp = fopen(Outfile, "wb"))) {
            fprintf(stderr, "%s: can't open %s for writing\n", progname, Outfile);
            free(outbuffer);
            return 2;
        }

        /* Write new file. */
        if (JFWRITE(fp, outbuffer, outsize) < outsize && ferror(fp)) {
            fprintf(stderr, "%s: can't write to %s\n", progname, Outfile);
        }
    }

    jpeg_destroy_compress(&dstinfo);
    jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_decompress(&srcinfo);
    fclose(fp);
    free(outbuffer);
    return x;
}
