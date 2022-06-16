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

#include "mozjpeg/jinclude.h"
#include "mozjpeg/jpeglib.h"
#include "mozjpeg/transupp.h"           /* Support routines for jpegtran */
#include "main.h"
#include "support.h"

static size_t jcopy_markers_execute_s (j_decompress_ptr srcinfo, j_compress_ptr dstinfo)
{
  size_t size = 0;
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
    if(marker->marker == JPEG_COM || (marker->marker >= JPEG_APP0 && marker->marker <= JPEG_APP0 + 15)){
      size += marker->data_length;
    }
  }
  return size;
}

/* Map Exif orientation values to correct JXFORM_CODE */
static JXFORM_CODE orient_jxform[9] = {
  JXFORM_NONE,
  JXFORM_NONE,
  JXFORM_FLIP_H,
  JXFORM_ROT_180,
  JXFORM_FLIP_V,
  JXFORM_TRANSPOSE,
  JXFORM_ROT_90,
  JXFORM_TRANSVERSE,
  JXFORM_ROT_270
};

/* Get Exif image orientation. Copied from adjust_exif_parameters. */
LOCAL(unsigned int)
get_exif_orientation (JOCTET *data, unsigned int length)
{
  boolean is_motorola; /* Flag for byte order */
  unsigned int number_of_tags, tagnum;
  unsigned int offset;

  if (length < 12) return 0; /* Length of an IFD entry */

  /* Discover byte order */
  if (GETJOCTET(data[0]) == 0x49 && GETJOCTET(data[1]) == 0x49)
    is_motorola = FALSE;
  else if (GETJOCTET(data[0]) == 0x4D && GETJOCTET(data[1]) == 0x4D)
    is_motorola = TRUE;
  else
    return 0;

  /* Check Tag Mark */
  if (is_motorola) {
    if (GETJOCTET(data[2]) != 0) return 0;
    if (GETJOCTET(data[3]) != 0x2A) return 0;
  } else {
    if (GETJOCTET(data[3]) != 0) return 0;
    if (GETJOCTET(data[2]) != 0x2A) return 0;
  }

  /* Get first IFD offset (offset to IFD0) */
  if (is_motorola) {
    if (GETJOCTET(data[4]) != 0) return 0;
    if (GETJOCTET(data[5]) != 0) return 0;
    offset = GETJOCTET(data[6]);
    offset <<= 8;
    offset += GETJOCTET(data[7]);
  } else {
    if (GETJOCTET(data[7]) != 0) return 0;
    if (GETJOCTET(data[6]) != 0) return 0;
    offset = GETJOCTET(data[5]);
    offset <<= 8;
    offset += GETJOCTET(data[4]);
  }
  if (offset > length - 2) return 0; /* check end of data segment */

  /* Get the number of directory entries contained in this IFD */
  if (is_motorola) {
    number_of_tags = GETJOCTET(data[offset]);
    number_of_tags <<= 8;
    number_of_tags += GETJOCTET(data[offset+1]);
  } else {
    number_of_tags = GETJOCTET(data[offset+1]);
    number_of_tags <<= 8;
    number_of_tags += GETJOCTET(data[offset]);
  }
  if (number_of_tags == 0) return 0;
  offset += 2;

  /* Search for Orientation Tag in this IFD */
  do {
    if (offset > length - 12) return 0; /* check end of data segment */
    /* Get Tag number */
    if (is_motorola) {
      tagnum = GETJOCTET(data[offset]);
      tagnum <<= 8;
      tagnum += GETJOCTET(data[offset+1]);
    } else {
      tagnum = GETJOCTET(data[offset+1]);
      tagnum <<= 8;
      tagnum += GETJOCTET(data[offset]);
    }
    if (tagnum == 0x0112) {
      if (is_motorola) {
        return GETJOCTET(data[offset+9]);
      } else {
        return GETJOCTET(data[offset+8]);
      }
    }
    offset += 12;
  } while (--number_of_tags);
  return 0;
}

METHODDEF(void)
output_message (j_common_ptr cinfo)
{
  char buffer[JMSG_LENGTH_MAX];

  /* Create the message */
  (*cinfo->err->format_message) (cinfo, buffer);

  /* Send it to stderr, adding a newline */
  fprintf(stderr, "%s: %s\n", cinfo->err->addon_message_table[0], buffer);
}

int mozjpegtran (bool arithmetic, bool progressive, bool strip, unsigned autorotate, const char * Infile, const char * Outfile, size_t* stripped_outsize)
{
  struct jpeg_decompress_struct srcinfo;
  struct jpeg_compress_struct dstinfo;
  struct jpeg_error_mgr jsrcerr, jdsterr;
  jpeg_transform_info transformoption; /* image transformation options */
  FILE * fp;
  unsigned char *outbuffer = 0;
  unsigned long outsize = 0;
  size_t extrasize = 0;
  unsigned char copy_exif = 0;
  /* Initialize the JPEG decompression object with default error handling. */
  srcinfo.err = jpeg_std_error(&jsrcerr);
  srcinfo.err->output_message = output_message;
  const char* addon = Infile;
  srcinfo.err->addon_message_table = &addon;
  jpeg_create_decompress(&srcinfo);
  /* Initialize the JPEG compression object with default error handling. */
  dstinfo.err = jpeg_std_error(&jdsterr);
  jpeg_create_compress(&dstinfo);
  if (!progressive){
    jpeg_c_set_int_param(&dstinfo, JINT_COMPRESS_PROFILE, JCP_FASTEST);
  }

  /* Open the input file. */
  if (!(fp = fopen(Infile, "rb"))) {
    fprintf(stderr, "ECT: can't open %s for reading\n", Infile);
    return 2;
  }

  long long insize = filesize(Infile);
  if(insize < 0){
    fprintf(stderr, "ECT: can't read from %s\n", Infile);
    return 2;
  }
  unsigned char* inbuffer = (unsigned char*)malloc(insize);
  if (!inbuffer) {
    fprintf(stderr, "ECT: memory allocation failure\n");
    exit(1);
  }

  if (fread(inbuffer, 1, insize, fp) < (size_t)insize) {
    fprintf(stderr, "ECT: can't read from %s\n", Infile);
  }
  fclose(fp);

  jpeg_mem_src(&srcinfo, inbuffer, insize);

  /* Enable saving of extra markers that we want to copy */
  if (!strip) {
    jcopy_markers_setup(&srcinfo, JCOPYOPT_ALL);
  } else if (autorotate > 0) {
    jpeg_save_markers(&srcinfo, JPEG_APP0+1, 0xFFFF);
  }

  /* Read file header */
  jpeg_read_header(&srcinfo, 1);

  /* Determine orientation from Exif */
  transformoption.transform = JXFORM_NONE;
  if (autorotate > 0 && srcinfo.marker_list != NULL &&
      srcinfo.marker_list->marker == JPEG_APP0+1 &&
      srcinfo.marker_list->data_length >= 6 &&
      GETJOCTET(srcinfo.marker_list->data[0]) == 0x45 &&
      GETJOCTET(srcinfo.marker_list->data[1]) == 0x78 &&
      GETJOCTET(srcinfo.marker_list->data[2]) == 0x69 &&
      GETJOCTET(srcinfo.marker_list->data[3]) == 0x66 &&
      GETJOCTET(srcinfo.marker_list->data[4]) == 0 &&
      GETJOCTET(srcinfo.marker_list->data[5]) == 0) {
    unsigned int orientation = get_exif_orientation(srcinfo.marker_list->data + 6,
                                                    srcinfo.marker_list->data_length - 6);
    /* Setup transform options for auto-rotate */
    if (orientation > 1 && orientation <= 8) {
      transformoption.transform = orient_jxform[orientation];
      transformoption.perfect = autorotate > 1;
      transformoption.trim = TRUE;
      transformoption.force_grayscale = FALSE;
      transformoption.crop = FALSE;
      transformoption.slow_hflip = FALSE;
      /* If perfect requested but not possible, show warning and do not transform */
      if (!jtransform_request_workspace(&srcinfo, &transformoption)) {
        fprintf(stderr, "ECT: %s can't be transformed perfectly\n", Infile);
        transformoption.transform = JXFORM_NONE;
        copy_exif = 1;
      }
    }
  }

  /* Read source file as DCT coefficients */
  jvirt_barray_ptr * src_coef_arrays = jpeg_read_coefficients(&srcinfo);

  /* Initialize destination compression parameters from source values */
  jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

  /* Adjust destination parameters if required by transform options;
   * also find out which set of coefficient arrays will hold the output.
   */
  jvirt_barray_ptr * dst_coef_arrays = src_coef_arrays;
  if (transformoption.transform != JXFORM_NONE) {
    dst_coef_arrays = jtransform_adjust_parameters(&srcinfo, &dstinfo,
                                                   src_coef_arrays,
                                                   &transformoption);
  }

  /* Adjust default compression parameters by re-parsing the options */
  dstinfo.optimize_coding = !arithmetic;
  dstinfo.arith_code = arithmetic;
  if (!dstinfo.num_scans || !progressive) {
    dstinfo.num_scans = 0;
    dstinfo.scan_info = 0;
  }

  /* Specify data destination for compression */
  jpeg_mem_dest(&dstinfo, &outbuffer, &outsize);

  /* Start compressor (note no image data is actually written here) */
  jpeg_write_coefficients(&dstinfo, dst_coef_arrays);

  /* Copy to the output file any extra markers that we want to preserve */
  if (!strip || copy_exif) {
    extrasize = jcopy_markers_execute_s(&srcinfo, &dstinfo);
  }

  /* Execute image transformation, if any */
  if (transformoption.transform != JXFORM_NONE) {
    jtransform_execute_transformation(&srcinfo, &dstinfo,
                                      src_coef_arrays,
                                      &transformoption);
  }

  /* Finish compression and release memory */
  jpeg_finish_compress(&dstinfo);
  free(inbuffer);

  bool x = insize < outsize;

  if (outsize < insize){
    /* Open the output file. */
    if (!(fp = fopen(Outfile, "wb"))) {
      fprintf(stderr, "ECT: can't open %s for writing\n", Outfile);
      free(outbuffer);
      return 2;
    }

    /* Write new file. */
    if (fwrite(outbuffer, 1, outsize, fp) < outsize) {
      fprintf(stderr, "ECT: can't write to %s\n", Outfile);
    }
    fclose(fp);
  }

  jpeg_destroy_compress(&dstinfo);
  jpeg_finish_decompress(&srcinfo);
  jpeg_destroy_decompress(&srcinfo);
  free(outbuffer);
  (*stripped_outsize) = /*x ? insize : */outsize - extrasize;
  return x;
}
