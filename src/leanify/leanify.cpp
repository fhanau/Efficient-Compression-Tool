#include <fcntl.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "../miniz/miniz.h"
#include "../zopfli/zlib_container.h"

#include "zip.h"
#include "fileio.h"

using std::string;

#ifdef _WIN32
File::File(const char* filepath) {
  fp_ = nullptr;
  hFile_ = CreateFile(filepath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (hFile_ == INVALID_HANDLE_VALUE) {
    size_ = 0;
    return;
  }
  size_ = GetFileSize(hFile_, nullptr);
  hMap_ = CreateFileMapping(hFile_, nullptr, PAGE_READWRITE, 0, 0, nullptr);
  if (hMap_ == INVALID_HANDLE_VALUE) {
    return;
  }
  fp_ = MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
}

void File::UnMapFile(size_t new_size) {
  if (new_size < size_)
    FlushViewOfFile(fp_, 0);

  UnmapViewOfFile(fp_);

  CloseHandle(hMap_);
  if (new_size) {
    SetFilePointer(hFile_, new_size, nullptr, FILE_BEGIN);
    SetEndOfFile(hFile_);
  }
  CloseHandle(hFile_);
  fp_ = nullptr;
}
#else
File::File(const char* filepath) {
  fp_ = nullptr;
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
  fp_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (fp_ == MAP_FAILED) {
    perror("Map file error");
    fp_ = nullptr;
  }
}

void File::UnMapFile(size_t new_size) {
  if (munmap(fp_, size_) == -1)
    perror("munmap");
  if (new_size)
    if (ftruncate(fd_, new_size) == -1)
      perror("ftruncate");

  close(fd_);
  fp_ = nullptr;
}
#endif

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
    input_file.UnMapFile(new_size);
  }
}
