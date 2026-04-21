#include "zip.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <fcntl.h>
#ifdef _WIN32
#ifdef _MSC_VER
#define NOMINMAX
#include <io.h>
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "../lodepng/lodepng.h"
#include "../miniz/miniz.h"
#include "../support.h"
#include "../zlib/zlib.h"

#if defined __GNUC__
#define PACK(...) __VA_ARGS__ __attribute__((__packed__))
#elif _MSC_VER
#define PACK(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#endif

using std::cerr;
using std::endl;
using std::string;
using std::vector;

const uint8_t Zip::header_magic[] = {0x50, 0x4B, 0x03, 0x04};

namespace {

constexpr uint32_t kUint32Max = std::numeric_limits<uint32_t>::max();
constexpr uint16_t kUint16Max = std::numeric_limits<uint16_t>::max();
constexpr uint16_t kZip64ExtraHeaderId = 0x0001;
constexpr size_t kZip64LocalExtraLength = 20;  // header(4) + sizes(16)

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
  uint8_t magic[4] = {0x50, 0x4B, 0x01, 0x02};
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
  uint8_t magic[4] = {0x50, 0x4B, 0x05, 0x06};
  uint16_t disk_num;
  uint16_t disk_cd_start;
  uint16_t num_records;
  uint16_t num_records_total;
  uint32_t cd_size;
  uint32_t cd_offset;
  uint16_t comment_len;
});

PACK(struct Zip64EOCDLocator {
  uint8_t magic[4] = {0x50, 0x4B, 0x06, 0x07};
  uint32_t disk_cd_start;
  uint64_t zip64_eocd_offset;
  uint32_t total_disks;
});

PACK(struct Zip64EOCD {
  uint8_t magic[4] = {0x50, 0x4B, 0x06, 0x06};
  uint64_t size_of_record;
  uint16_t version_made_by;
  uint16_t version_needed;
  uint32_t disk_num;
  uint32_t disk_cd_start;
  uint64_t num_records;
  uint64_t num_records_total;
  uint64_t cd_size;
  uint64_t cd_offset;
});

struct ParsedCDHeader {
  CDHeader header;
  uint64_t compressed_size;
  uint64_t uncompressed_size;
  uint64_t local_header_offset;
};

bool FitsSizeT(uint64_t value) {
  return value <= static_cast<uint64_t>(std::numeric_limits<size_t>::max());
}

uint16_t ReadLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t ReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t ReadLe64(const uint8_t* p) {
  return static_cast<uint64_t>(ReadLe32(p)) | (static_cast<uint64_t>(ReadLe32(p + 4)) << 32);
}

void WriteLe16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void WriteLe64(uint8_t* p, uint64_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
  p[4] = static_cast<uint8_t>(v >> 32);
  p[5] = static_cast<uint8_t>(v >> 40);
  p[6] = static_cast<uint8_t>(v >> 48);
  p[7] = static_cast<uint8_t>(v >> 56);
}

bool ParseZip64ExtraField(const uint8_t* extra, uint16_t extra_len, bool need_uncompressed, bool need_compressed,
                          bool need_local_offset, bool need_disk_start, uint64_t* uncompressed_size,
                          uint64_t* compressed_size, uint64_t* local_header_offset, uint32_t* disk_start) {
  const uint8_t* p = extra;
  const uint8_t* end = extra + extra_len;
  while (p + 4 <= end) {
    const uint16_t id = ReadLe16(p);
    const uint16_t field_len = ReadLe16(p + 2);
    p += 4;
    if (p + field_len > end) {
      return false;
    }
    if (id == kZip64ExtraHeaderId) {
      const uint8_t* q = p;
      const uint8_t* field_end = p + field_len;
      if (need_uncompressed) {
        if (q + 8 > field_end) return false;
        *uncompressed_size = ReadLe64(q);
        q += 8;
      }
      if (need_compressed) {
        if (q + 8 > field_end) return false;
        *compressed_size = ReadLe64(q);
        q += 8;
      }
      if (need_local_offset) {
        if (q + 8 > field_end) return false;
        *local_header_offset = ReadLe64(q);
        q += 8;
      }
      if (need_disk_start) {
        if (q + 4 > field_end) return false;
        *disk_start = ReadLe32(q);
      }
      return true;
    }
    p += field_len;
  }
  return !(need_uncompressed || need_compressed || need_local_offset || need_disk_start);
}

bool ParseEOCDCandidate(const uint8_t* fp, size_t size, const uint8_t* p_eocd, size_t zip_offset, uint64_t* cd_offset,
                        uint64_t* cd_size, uint64_t* num_records) {
  EOCD eocd;
  memcpy(&eocd, p_eocd, sizeof(eocd));

  if (eocd.disk_num != 0 || eocd.disk_cd_start != 0) {
    return false;
  }

  *cd_offset = eocd.cd_offset;
  *cd_size = eocd.cd_size;
  *num_records = eocd.num_records_total;

  const bool needs_zip64 = (eocd.num_records == kUint16Max) || (eocd.num_records_total == kUint16Max) ||
                           (eocd.cd_size == kUint32Max) || (eocd.cd_offset == kUint32Max);
  if (!needs_zip64) {
    return true;
  }

  if (p_eocd < fp + sizeof(Zip64EOCDLocator)) {
    return false;
  }

  Zip64EOCDLocator locator;
  memcpy(&locator, p_eocd - sizeof(Zip64EOCDLocator), sizeof(locator));
  if (memcmp(locator.magic, "\x50\x4B\x06\x07", 4) != 0) {
    return false;
  }
  if (locator.disk_cd_start != 0 || locator.total_disks != 1) {
    return false;
  }

  uint64_t zip64_offsets[2];
  size_t num_offsets = 0;
  zip64_offsets[num_offsets++] = locator.zip64_eocd_offset;
  if (zip_offset && locator.zip64_eocd_offset + zip_offset >= locator.zip64_eocd_offset) {
    zip64_offsets[num_offsets++] = locator.zip64_eocd_offset + zip_offset;
  }

  for (size_t i = 0; i < num_offsets; ++i) {
    if (!FitsSizeT(zip64_offsets[i])) {
      continue;
    }
    const size_t zip64_eocd_offset = static_cast<size_t>(zip64_offsets[i]);
    if (zip64_eocd_offset + sizeof(Zip64EOCD) > size) {
      continue;
    }
    Zip64EOCD zip64_eocd;
    memcpy(&zip64_eocd, fp + zip64_eocd_offset, sizeof(zip64_eocd));
    if (memcmp(zip64_eocd.magic, "\x50\x4B\x06\x06", 4) != 0) {
      continue;
    }
    if (zip64_eocd.disk_num != 0 || zip64_eocd.disk_cd_start != 0) {
      return false;
    }
    *num_records = zip64_eocd.num_records_total;
    *cd_size = zip64_eocd.cd_size;
    *cd_offset = zip64_eocd.cd_offset;
    return true;
  }

  return false;
}

bool GetCDHeaders(const uint8_t* fp, size_t size, uint64_t cd_offset, uint64_t cd_size, uint64_t num_records, size_t zip_offset,
                  vector<ParsedCDHeader>* out_cd_headers, size_t* out_base_offset) {
  if (!FitsSizeT(cd_offset) || !FitsSizeT(cd_size) || static_cast<size_t>(cd_offset) > size ||
      static_cast<size_t>(cd_size) > size - static_cast<size_t>(cd_offset)) {
    return false;
  }

  vector<ParsedCDHeader> cd_headers;
  size_t base_offset = 0;
  const uint8_t* p_cdheader = fp + static_cast<size_t>(cd_offset);
  const uint8_t* cd_end = p_cdheader + static_cast<size_t>(cd_size);

  for (uint64_t i = 0; i < num_records; i++) {
    if (p_cdheader + sizeof(CDHeader) > cd_end) {
      return false;
    }

    CDHeader cd_header;
    if (memcmp(p_cdheader, cd_header.magic, sizeof(cd_header.magic)) != 0) {
      if (i != 0 || zip_offset == 0 || p_cdheader + zip_offset + sizeof(CDHeader) > fp + size ||
          cd_end + zip_offset > fp + size ||
          memcmp(p_cdheader + zip_offset, cd_header.magic, sizeof(cd_header.magic)) != 0) {
        return false;
      }
      base_offset = zip_offset;
      p_cdheader += base_offset;
      cd_end += base_offset;
    }

    memcpy(&cd_header, p_cdheader, sizeof(CDHeader));
    const uint8_t* p_name = p_cdheader + sizeof(CDHeader);
    const uint8_t* p_extra = p_name + cd_header.filename_len;
    const uint8_t* p_comment = p_extra + cd_header.extra_field_len;
    const uint8_t* p_next = p_comment + cd_header.comment_len;
    if (p_next > cd_end) {
      return false;
    }

    uint64_t compressed_size = cd_header.compressed_size;
    uint64_t uncompressed_size = cd_header.uncompressed_size;
    uint64_t local_header_offset = cd_header.local_header_offset;
    uint32_t disk_start = cd_header.disk_file_start;

    const bool need_uncompressed = (cd_header.uncompressed_size == kUint32Max);
    const bool need_compressed = (cd_header.compressed_size == kUint32Max);
    const bool need_local_offset = (cd_header.local_header_offset == kUint32Max);
    const bool need_disk_start = (cd_header.disk_file_start == kUint16Max);
    if (!ParseZip64ExtraField(p_extra, cd_header.extra_field_len, need_uncompressed, need_compressed, need_local_offset,
                              need_disk_start, &uncompressed_size, &compressed_size, &local_header_offset, &disk_start)) {
      return false;
    }

    if (disk_start != 0) {
      return false;
    }
    if (!FitsSizeT(local_header_offset) || !FitsSizeT(compressed_size)) {
      return false;
    }

    const size_t local_header_file_offset = base_offset + static_cast<size_t>(local_header_offset);
    if (local_header_file_offset > size || local_header_file_offset + sizeof(LocalHeader) > size) {
      return false;
    }

    LocalHeader local_header;
    memcpy(&local_header, fp + local_header_file_offset, sizeof(LocalHeader));
    if (memcmp(local_header.magic, Zip::header_magic, sizeof(Zip::header_magic)) != 0) {
      return false;
    }

    const size_t local_header_total_size = sizeof(LocalHeader) + local_header.filename_len + local_header.extra_field_len;
    if (local_header_file_offset > size || local_header_total_size > size - local_header_file_offset) {
      return false;
    }
    if (local_header_file_offset + local_header_total_size > size || static_cast<size_t>(compressed_size) > size - local_header_file_offset - local_header_total_size) {
      return false;
    }

    if (local_header.filename_len != cd_header.filename_len ||
        memcmp(fp + local_header_file_offset + sizeof(LocalHeader), p_name, cd_header.filename_len) != 0) {
      return false;
    }

    p_cdheader = p_next;

    ParsedCDHeader parsed{};
    parsed.header = cd_header;
    parsed.compressed_size = compressed_size;
    parsed.uncompressed_size = uncompressed_size;
    parsed.local_header_offset = local_header_offset;
    cd_headers.push_back(parsed);
  }

  std::sort(cd_headers.begin(), cd_headers.end(),
            [](const ParsedCDHeader& a, const ParsedCDHeader& b) { return a.local_header_offset < b.local_header_offset; });

  for (size_t i = 1; i < cd_headers.size(); i++) {
    const size_t prev_local_header_file_offset = base_offset + static_cast<size_t>(cd_headers[i - 1].local_header_offset);
    LocalHeader prev_local_header;
    memcpy(&prev_local_header, fp + prev_local_header_file_offset, sizeof(LocalHeader));

    const uint64_t prev_total_size = sizeof(LocalHeader) + prev_local_header.filename_len + prev_local_header.extra_field_len +
                                     cd_headers[i - 1].compressed_size;
    if (cd_headers[i - 1].local_header_offset + prev_total_size > cd_headers[i].local_header_offset) {
      return false;
    }
  }

  *out_cd_headers = std::move(cd_headers);
  *out_base_offset = base_offset;
  return true;
}

bool CreateTempFilePath(std::string* out_path) {
#ifdef _WIN32
  char temp_path[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, temp_path)) {
    return false;
  }
  char temp_file[MAX_PATH];
  if (!GetTempFileNameA(temp_path, "ect", 0, temp_file)) {
    return false;
  }
  *out_path = temp_file;
  return true;
#else
  std::string tmpl = (std::filesystem::temp_directory_path() / "ectXXXXXX").string();
  std::vector<char> temp_path(tmpl.begin(), tmpl.end());
  temp_path.push_back('\0');
  int fd = mkstemp(temp_path.data());
  if (fd < 0) {
    return false;
  }
  close(fd);
  *out_path = temp_path.data();
  return true;
#endif
}

}  // namespace

uint32_t Zip::RecompressFile(unsigned char* data, uint32_t size, uint32_t size_leanified, string filename, const ECTOptions& Options) {
  bool isZIP = size > sizeof(Zip::header_magic) && memcmp(data, Zip::header_magic, sizeof(Zip::header_magic)) == 0;

  string::size_type dotpos = filename.find_last_of('.');
  if (dotpos == filename.npos) {
    return size;
  }
  string extension = filename.substr(filename.find_last_of('.'));
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  if (!(extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp" || isZIP)) {
    return size;
  }

  std::string tempname;
  if (!CreateTempFilePath(&tempname)) {
    fprintf(stderr, "Error: Can't create temp file for archive entry '%s'\n", filename.c_str());
    return size;
  }
  FILE* stream = fopen(tempname.c_str(), "wb");
  if (!stream) {
    fprintf(stderr, "Error: Can't create temp file for archive entry '%s'\n", filename.c_str());
    unlink(tempname.c_str());
    return size;
  }
  fwrite(data, 1, size, stream);
  fclose(stream);

  if (isZIP) {
    std::vector<int> args;
    args.push_back(0);
    const char* v[1];
    v[0] = tempname.c_str();
    if (zipHandler(args, v, 1, Options)) {
      fprintf(stderr, "Error: Failed to optimize nested archive entry '%s'\n", filename.c_str());
    }
  } else {
    if (fileHandler(tempname.c_str(), Options, 1)) {
      fprintf(stderr, "Error: Failed to optimize archive entry '%s'\n", filename.c_str());
    }
  }
  long long new_size = filesize(tempname.c_str());

  if (new_size < size && new_size >= 0) {
    stream = fopen(tempname.c_str(), "rb");
    if (!stream) {
      fprintf(stderr, "Error: Can't reopen temp file for archive entry '%s'\n", filename.c_str());
      unlink(tempname.c_str());
      return size;
    }
    if (fread(data - size_leanified, 1, new_size, stream) < static_cast<size_t>(new_size)) {
      fprintf(stderr, "Error: Read error for archive entry '%s'\n", filename.c_str());
    } else {
      size = static_cast<uint32_t>(new_size);
    }
    fclose(stream);
  }

  unlink(tempname.c_str());
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

  vector<ParsedCDHeader> cd_headers;
  uint8_t* p_end = fp_ + size_;
  // smallest possible location of EOCD if there's a 64K comment
  uint8_t* p_searchstart = std::max(fp_, p_end - 65535 - sizeof(EOCD::magic));
  uint8_t* p_eocd = nullptr;
  while (true) {
    if (p_eocd != nullptr) {
      cerr << "Warning: Found EOCD at 0x" << std::hex << p_eocd - fp_ << std::dec << ", but it's invalid." << endl;
      p_end = p_eocd;
    }
    uint8_t eocd_magic[4] = {0x50, 0x4B, 0x05, 0x06};
    p_eocd = std::find_end(p_searchstart, p_end, eocd_magic, std::end(eocd_magic));
    if (p_eocd == p_end) {
      cerr << "EOCD not found!" << endl;
      return size_;
    }

    if (p_eocd + sizeof(EOCD) > p_end) {
      continue;
    }

    uint64_t cd_offset = 0;
    uint64_t cd_size = 0;
    uint64_t num_records = 0;
    if (!ParseEOCDCandidate(fp_, size_, p_eocd, zip_offset, &cd_offset, &cd_size, &num_records)) {
      continue;
    }

    // Try to get all CD headers using this EOCD, if everything checks out then proceed.
    if (GetCDHeaders(fp_, size_, cd_offset, cd_size, num_records, zip_offset, &cd_headers, &base_offset)) {
      break;
    }
  }

  uint8_t* fp_w = fp_;
  uint8_t* fp_w_base = fp_w + base_offset;
  memmove(fp_w, fp_, zip_offset);
  uint8_t* p_write = fp_w + zip_offset;

  // Local file header and data.
  for (ParsedCDHeader& cd_header : cd_headers) {
    (*files)++;
    if (!FitsSizeT(cd_header.local_header_offset) || !FitsSizeT(cd_header.compressed_size) ||
        base_offset + static_cast<size_t>(cd_header.local_header_offset) > size_) {
      cerr << "Zip64 entry exceeds processable in-memory size, skipping this archive." << endl;
      return size_;
    }

    uint8_t* p_read_header = fp_ + base_offset + static_cast<size_t>(cd_header.local_header_offset);
    LocalHeader local_header_in{};
    if (p_read_header + sizeof(LocalHeader) > fp_ + size_) {
      cerr << "Invalid local header offset in central directory." << endl;
      return size_;
    }
    memcpy(&local_header_in, p_read_header, sizeof(LocalHeader));
    if (memcmp(local_header_in.magic, header_magic, sizeof(header_magic)) != 0) {
      cerr << "Invalid local header signature." << endl;
      return size_;
    }

    const size_t local_header_total = sizeof(LocalHeader) + local_header_in.filename_len + local_header_in.extra_field_len;
    if (local_header_total > size_ || p_read_header + local_header_total > fp_ + size_) {
      cerr << "Invalid local header lengths." << endl;
      return size_;
    }

    uint8_t* p_read_data = p_read_header + local_header_total;
    const size_t compressed_size_in = static_cast<size_t>(cd_header.compressed_size);
    if (p_read_data + compressed_size_in > fp_ + size_) {
      cerr << "Compressed size too large: " << cd_header.compressed_size << endl;
      return size_;
    }

    cd_header.local_header_offset = static_cast<uint64_t>(p_write - fp_w_base);

    LocalHeader local_header_out = local_header_in;
    local_header_out.extra_field_len = 0;
    if (local_header_out.flag & 8) {
      // set this bit to 0, we don't use data descriptor to save space
      local_header_out.flag &= static_cast<uint16_t>(~8U);
      cd_header.header.flag &= static_cast<uint16_t>(~8U);
      // use central directory values
      local_header_out.crc32 = cd_header.header.crc32;
      local_header_out.compressed_size = cd_header.header.compressed_size;
      local_header_out.uncompressed_size = cd_header.header.uncompressed_size;
    }

    memcpy(p_write, &local_header_out, sizeof(LocalHeader));
    memcpy(p_write + sizeof(LocalHeader), p_read_header + sizeof(LocalHeader), local_header_out.filename_len);

    LocalHeader* local_header = reinterpret_cast<LocalHeader*>(p_write);
    string filename(reinterpret_cast<char*>(p_write) + sizeof(LocalHeader), local_header->filename_len);
    uint8_t* entry_data_write = p_write + sizeof(LocalHeader) + local_header->filename_len;
    p_write = entry_data_write;

    uint16_t final_method = local_header->compression_method;
    uint64_t final_compressed_size = cd_header.compressed_size;
    uint64_t final_uncompressed_size = cd_header.uncompressed_size;
    uint32_t final_crc = cd_header.header.crc32;

    // If the method is store, Leanify the embedded file and try deflate recompression in non-strict mode.
    if (local_header->compression_method == 0) {
      if (cd_header.compressed_size != 0) {
        if (!Options.Strict && cd_header.compressed_size <= kUint32Max) {
          uint32_t new_uncomp_size =
              RecompressFile(p_read_data, static_cast<uint32_t>(cd_header.compressed_size),
                             static_cast<uint32_t>(p_read_data - entry_data_write), filename, Options);
          if (new_uncomp_size == cd_header.compressed_size) {
            memmove(entry_data_write, p_read_data, static_cast<size_t>(cd_header.compressed_size));
          }
          final_crc = static_cast<uint32_t>(crc32(0, entry_data_write, new_uncomp_size));

          bool stored_as_deflate = false;
          uint8_t* compress_buf = nullptr;
          size_t new_comp_size = 0;
          if (new_uncomp_size && !Options.Strict) {
            ZopfliBuffer(Options.Mode, Options.DeflateMultithreading, entry_data_write, new_uncomp_size, &compress_buf,
                         &new_comp_size);
            if (new_comp_size < new_uncomp_size) {
              final_method = 8;
              final_compressed_size = new_comp_size;
              final_uncompressed_size = new_uncomp_size;
              memcpy(entry_data_write, compress_buf, new_comp_size);
              stored_as_deflate = true;
            }
          }
          if (compress_buf) {
            delete[] compress_buf;
          }
          if (!stored_as_deflate) {
            final_method = 0;
            final_compressed_size = new_uncomp_size;
            final_uncompressed_size = new_uncomp_size;
          }
        } else {
          memmove(entry_data_write, p_read_data, static_cast<size_t>(cd_header.compressed_size));
          final_method = 0;
          final_compressed_size = cd_header.compressed_size;
          final_uncompressed_size = cd_header.uncompressed_size;
        }
      } else {
        final_method = 0;
        final_compressed_size = 0;
        final_uncompressed_size = 0;
      }
    } else if (local_header->compression_method != 8 || (local_header->flag & 1) ||
               cd_header.compressed_size > kUint32Max || cd_header.uncompressed_size > kUint32Max) {
      // Unsupported compression method, encrypted or too large for in-memory deflate path: copy as-is.
      memmove(entry_data_write, p_read_data, static_cast<size_t>(cd_header.compressed_size));
      final_method = local_header->compression_method;
      final_compressed_size = cd_header.compressed_size;
      final_uncompressed_size = cd_header.uncompressed_size;
      final_crc = cd_header.header.crc32;
    } else if (cd_header.uncompressed_size == 0) {
      // Switch from deflate to store for empty file.
      final_method = 0;
      final_compressed_size = 0;
      final_uncompressed_size = 0;
      final_crc = 0;
    } else {
      // deflate -> inflate -> optimize -> recompress
      size_t decompressed_size = 0;
      uint8_t* decompress_buf = nullptr;
      unsigned error = lodepng_inflate(reinterpret_cast<unsigned char**>(&decompress_buf), &decompressed_size,
                                       reinterpret_cast<unsigned char*>(p_read_data),
                                       static_cast<size_t>(cd_header.compressed_size));

      if (error || decompressed_size != cd_header.uncompressed_size ||
          cd_header.header.crc32 != crc32(0, decompress_buf, static_cast<uInt>(cd_header.uncompressed_size))) {
        cerr << "Decompression failed or CRC32 mismatch, skipping this file." << endl;
        free(decompress_buf);
        memmove(entry_data_write, p_read_data, static_cast<size_t>(cd_header.compressed_size));
        final_method = local_header->compression_method;
        final_compressed_size = cd_header.compressed_size;
        final_uncompressed_size = cd_header.uncompressed_size;
        final_crc = cd_header.header.crc32;
      } else {
        // Allocate 16 more bytes to accommodate optimized deflate match finder.
        decompress_buf = reinterpret_cast<uint8_t*>(realloc(decompress_buf, decompressed_size + 16));
        uint32_t new_uncomp_size = Options.Strict ? static_cast<uint32_t>(cd_header.uncompressed_size)
                                                  : RecompressFile(decompress_buf, static_cast<uint32_t>(decompressed_size),
                                                                   0, filename, Options);

        uint8_t* compress_buf = nullptr;
        size_t new_comp_size = 0;
        ZopfliBuffer(Options.Mode, Options.DeflateMultithreading, decompress_buf, new_uncomp_size, &compress_buf,
                     &new_comp_size);

        final_crc = static_cast<uint32_t>(crc32(0, decompress_buf, new_uncomp_size));
        if (new_uncomp_size <= new_comp_size && new_uncomp_size <= cd_header.compressed_size) {
          final_method = 0;
          final_compressed_size = new_uncomp_size;
          final_uncompressed_size = new_uncomp_size;
          memcpy(entry_data_write, decompress_buf, new_uncomp_size);
        } else if (new_comp_size < cd_header.compressed_size) {
          final_method = 8;
          final_compressed_size = new_comp_size;
          final_uncompressed_size = new_uncomp_size;
          memcpy(entry_data_write, compress_buf, new_comp_size);
        } else {
          final_method = local_header->compression_method;
          final_compressed_size = cd_header.compressed_size;
          final_uncompressed_size = cd_header.uncompressed_size;
          final_crc = cd_header.header.crc32;
          memmove(entry_data_write, p_read_data, static_cast<size_t>(cd_header.compressed_size));
        }

        free(decompress_buf);
        delete[] compress_buf;
      }
    }

    // Write/insert local Zip64 extra only when needed by size fields.
    const bool local_needs_zip64 = final_compressed_size > kUint32Max || final_uncompressed_size > kUint32Max;
    if (local_needs_zip64) {
      uint8_t zip64_extra[kZip64LocalExtraLength];
      WriteLe16(zip64_extra, kZip64ExtraHeaderId);
      WriteLe16(zip64_extra + 2, 16);
      WriteLe64(zip64_extra + 4, final_uncompressed_size);
      WriteLe64(zip64_extra + 12, final_compressed_size);
      memmove(entry_data_write + kZip64LocalExtraLength, entry_data_write, static_cast<size_t>(final_compressed_size));
      memcpy(entry_data_write, zip64_extra, sizeof(zip64_extra));
      local_header->extra_field_len = static_cast<uint16_t>(kZip64LocalExtraLength);
      p_write += static_cast<size_t>(final_compressed_size) + kZip64LocalExtraLength;
      local_header->version_needed = std::max<uint16_t>(local_header->version_needed, 45);
    } else {
      local_header->extra_field_len = 0;
      p_write += static_cast<size_t>(final_compressed_size);
    }

    local_header->compression_method = final_method;
    local_header->crc32 = final_crc;
    local_header->compressed_size =
        local_needs_zip64 ? kUint32Max : static_cast<uint32_t>(final_compressed_size);
    local_header->uncompressed_size =
        local_needs_zip64 ? kUint32Max : static_cast<uint32_t>(final_uncompressed_size);

    cd_header.header.version_needed = local_header->version_needed;
    cd_header.header.flag = local_header->flag;
    cd_header.header.compression_method = final_method;
    cd_header.header.crc32 = final_crc;
    cd_header.compressed_size = final_compressed_size;
    cd_header.uncompressed_size = final_uncompressed_size;
  }

  // central directory offset (relative to base offset convention).
  const uint64_t cd_offset64 = static_cast<uint64_t>(p_write - fp_w_base);
  for (ParsedCDHeader& cd_header : cd_headers) {
    CDHeader cd_out = cd_header.header;
    cd_out.comment_len = 0;
    cd_out.disk_file_start = 0;

    uint8_t cd_zip64_extra[4 + 24];
    uint16_t cd_zip64_extra_len = 0;

    if (cd_header.uncompressed_size > kUint32Max || cd_header.compressed_size > kUint32Max ||
        cd_header.local_header_offset > kUint32Max) {
      uint8_t* p_extra = cd_zip64_extra;
      WriteLe16(p_extra, kZip64ExtraHeaderId);
      p_extra += 2;
      uint16_t data_len = 0;
      if (cd_header.uncompressed_size > kUint32Max) data_len += 8;
      if (cd_header.compressed_size > kUint32Max) data_len += 8;
      if (cd_header.local_header_offset > kUint32Max) data_len += 8;
      WriteLe16(p_extra, data_len);
      p_extra += 2;
      if (cd_header.uncompressed_size > kUint32Max) {
        WriteLe64(p_extra, cd_header.uncompressed_size);
        p_extra += 8;
        cd_out.uncompressed_size = kUint32Max;
      } else {
        cd_out.uncompressed_size = static_cast<uint32_t>(cd_header.uncompressed_size);
      }
      if (cd_header.compressed_size > kUint32Max) {
        WriteLe64(p_extra, cd_header.compressed_size);
        p_extra += 8;
        cd_out.compressed_size = kUint32Max;
      } else {
        cd_out.compressed_size = static_cast<uint32_t>(cd_header.compressed_size);
      }
      if (cd_header.local_header_offset > kUint32Max) {
        WriteLe64(p_extra, cd_header.local_header_offset);
        p_extra += 8;
        cd_out.local_header_offset = kUint32Max;
      } else {
        cd_out.local_header_offset = static_cast<uint32_t>(cd_header.local_header_offset);
      }
      cd_zip64_extra_len = static_cast<uint16_t>(p_extra - cd_zip64_extra);
      cd_out.extra_field_len = cd_zip64_extra_len;
      cd_out.version_needed = std::max<uint16_t>(cd_out.version_needed, 45);
    } else {
      cd_out.uncompressed_size = static_cast<uint32_t>(cd_header.uncompressed_size);
      cd_out.compressed_size = static_cast<uint32_t>(cd_header.compressed_size);
      cd_out.local_header_offset = static_cast<uint32_t>(cd_header.local_header_offset);
      cd_out.extra_field_len = 0;
    }

    memcpy(p_write, &cd_out, sizeof(CDHeader));
    p_write += sizeof(CDHeader);

    // Copy filename from rewritten local file header.
    memcpy(p_write, fp_w_base + static_cast<size_t>(cd_header.local_header_offset) + sizeof(LocalHeader),
           cd_out.filename_len);
    p_write += cd_out.filename_len;

    if (cd_zip64_extra_len) {
      memcpy(p_write, cd_zip64_extra, cd_zip64_extra_len);
      p_write += cd_zip64_extra_len;
    }
  }

  const uint64_t cd_size64 = static_cast<uint64_t>(p_write - fp_w_base) - cd_offset64;
  const uint64_t total_records = cd_headers.size();
  const bool need_zip64_archive = total_records > kUint16Max || cd_size64 > kUint32Max || cd_offset64 > kUint32Max;

  if (need_zip64_archive) {
    const uint64_t zip64_eocd_offset = static_cast<uint64_t>(p_write - fp_w_base);

    Zip64EOCD zip64_eocd{};
    zip64_eocd.size_of_record = 44;
    zip64_eocd.version_made_by = 45;
    zip64_eocd.version_needed = 45;
    zip64_eocd.disk_num = 0;
    zip64_eocd.disk_cd_start = 0;
    zip64_eocd.num_records = total_records;
    zip64_eocd.num_records_total = total_records;
    zip64_eocd.cd_size = cd_size64;
    zip64_eocd.cd_offset = cd_offset64;
    memcpy(p_write, &zip64_eocd, sizeof(zip64_eocd));
    p_write += sizeof(zip64_eocd);

    Zip64EOCDLocator locator{};
    locator.disk_cd_start = 0;
    locator.zip64_eocd_offset = zip64_eocd_offset;
    locator.total_disks = 1;
    memcpy(p_write, &locator, sizeof(locator));
    p_write += sizeof(locator);
  }

  EOCD eocd{};
  eocd.disk_num = 0;
  eocd.disk_cd_start = 0;
  eocd.num_records = need_zip64_archive ? kUint16Max : static_cast<uint16_t>(total_records);
  eocd.num_records_total = need_zip64_archive ? kUint16Max : static_cast<uint16_t>(total_records);
  eocd.cd_size = need_zip64_archive ? kUint32Max : static_cast<uint32_t>(cd_size64);
  eocd.cd_offset = need_zip64_archive ? kUint32Max : static_cast<uint32_t>(cd_offset64);
  eocd.comment_len = 0;
  memcpy(p_write, &eocd, sizeof(EOCD));

  size_ = p_write + sizeof(EOCD) - fp_;
  return size_;
}
