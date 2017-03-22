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

  // Compression strategies.
  enum { MZ_DEFAULT_STRATEGY = 0, MZ_FILTERED = 1, MZ_HUFFMAN_ONLY = 2, MZ_RLE = 3, MZ_FIXED = 4 };

  // Method
#define MZ_DEFLATED 8

  // Return status codes. MZ_PARAM_ERROR is non-standard.
  enum { MZ_OK = 0, MZ_STREAM_END = 1, MZ_NEED_DICT = 2, MZ_ERRNO = -1, MZ_STREAM_ERROR = -2, MZ_DATA_ERROR = -3, MZ_MEM_ERROR = -4, MZ_BUF_ERROR = -5, MZ_VERSION_ERROR = -6, MZ_PARAM_ERROR = -10000 };

  // Compression levels: 0-9 are the standard zlib-style levels, 10 is best possible compression (not zlib compatible, and may be very slow), MZ_DEFAULT_COMPRESSION=MZ_DEFAULT_LEVEL.
  enum { MZ_NO_COMPRESSION = 0, MZ_BEST_SPEED = 1, MZ_BEST_COMPRESSION = 9, MZ_UBER_COMPRESSION = 10, MZ_DEFAULT_LEVEL = 6, MZ_DEFAULT_COMPRESSION = -1 };

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

  typedef size_t (*mz_file_read_func)(void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n);
  typedef size_t (*mz_file_write_func)(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n);

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

    mz_file_read_func m_pRead;
    mz_file_write_func m_pWrite;
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

  // Inits a ZIP archive reader.
  // These functions read and validate the archive's central directory.
  mz_bool mz_zip_reader_init(mz_zip_archive *pZip, mz_uint64 size, mz_uint32 flags);
  mz_bool mz_zip_reader_init_mem(mz_zip_archive *pZip, const void *pMem, size_t size, mz_uint32 flags);

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
  mz_bool mz_zip_writer_init_heap(mz_zip_archive *pZip, size_t size_to_reserve_at_beginning, size_t initial_allocation_size);

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
  mz_bool mz_zip_writer_add_mem(mz_zip_archive *pZip, const char *pArchive_name, const void *pBuf, size_t buf_size, mz_uint level_and_flags);
  mz_bool mz_zip_writer_add_mem_ex(mz_zip_archive *pZip, const char *pArchive_name, const void *pBuf, size_t buf_size, const void *pComment, mz_uint16 comment_size, mz_uint level_and_flags, mz_uint64 uncomp_size, mz_uint32 uncomp_crc32);

  // Adds a file to an archive by fully cloning the data from another archive.
  // This function fully clones the source file's compressed data (no recompression), along with its full filename, extra data, and comment fields.
  mz_bool mz_zip_writer_add_from_zip_reader(mz_zip_archive *pZip, mz_zip_archive *pSource_zip, mz_uint file_index);

  // Finalizes the archive by writing the central directory records followed by the end of central directory record.
  // After an archive is finalized, the only valid call on the mz_zip_archive struct is mz_zip_writer_end().
  // An archive must be manually finalized by calling this function for it to be valid.
  mz_bool mz_zip_writer_finalize_archive(mz_zip_archive *pZip);
  mz_bool mz_zip_writer_finalize_heap_archive(mz_zip_archive *pZip, void **pBuf, size_t *pSize);

  // Ends archive writing, freeing all allocations, and closing the output file if mz_zip_writer_init_file() was used.
  // Note for the archive to be valid, it must have been finalized before ending.
  mz_bool mz_zip_writer_end(mz_zip_archive *pZip);

  // Misc. high-level helper functions:

  // mz_zip_add_mem_to_archive_file_in_place() efficiently (but not atomically) appends a memory blob to a ZIP archive.
  // level_and_flags - compression level (0-10, see MZ_BEST_SPEED, MZ_BEST_COMPRESSION, etc.) logically OR'd with zero or more mz_zip_flags, or just set to MZ_DEFAULT_COMPRESSION.
  mz_bool mz_zip_add_mem_to_archive_file_in_place(const char *pZip_filename, const char *pArchive_name, const void *pBuf, size_t buf_size, const void *pComment, mz_uint16 comment_size, mz_uint level_and_flags);

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

  // High level compression functions:
  // tdefl_compress_mem_to_heap() compresses a block in memory to a heap block allocated via malloc().
  // On entry:
  //  pSrc_buf, src_buf_len: Pointer and size of source block to compress.
  //  flags: The max match finder probes (default is 128) logically OR'd against the above flags. Higher probes are slower but improve compression.
  // On return:
  //  Function returns a pointer to the compressed data, or NULL on failure.
  //  *pOut_len will be set to the compressed data's size, which could be larger than src_buf_len on uncompressible data.
  //  The caller must free() the returned block when it's no longer needed.

  // Output stream interface. The compressor uses this interface to write compressed data. It'll typically be called TDEFL_OUT_BUF_SIZE at a time.
  typedef mz_bool (*tdefl_put_buf_func_ptr)(const void* pBuf, int len, void *pUser);

  enum {TDEFL_LZ_DICT_SIZE = 32768, TDEFL_LZ_DICT_SIZE_MASK = TDEFL_LZ_DICT_SIZE - 1, TDEFL_MIN_MATCH_LEN = 3, TDEFL_MAX_MATCH_LEN = 258 };

  // TDEFL_OUT_BUF_SIZE MUST be large enough to hold a single entire compressed output block (using static/fixed Huffman codes).
  enum { TDEFL_OUT_BUF_SIZE = (64 * 1024 * 13 ) / 10 };

  // The low-level tdefl functions below may be used directly if the above helper functions aren't flexible enough. The low-level functions don't make any heap allocations, unlike the above helper functions.
  typedef enum
  {
    TDEFL_STATUS_BAD_PARAM = -2,
    TDEFL_STATUS_PUT_BUF_FAILED = -1,
    TDEFL_STATUS_OKAY = 0,
    TDEFL_STATUS_DONE = 1,
  } tdefl_status;

  // Must map to MZ_NO_FLUSH, MZ_SYNC_FLUSH, etc. enums
  typedef enum
  {
    TDEFL_FINISH = 4
  } tdefl_flush;

  // tdefl's compression state structure.
  typedef struct
  {
    tdefl_put_buf_func_ptr m_pPut_buf_func;
    void *m_pPut_buf_user;
    mz_uint m_lookahead_pos, m_lookahead_size, m_dict_size;
    mz_uint8 *m_pOutput_buf;
    mz_uint m_total_lz_bytes, m_lz_code_buf_dict_pos, m_bits_in, m_bit_buffer;
    tdefl_status m_prev_return_status;
    const void *m_pIn_buf;
    const mz_uint8 *m_pSrc;
    size_t m_src_buf_left;
    mz_uint8 m_dict[TDEFL_LZ_DICT_SIZE + TDEFL_MAX_MATCH_LEN - 1];
    mz_uint8 m_output_buf[TDEFL_OUT_BUF_SIZE];
  } tdefl_compressor;

  // Initializes the compressor.
  // There is no corresponding deinit() function because the tdefl API's do not dynamically allocate memory.
  // pBut_buf_func: If NULL, output data will be supplied to the specified callback. In this case, the user should call the tdefl_compress_buffer() API for compression.
  // If pBut_buf_func is NULL the user should always call the tdefl_compress() API.
  // flags: See the above enums (TDEFL_HUFFMAN_ONLY, TDEFL_WRITE_ZLIB_HEADER, etc.)
  tdefl_status tdefl_init(tdefl_compressor *d, tdefl_put_buf_func_ptr pPut_buf_func, void *pPut_buf_user);

#ifdef __cplusplus
}
#endif

#endif
