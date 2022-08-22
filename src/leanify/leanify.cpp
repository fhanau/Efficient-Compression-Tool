#include <fcntl.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#ifdef _MSC_VER
#include <io.h>
#endif
#endif

#include "../miniz/miniz.h"
#include "../zopfli/zlib_container.h"

#include "zip.h"
#include "fileio.h"

using std::string;

#ifdef _WIN32
File::File(const char* filepath) {
  data_ = nullptr;
  hFile_ = CreateFile(filepath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (hFile_ == INVALID_HANDLE_VALUE) {
    size_ = 0;
    return;
  }
  size_ = GetFileSize(hFile_, nullptr);
  hMap_ = CreateFileMapping(hFile_, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (hMap_ == INVALID_HANDLE_VALUE) {
    return;
  }
  data_ = MapViewOfFile(hMap_, FILE_MAP_COPY, 0, 0, 0);
}

void File::UnMap() {
  UnmapViewOfFile(data_);

  CloseHandle(hMap_);
  CloseHandle(hFile_);
  data_ = nullptr;
}
#else
File::File(const char* filepath) {
  data_ = nullptr;
  fd_ = open(filepath, O_RDWR);

  if (fd_ == -1) {
    perror("Open file error");
    return;
  }

  struct stat sb;
  if (fstat(fd_, &sb) == -1) {
    perror("fstat");
    return;
  }
  size_ = sb.st_size;

  // map the file into memory
  data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd_, 0);
  if (data_ == MAP_FAILED) {
    perror("Map file error");
    data_ = nullptr;
  }
}

void File::UnMap() {
  if (munmap(data_, size_) == -1)
    perror("munmap");
  close(fd_);
  data_ = nullptr;
}
#endif

void File::Write(size_t new_size, const char* filepath) {
  if (new_size && new_size < size_) {
    string filepath_tmp = filepath;
    filepath_tmp.append(".tmp");
    struct stat st;
    if (stat(filepath_tmp.c_str(), &st) == 0) {
      fprintf(stderr, "%s: temp file name exists\n", filepath_tmp.c_str());
      UnMap();
      return;
    }
    FILE* new_fp = fopen(filepath_tmp.c_str(), "wb");
    if (!new_fp) {
      perror("Open file error");
      UnMap();
      return;
    }
    if (fwrite(data_, 1, new_size, new_fp) != new_size) {
      perror("fwrite");
      UnMap();
      return;
    }

    fclose(new_fp);
    UnMap();
#ifdef WIN32
    if (MoveFileExA(filepath_tmp.c_str(), filepath, MOVEFILE_REPLACE_EXISTING) == 0) {
      fprintf(stderr, "%s: zip replace file error\n", filepath);
    }
#else
    if (rename(filepath_tmp.c_str(), filepath)) {
      perror("rename");
    }
#endif
  }
}

// Leanify the file
// and move the file ahead size_leanified bytes
// the new location of the file will be file_pointer - size_leanified
// it's designed this way to avoid extra memmove or memcpy
// return new size
static size_t LeanifyFile(void* file_pointer, size_t file_size, const ECTOptions& Options, size_t* files) {
  if (memcmp(file_pointer, Zip::header_magic, sizeof(Zip::header_magic)) != 0) {
    return file_size;
  }

  Zip* f = new Zip(file_pointer, file_size);
  size_t r = f->Leanify(Options, files);
  delete f;
  return r;
}

void ReZipFile(const char* file_path, const ECTOptions& Options, size_t* files) {
  string filename(file_path);
  File input_file(file_path);

  if (input_file.IsOK()) {
    size_t original_size = input_file.GetSize();

    size_t new_size = LeanifyFile(input_file.GetFilePionter(), original_size, Options, files);
    input_file.Write(new_size, file_path);
  }
}
