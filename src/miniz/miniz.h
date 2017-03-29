#ifndef MINIZ_H
#define MINIZ_H

#include <stdlib.h>

// Defines to completely disable specific portions of miniz.c:
// If all macros here are defined the only functionality remaining will be CRC-32, adler-32, tinfl, and tdefl.

// Define MINIZ_NO_STDIO to disable all usage and any functions which rely on stdio for file I/O.
//#define MINIZ_NO_STDIO

// If MINIZ_NO_TIME is specified then the ZIP archive functions will not be able to get the current time, or
// get/set file times, and the C run-time funcs that get/set times won't be called.
// The current downside is the times written to your archives will be from 1979.
//#define MINIZ_NO_TIME

// Define MINIZ_NO_ARCHIVE_APIS to disable all ZIP archive API's.
//#define MINIZ_NO_ARCHIVE_APIS

// Define MINIZ_NO_ARCHIVE_APIS to disable all writing related ZIP archive API's.
//#define MINIZ_NO_ARCHIVE_WRITING_APIS

#if defined(__TINYC__) && (defined(__linux) || defined(__linux__))
// TODO: Work around "error: include file 'sys\utime.h' when compiling with tcc on Linux
#define MINIZ_NO_TIME
#endif

#if !defined(MINIZ_NO_TIME) && !defined(MINIZ_NO_ARCHIVE_APIS)
#include <time.h>
#endif

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__i386) || defined(__i486__) || defined(__i486) || defined(i386) || defined(__ia64__) || defined(__x86_64__)
// MINIZ_X86_OR_X64_CPU is only used to help set the below macros.
#define MINIZ_X86_OR_X64_CPU 1
#endif

#if (__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__) || MINIZ_X86_OR_X64_CPU
// Set MINIZ_LITTLE_ENDIAN to 1 if the processor is little endian.
#define MINIZ_LITTLE_ENDIAN 1
#endif

#if MINIZ_X86_OR_X64_CPU
// Set MINIZ_USE_UNALIGNED_LOADS_AND_STORES to 1 on CPU's that permit efficient integer loads and stores from unaligned addresses.
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1
#endif

#if defined(_M_X64) || defined(_WIN64) || defined(__MINGW64__) || defined(_LP64) || defined(__LP64__) || defined(__ia64__) || defined(__x86_64__)
// Set MINIZ_HAS_64BIT_REGISTERS to 1 if operations on 64-bit integers are reasonably fast (and don't involve compiler generated calls to helper functions).
#define MINIZ_HAS_64BIT_REGISTERS 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

  // ------------------- zlib-style API Definitions.

  // For more compatibility with zlib, miniz.c uses unsigned long for some parameters/struct members. Beware: mz_ulong can be either 32 or 64-bits!
  typedef unsigned long mz_ulong;

#define MZ_ADLER32_INIT (1)

#define MZ_CRC32_INIT (0)

  // Method
#define MZ_DEFLATED 8

  // ------------------- Types and macros

  typedef unsigned char mz_uint8;
  typedef signed short mz_int16;
  typedef unsigned short mz_uint16;
  typedef unsigned int mz_uint32;
  typedef unsigned int mz_uint;
  typedef long long mz_int64;
  typedef unsigned long long mz_uint64;
  typedef int mz_bool;

#define MZ_FALSE (0)
#define MZ_TRUE (1)

  // An attempt to work around MSVC's spammy "warning C4127: conditional expression is constant" message.
#define MZ_MACRO_END while (0)

  // ------------------- ZIP archive reading/writing

#ifndef MINIZ_NO_ARCHIVE_APIS

  enum
  {
    MZ_ZIP_MAX_IO_BUF_SIZE = 64*1024,
    MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE = 260,
    MZ_ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE = 256
  };

  typedef struct
  {
    mz_uint32 m_file_index;
    mz_uint32 m_central_dir_ofs;
    mz_uint16 m_version_made_by;
    mz_uint16 m_version_needed;
    mz_uint16 m_bit_flag;
    mz_uint16 m_method;
#ifndef MINIZ_NO_TIME
    time_t m_time;
#endif
    mz_uint32 m_crc32;
    mz_uint64 m_comp_size;
    mz_uint64 m_uncomp_size;
    mz_uint16 m_internal_attr;
    mz_uint32 m_external_attr;
    mz_uint64 m_local_header_ofs;
    mz_uint32 m_comment_size;
    char m_filename[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
    char m_comment[MZ_ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE];
  } mz_zip_archive_file_stat;

  struct mz_zip_internal_state_tag;
  typedef struct mz_zip_internal_state_tag mz_zip_internal_state;

  typedef enum
  {
    MZ_ZIP_MODE_INVALID = 0,
    MZ_ZIP_MODE_READING = 1,
    MZ_ZIP_MODE_WRITING = 2,
    MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED = 3
  } mz_zip_mode;

  typedef struct mz_zip_archive_tag
  {
    mz_uint64 m_archive_size;
    mz_uint64 m_central_directory_file_ofs;
    mz_uint m_total_files;
    mz_zip_mode m_zip_mode;

    mz_uint m_file_offset_alignment;

    void *m_pIO_opaque;

    mz_zip_internal_state *m_pState;

  } mz_zip_archive;

  typedef enum
  {
    MZ_ZIP_FLAG_CASE_SENSITIVE                = 0x0100,
    MZ_ZIP_FLAG_IGNORE_PATH                   = 0x0200,
    MZ_ZIP_FLAG_COMPRESSED_DATA               = 0x0400,
    MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY = 0x0800
  } mz_zip_flags;

  // ZIP archive reading

#ifndef MINIZ_NO_STDIO
  mz_bool mz_zip_reader_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint32 flags);
#endif

  // Returns the total number of files in the archive.
  mz_uint mz_zip_reader_get_num_files(mz_zip_archive *pZip);

  // Ends archive reading, freeing all allocations, and closing the input archive file if mz_zip_reader_init_file() was used.
  mz_bool mz_zip_reader_end(mz_zip_archive *pZip);

  // ZIP archive writing

#ifndef MINIZ_NO_ARCHIVE_WRITING_APIS

  // Inits a ZIP archive writer.
  mz_bool mz_zip_writer_init(mz_zip_archive *pZip, mz_uint64 existing_size);

#ifndef MINIZ_NO_STDIO
  mz_bool mz_zip_writer_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint64 size_to_reserve_at_beginning);
#endif

  // Converts a ZIP archive reader object into a writer object, to allow efficient in-place file appends to occur on an existing archive.
  // For archives opened using mz_zip_reader_init_file, pFilename must be the archive's filename so it can be reopened for writing. If the file can't be reopened, mz_zip_reader_end() will be called.
  // For archives opened using mz_zip_reader_init_mem, the memory block must be growable using the realloc callback (which defaults to realloc unless you've overridden it).
  // Finally, for archives opened using mz_zip_reader_init, the mz_zip_archive's user provided m_pWrite function cannot be NULL.
  // Note: In-place archive modification is not recommended unless you know what you're doing, because if execution stops or something goes wrong before
  // the archive is finalized the file's central directory will be hosed.
  mz_bool mz_zip_writer_init_from_reader(mz_zip_archive *pZip, const char *pFilename);

  // Adds the contents of a memory buffer to an archive. These functions record the current local time into the archive.
  // To add a directory entry, call this method with an archive name ending in a forwardslash with empty buffer.
  // level_and_flags - compression level (0-10, see MZ_BEST_SPEED, MZ_BEST_COMPRESSION, etc.) logically OR'd with zero or more mz_zip_flags, or just set to MZ_DEFAULT_COMPRESSION.
  mz_bool mz_zip_writer_add_mem_ex(mz_zip_archive *pZip, const char *pArchive_name, const void *pBuf, size_t buf_size, const void *pComment, mz_uint16 comment_size, const char* location);

  // Finalizes the archive by writing the central directory records followed by the end of central directory record.
  // After an archive is finalized, the only valid call on the mz_zip_archive struct is mz_zip_writer_end().
  // An archive must be manually finalized by calling this function for it to be valid.
  mz_bool mz_zip_writer_finalize_archive(mz_zip_archive *pZip);

  // Ends archive writing, freeing all allocations, and closing the output file if mz_zip_writer_init_file() was used.
  // Note for the archive to be valid, it must have been finalized before ending.
  mz_bool mz_zip_writer_end(mz_zip_archive *pZip);

  // Misc. high-level helper functions:

  // mz_zip_add_mem_to_archive_file_in_place() efficiently (but not atomically) appends a memory blob to a ZIP archive.
  // level_and_flags - compression level (0-10, see MZ_BEST_SPEED, MZ_BEST_COMPRESSION, etc.) logically OR'd with zero or more mz_zip_flags, or just set to MZ_DEFAULT_COMPRESSION.
  mz_bool mz_zip_add_mem_to_archive_file_in_place(const char *pZip_filename, const char *pArchive_name, const void *pBuf, size_t buf_size, const void *pComment, mz_uint16 comment_size, const char* location);

#endif // #ifndef MINIZ_NO_ARCHIVE_WRITING_APIS

#endif // #ifndef MINIZ_NO_ARCHIVE_APIS

  // ------------------- Low-level Decompression API Definitions

  // Decompression flags used by tinfl_decompress().
  // TINFL_FLAG_HAS_MORE_INPUT: If set, there are more input bytes available beyond the end of the supplied input buffer. If clear, the input buffer contains all remaining input.
  // TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF: If set, the output buffer is large enough to hold the entire decompressed stream. If clear, the output buffer is at least the size of the dictionary (typically 32KB).
  enum
  {
    TINFL_FLAG_HAS_MORE_INPUT = 2,
    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
  };

  // High level decompression functions:
  // tinfl_decompress_mem_to_heap() decompresses a block in memory to a heap block allocated via malloc().
  // On entry:
  //  pSrc_buf, src_buf_len: Pointer and size of the Deflate or zlib source data to decompress.
  // On return:
  //  Function returns a pointer to the decompressed data, or NULL on failure.
  //  *pOut_len will be set to the decompressed data's size, which could be larger than src_buf_len on uncompressible data.
  //  The caller must call free() on the returned block when it's no longer needed.
  void *tinfl_decompress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len, size_t *pOut_len, int flags);

  struct tinfl_decompressor_tag; typedef struct tinfl_decompressor_tag tinfl_decompressor;

  // Max size of LZ dictionary.
#define TINFL_LZ_DICT_SIZE 32768

  // Return status.
  typedef enum
  {
    TINFL_STATUS_BAD_PARAM = -3,
    TINFL_STATUS_ADLER32_MISMATCH = -2,
    TINFL_STATUS_FAILED = -1,
    TINFL_STATUS_DONE = 0,
    TINFL_STATUS_NEEDS_MORE_INPUT = 1,
    TINFL_STATUS_HAS_MORE_OUTPUT = 2
  } tinfl_status;

  // Initializes the decompressor to its initial state.
#define tinfl_init(r) do { (r)->m_state = 0; } MZ_MACRO_END

  // Main low-level decompressor coroutine function. This is the only function actually needed for decompression. All the other functions are just high-level helpers for improved usability.
  // This is a universal API, i.e. it can be used as a building block to build any desired higher level decompression API. In the limit case, it can be called once per every byte input or output.
  tinfl_status tinfl_decompress(tinfl_decompressor *r, const mz_uint8 *pIn_buf_next, size_t *pIn_buf_size, mz_uint8 *pOut_buf_start, mz_uint8 *pOut_buf_next, size_t *pOut_buf_size, const mz_uint32 decomp_flags);

  // Internal/private bits follow.
  enum
  {
    TINFL_MAX_HUFF_TABLES = 3, TINFL_MAX_HUFF_SYMBOLS_0 = 288, TINFL_MAX_HUFF_SYMBOLS_1 = 32, TINFL_MAX_HUFF_SYMBOLS_2 = 19,
    TINFL_FAST_LOOKUP_BITS = 10, TINFL_FAST_LOOKUP_SIZE = 1 << TINFL_FAST_LOOKUP_BITS
  };

  typedef struct
  {
    mz_uint8 m_code_size[TINFL_MAX_HUFF_SYMBOLS_0];
    mz_int16 m_look_up[TINFL_FAST_LOOKUP_SIZE], m_tree[TINFL_MAX_HUFF_SYMBOLS_0 * 2];
  } tinfl_huff_table;

#if MINIZ_HAS_64BIT_REGISTERS
#define TINFL_USE_64BIT_BITBUF 1
#endif

#if TINFL_USE_64BIT_BITBUF
  typedef mz_uint64 tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (64)
#else
  typedef mz_uint32 tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (32)
#endif

  struct tinfl_decompressor_tag
  {
    mz_uint32 m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32, m_final, m_type, m_check_adler32, m_dist, m_counter, m_num_extra, m_table_sizes[TINFL_MAX_HUFF_TABLES];
    tinfl_bit_buf_t m_bit_buf;
    size_t m_dist_from_out_buf_start;
    tinfl_huff_table m_tables[TINFL_MAX_HUFF_TABLES];
    mz_uint8 m_raw_header[4], m_len_codes[TINFL_MAX_HUFF_SYMBOLS_0 + TINFL_MAX_HUFF_SYMBOLS_1 + 137];
  };

#ifdef __cplusplus
}
#endif

#endif
