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

#define JPEG_INTERNAL_OPTIONS   /* cjpeg.c,djpeg.c need to see xxx_SUPPORTED */

#include "mozjpeg/jinclude.h"
#include "mozjpeg/jpeglib.h"

#define INPUT_BUF_SIZE  4096

static void jcopy_markers_setup (j_decompress_ptr srcinfo, bool copyoption)
{
    if (copyoption == false) {
        jpeg_save_markers(srcinfo, JPEG_COM, 0xFFFF);
        for (int m = 0; m < 16; m++)
            jpeg_save_markers(srcinfo, JPEG_APP0 + m, 0xFFFF);
    }
}

static void jcopy_markers_execute (j_decompress_ptr srcinfo, j_compress_ptr dstinfo)
{
    jpeg_saved_marker_ptr marker;
    for (marker = srcinfo->marker_list; marker != NULL; marker = marker->next) {
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

static void parse_switches (j_compress_ptr cinfo, bool progressive, bool arithmetic, bool for_real)
{
#ifdef C_PROGRESSIVE_SUPPORTED
  bool simple_progressive = cinfo->num_scans == 0 ? FALSE : TRUE;
#else
  bool simple_progressive = FALSE;
#endif
  cinfo->err->trace_level = 0;
    if(progressive==false){
        jpeg_c_set_int_param(cinfo, JINT_COMPRESS_PROFILE, JCP_FASTEST);
        if(arithmetic==false){cinfo->optimize_coding = TRUE;}
    }
    if(arithmetic){cinfo->arith_code = TRUE;
        cinfo->optimize_coding = FALSE;}
  if (for_real) {
#ifdef C_PROGRESSIVE_SUPPORTED
    if (simple_progressive)     /* process -progressive; -scans can override */
      jpeg_simple_progression(cinfo);
#endif
  }
  return;
}

int mozjpegtran (bool arithmetic, bool progressive, bool copyoption, const char * Input, const char * Output)
{
  struct jpeg_decompress_struct srcinfo;
  struct jpeg_compress_struct dstinfo;
  struct jpeg_error_mgr jsrcerr, jdsterr;
  FILE * fp;
  unsigned char *inbuffer = NULL;
  unsigned long insize = 0;
  unsigned char *outbuffer = NULL;
  unsigned long outsize = 0;
  const char * progname = "ECT (JPEG)";
  /* Initialize the JPEG decompression object with default error handling. */
  srcinfo.err = jpeg_std_error(&jsrcerr);
  jpeg_create_decompress(&srcinfo);
  /* Initialize the JPEG compression object with default error handling. */
  dstinfo.err = jpeg_std_error(&jdsterr);
  jpeg_create_compress(&dstinfo);

  /* Scan command line to find file names.
   * It is convenient to use just one switch-parsing routine, but the switch
   * values read here are mostly ignored; we will rescan the switches after
   * opening the input file.  Also note that most of the switches affect the
   * destination JPEG object, so we parse into that and then copy over what
   * needs to affects the source too.
   */

  parse_switches(&dstinfo, progressive, arithmetic, FALSE);
  jsrcerr.trace_level = jdsterr.trace_level;
  srcinfo.mem->max_memory_to_use = dstinfo.mem->max_memory_to_use;

  /* Open the input file. */
    if ((fp = fopen(Input, "rb")) == NULL) {
      fprintf(stderr, "%s: can't open %s for reading\n", progname, Input);
        return 2;
    }

#if JPEG_LIB_VERSION >= 80 || defined(MEM_SRCDST_SUPPORTED)
    size_t nbytes;
    do {
      inbuffer = (unsigned char *)realloc(inbuffer, insize + INPUT_BUF_SIZE);
      if (inbuffer == NULL) {
        fprintf(stderr, "%s: memory allocation failure\n", progname);
        exit(EXIT_FAILURE);
      }
      nbytes = JFREAD(fp, &inbuffer[insize], INPUT_BUF_SIZE);
      if (nbytes < INPUT_BUF_SIZE && ferror(fp)) {
          fprintf(stderr, "%s: can't read from %s\n", progname, Input);
      }
      insize += (unsigned long)nbytes;
    } while (nbytes == INPUT_BUF_SIZE);
    jpeg_mem_src(&srcinfo, inbuffer, insize);
#else
    jpeg_stdio_src(&srcinfo, fp);
#endif

  /* Enable saving of extra markers that we want to copy */
  jcopy_markers_setup(&srcinfo, copyoption);

  /* Read file header */
  jpeg_read_header(&srcinfo, TRUE);

  /* Any space needed by a transform option must be requested before
   * jpeg_read_coefficients so that memory allocation will be done right.
   */

  /* Read source file as DCT coefficients */
  jvirt_barray_ptr * src_coef_arrays = jpeg_read_coefficients(&srcinfo);

  /* Initialize destination compression parameters from source values */
  jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

  /* Adjust destination parameters if required by transform options;
   * also find out which set of coefficient arrays will hold the output.
   */
  jvirt_barray_ptr * dst_coef_arrays = src_coef_arrays;
    
    fclose(fp);

  /* Open the output file. */
    if ((fp = fopen(Output, "wb")) == NULL) {
      fprintf(stderr, "%s: can't open %s for writing\n", progname, Output);
        return 2;
    }

  /* Adjust default compression parameters by re-parsing the options */
  parse_switches(&dstinfo, progressive, arithmetic, TRUE);

  /* Specify data destination for compression */
#if JPEG_LIB_VERSION >= 80 || defined(MEM_SRCDST_SUPPORTED)
    jpeg_mem_dest(&dstinfo, &outbuffer, &outsize);
#else
    jpeg_stdio_dest(&dstinfo, fp);
#endif

  /* Start compressor (note no image data is actually written here) */
  jpeg_write_coefficients(&dstinfo, dst_coef_arrays);

  /* Copy to the output file any extra markers that we want to preserve */
  jcopy_markers_execute(&srcinfo, &dstinfo);

  /* Finish compression and release memory */
  jpeg_finish_compress(&dstinfo);
    
    unsigned char *buffer = outbuffer;
    unsigned long size = outsize;
    int x=0;
    if (progressive){
    if (insize <= size) {
      size = insize;
      buffer = inbuffer;
        x = 1;
    }}
    else{
        if (insize < size) {
            size = insize;
            buffer = inbuffer;
            x = 1;
        }}

    size_t nbytes2 = JFWRITE(fp, buffer, size);
    if (nbytes2 < size && ferror(fp)) {
        fprintf(stderr, "%s: can't write to %s\n", progname,Output);
    }
  jpeg_destroy_compress(&dstinfo);
  jpeg_finish_decompress(&srcinfo);
  jpeg_destroy_decompress(&srcinfo);
  fclose(fp);
  free(inbuffer);
  free(outbuffer);
  return x;
}
