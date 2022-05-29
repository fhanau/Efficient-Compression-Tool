#include "zip.h"

#include <iostream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <algorithm>
#ifdef _WIN32
#ifdef _MSC_VER
#define NOMINMAX
#include <io.h>
#endif
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/param.h>
#endif

#include "../miniz/miniz.h"
#include "../zlib/zlib.h"
#include "../support.h"
#include "../lodepng/lodepng.h"

#if defined __GNUC__
#define PACK(...) __VA_ARGS__ __attribute__((__packed__))
#elif _MSC_VER
#define PACK(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#endif

using std::cerr;
using std::endl;
using std::string;
using std::vector;

//From miniz
static void *decompress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len, size_t *pOut_len)
{
  unsigned char *pBuf = 0;

  unsigned error = lodepng_inflate(&pBuf, pOut_len, (unsigned char*)pSrc_buf, src_buf_len);
  //Work around problem w/ nonstandard malloc's
  pBuf = (unsigned char*)realloc(pBuf, (*(pOut_len)) + 8);
  if(error){
    free(pBuf); *pOut_len = 0; return 0;
  }
  return pBuf;
}

const uint8_t Zip::header_magic[] = { 0x50, 0x4B, 0x03, 0x04 };

namespace {

PACK(struct LocalHeader {
  uint8_t magic[4];
  uint16_t version_needed;
  uint16_t flag;
  uint16_t compression_method;
  uint16_t last_mod_time;
  uint16_t last_mod_date;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t filename_len;
  uint16_t extra_field_len;
});

PACK(struct CDHeader {
  uint8_t magic[4] = { 0x50, 0x4B, 0x01, 0x02 };
  uint16_t version_made_by;
  uint16_t version_needed;
  uint16_t flag;
  uint16_t compression_method;
  uint16_t last_mod_time;
  uint16_t last_mod_date;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t filename_len;
  uint16_t extra_field_len;
  uint16_t comment_len;
  uint16_t disk_file_start;
  uint16_t internal_file_attributes;
  uint32_t external_file_attributes;
  uint32_t local_header_offset;
});

PACK(struct EOCD {
  uint8_t magic[4] = { 0x50, 0x4B, 0x05, 0x06 };
  uint16_t disk_num;
  uint16_t disk_cd_start;
  uint16_t num_records;
  uint16_t num_records_total;
  uint32_t cd_size;
  uint32_t cd_offset;
  uint16_t comment_len;
});

bool GetCDHeaders(const uint8_t* fp, size_t size, const EOCD& eocd, size_t zip_offset, vector<CDHeader>* out_cd_headers,
                  size_t* out_base_offset) {
  vector<CDHeader> cd_headers;
  size_t base_offset = 0;
  // Copy cd headers to vector
  const uint8_t* p_cdheader = fp + eocd.cd_offset;
  const uint8_t* cd_end = p_cdheader + eocd.cd_size;
  for (int i = 0; i < eocd.num_records; i++) {
    CDHeader cd_header;
    if (p_cdheader + sizeof(CDHeader) > cd_end){
      return false;
    }
    if (memcmp(p_cdheader, cd_header.magic, sizeof(cd_header.magic)) != 0) {
      // The offset might be relative to the first local file header instead of the beginning of the file.
      if (i != 0 || cd_end + zip_offset > fp + size ||
          memcmp(p_cdheader + zip_offset, cd_header.magic, sizeof(cd_header.magic)) != 0) {
        return false;
      }
      // This is indeed the case, set the |base_offset| to |zip_offset| and move pointer forward.
      base_offset = zip_offset;
      p_cdheader += base_offset;
      cd_end += base_offset;
    }
    memcpy(&cd_header, p_cdheader, sizeof(CDHeader));
    const uint8_t* p_local_header = fp + base_offset + cd_header.local_header_offset;

    // Check if local header magic matches.
    if (p_local_header + sizeof(LocalHeader) + cd_header.filename_len + cd_header.compressed_size > fp + size ||
        memcmp(p_local_header, Zip::header_magic, sizeof(Zip::header_magic)) != 0) {
      return false;
    }
    const LocalHeader* local_header = reinterpret_cast<const LocalHeader*>(p_local_header);
    // Check if file name matches.
    if (local_header->filename_len != cd_header.filename_len ||
        memcmp(p_local_header + sizeof(LocalHeader), p_cdheader + sizeof(CDHeader), cd_header.filename_len) != 0){
		return false;
	}

    p_cdheader += sizeof(CDHeader) + cd_header.filename_len + cd_header.extra_field_len + cd_header.comment_len;
    if (p_cdheader > cd_end){
		return false;
	}
    cd_headers.push_back(cd_header);
  }
  std::sort(cd_headers.begin(), cd_headers.end(), [](const CDHeader& a, const CDHeader& b) { return a.local_header_offset < b.local_header_offset; });
  // Check if there's any overlaps.
  for (size_t i = 1; i < cd_headers.size(); i++) {
    if (cd_headers[i - 1].local_header_offset + sizeof(LocalHeader) + cd_headers[i - 1].filename_len +
            cd_headers[i - 1].compressed_size >
        cd_headers[i].local_header_offset) {
      return false;
    }
  }

  *out_cd_headers = std::move(cd_headers);
  *out_base_offset = base_offset;
  return true;
}

}  // namespace

uint32_t Zip::RecompressFile(unsigned char* data, uint32_t size, uint32_t size_leanified, string filename, const ECTOptions& Options){
  bool isZIP = size > sizeof(Zip::header_magic) && memcmp(data, Zip::header_magic, sizeof(Zip::header_magic)) == 0;

  int dotpos = filename.find_last_of('.');
  if(dotpos == filename.npos){
    return size;
  }
  string extension = filename.substr(filename.find_last_of('.'));
  if(!(extension == ".PNG" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".JPEG" || extension == ".JPG" || isZIP)){
    return size;
  }

#ifdef _WIN32
  char tempname[13];
  memcpy(tempname, "fXXXXXX", 8);
#ifdef _MSC_VER
  _mktemp_s(tempname, 8);
#else
  int descriptor = mkstemp(tempname);
  close(descriptor);
  //Just in case
  unlink(tempname);
#endif

  memcpy(&(tempname[7]), extension.c_str(), extension.length());
  tempname[7 + extension.length()] = '\0';
  const char* temp = tempname;
  if (exists(temp)) {
    printf("Error: Can't create temp file\n");
	  return size;
  }
  FILE* stream = fopen(temp, "wb");
#else
  char* t0 = getcwd(0, MAXPATHLEN - 7 - extension.length());
  if(!t0){
    printf("Error: Can't get working directory\n");
    return size;
  }
  string tmp = (std::string)t0 + "/XXXXXX" + extension;
  free(t0);
  char* temp = strdup(tmp.c_str());

  int fd = mkstemps(temp, extension.size());
  FILE* stream = fdopen(fd, "wb");
#endif
  fwrite(data, 1, size, stream);
  fclose(stream);

  if(isZIP){
    std::vector<int> args;
    args.push_back(0);
    const char * v[1];
    v[0] = temp;
    zipHandler(args, v, 1, Options);
  } else {
    fileHandler(temp, Options, 1);
  }
  long long new_size = filesize(temp);

  if(new_size < size && new_size >= 0){
    stream = fopen(temp, "rb");
    if(fread(data - size_leanified, 1, new_size, stream) < new_size){
      printf("Error: Read error\n");
    }
    else{
      size = new_size;
    }
    fclose(stream);
  }

  unlink(temp);
#ifndef _WIN32
  free(temp);
#endif
  return size;
}

size_t Zip::Leanify(const ECTOptions& Options, size_t* files) {
  uint8_t* first_local_header = std::search(fp_, fp_ + size_, header_magic, std::end(header_magic));
  // The offset of the first local header, we should keep everything before this offset.
  size_t zip_offset = first_local_header - fp_;
  if (zip_offset == size_) {
    cerr << "ZIP header magic not found!" << endl;
    return size_;
  }
  // The offset that all the offsets in the zip file based on (relative to).
  // Should be 0 by default except when we detected that the input file has a base offset.
  size_t base_offset = 0;

  EOCD eocd;
  vector<CDHeader> cd_headers;
  uint8_t* p_end = fp_ + size_;
  // smallest possible location of EOCD if there's a 64K comment
  uint8_t* p_searchstart = std::max(fp_, p_end - 65535 - sizeof(eocd.magic));
  uint8_t* p_eocd = nullptr;
  while (true) {
    if (p_eocd != nullptr) {
      cerr << "Warning: Found EOCD at 0x" << std::hex << p_eocd - fp_ << std::dec << ", but it's invalid." << endl;
      p_end = p_eocd;
    }
    p_eocd = std::find_end(p_searchstart, p_end, eocd.magic, std::end(eocd.magic));
    if (p_eocd == p_end) {
      cerr << "EOCD not found!" << endl;
      return size_;
    }

    if (p_eocd + sizeof(EOCD) > p_end){
      continue;
    }
  
    memcpy(&eocd, p_eocd, sizeof(EOCD));
    uint8_t* cd_end = fp_ + eocd.cd_offset + eocd.cd_size;
    if (cd_end > p_eocd){
      continue;
    }
	
    // Try to get all CD headers using this EOCD, if everything checks out then proceed.
    if (GetCDHeaders(fp_, size_, eocd, zip_offset, &cd_headers, &base_offset)) {
      break;
    }
  }

  uint8_t* fp_w = fp_;
  uint8_t* fp_w_base = fp_w + base_offset;
  memmove(fp_w, fp_, zip_offset);
  uint8_t* p_write = fp_w + zip_offset;
  // Local file header
  for (CDHeader& cd_header : cd_headers) {
    (*files)++;
    uint8_t* p_read = fp_ + base_offset + cd_header.local_header_offset;

    cd_header.local_header_offset = p_write - fp_w_base;

    size_t header_size = sizeof(LocalHeader) + cd_header.filename_len;
    // move header
    memmove(p_write, p_read, header_size);
    LocalHeader* local_header = reinterpret_cast<LocalHeader*>(p_write);

    // if Extra field length is not 0, then skip it and set it to 0
    if (local_header->extra_field_len) {
      p_read += local_header->extra_field_len;
      local_header->extra_field_len = 0;
    }

    if (local_header->flag & 8) {
      // set this bit to 0, we don't use data descriptor to save 16 byte
      local_header->flag &= ~8;
      cd_header.flag &= ~8;

      // Use the correct value from central directory
      local_header->crc32 = cd_header.crc32;
      local_header->compressed_size = cd_header.compressed_size;
      local_header->uncompressed_size = cd_header.uncompressed_size;
    }

    string filename(reinterpret_cast<char*>(local_header) + sizeof(LocalHeader), local_header->filename_len);

    p_read += header_size;
    p_write += header_size;

    if (p_read + local_header->compressed_size > p_end) {
      cerr << "Compressed size too large: " << local_header->compressed_size << endl;
      break;
    }

    // If the method is store, just Leanify the embedded file
    // don't try to change it to deflate, it might break some file.
    if (local_header->compression_method == 0) {
      // method is store
      if (local_header->compressed_size) {
        uint32_t new_size = RecompressFile(p_read, local_header->compressed_size, p_read - p_write, filename, Options);
        if(new_size == local_header->compressed_size){
          memmove(p_write, p_read, local_header->compressed_size);
        }
        cd_header.crc32 = local_header->crc32 = crc32(0, p_write, new_size);
        cd_header.compressed_size = local_header->compressed_size = new_size;
        cd_header.uncompressed_size = local_header->uncompressed_size = new_size;
        p_write += local_header->compressed_size;
      }
      continue;
    }

    // If unsupported compression method or encrypted, just move it.
    if (local_header->compression_method != 8 || local_header->flag & 1) {
      memmove(p_write, p_read, local_header->compressed_size);
      p_write += local_header->compressed_size;
      continue;
    }

    // Switch from deflate to store for empty file.
    if (local_header->uncompressed_size == 0) {
      cd_header.compression_method = local_header->compression_method = 0;
      cd_header.compressed_size = local_header->compressed_size = 0;
      continue;
    }

    // decompress
    size_t decompressed_size = 0;
    uint8_t* decompress_buf = static_cast<uint8_t*>(
    decompress_mem_to_heap(p_read, local_header->compressed_size, &decompressed_size));

    if (!decompress_buf || decompressed_size != local_header->uncompressed_size ||
        local_header->crc32 != crc32(0, decompress_buf, local_header->uncompressed_size)) {
      cerr << "Decompression failed or CRC32 mismatch, skipping this file." << endl;
      free(decompress_buf);
      memmove(p_write, p_read, local_header->compressed_size);
      p_write += local_header->compressed_size;
      continue;
    }

    // Leanify uncompressed file
    uint32_t new_uncomp_size = RecompressFile(decompress_buf, decompressed_size, 0, filename, Options);

    // recompress
    uint8_t* compress_buf = nullptr;
    size_t new_comp_size = 0;
    ZopfliBuffer(Options.Mode, Options.DeflateMultithreading, decompress_buf, new_uncomp_size, &compress_buf, &new_comp_size);

    // switch to store if deflate makes file larger
    if (new_uncomp_size <= new_comp_size && new_uncomp_size <= local_header->compressed_size) {
      cd_header.compression_method = local_header->compression_method = 0;
      cd_header.crc32 = local_header->crc32 = crc32(0, decompress_buf, new_uncomp_size);
      cd_header.compressed_size = local_header->compressed_size = new_uncomp_size;
      cd_header.uncompressed_size = local_header->uncompressed_size = new_uncomp_size;
      memcpy(p_write, decompress_buf, new_uncomp_size);
    } else if (new_comp_size < local_header->compressed_size) {
      cd_header.crc32 = local_header->crc32 = crc32(0, decompress_buf, new_uncomp_size);
      cd_header.compressed_size = local_header->compressed_size = new_comp_size;
      cd_header.uncompressed_size = local_header->uncompressed_size = new_uncomp_size;
      memcpy(p_write, compress_buf, new_comp_size);
    } else {
      memmove(p_write, p_read, local_header->compressed_size);
    }
    p_write += local_header->compressed_size;

    free(decompress_buf);
    delete[] compress_buf;
  }

  // central directory offset
  eocd.cd_offset = p_write - fp_w_base;
  for (CDHeader& cd_header : cd_headers) {
    cd_header.extra_field_len = cd_header.comment_len = 0;

    memcpy(p_write, &cd_header, sizeof(CDHeader));
    p_write += sizeof(CDHeader);
    // Copy the filename from local file header to central directory,
    // the old central directory might have been overwritten already because we sort them.
    memcpy(p_write, fp_w_base + cd_header.local_header_offset + 30, cd_header.filename_len);
    p_write += cd_header.filename_len;
  }

  // Update end of central directory record
  eocd.num_records = eocd.num_records_total = cd_headers.size();
  eocd.cd_size = p_write - fp_w_base - eocd.cd_offset;
  eocd.comment_len = 0;

  memcpy(p_write, &eocd, sizeof(EOCD));

  size_ = p_write + sizeof(EOCD) - fp_;
  return size_;
}
