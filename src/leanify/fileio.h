#ifndef FILEIO_H_
#define FILEIO_H_

#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <Windows.h>
#endif

class File {
 public:
  explicit File(const char* filepath);

  void* GetFilePionter() const {
    return data_;
  }

  unsigned int GetSize() const {
    return size_;
  }

  bool IsOK() const {
    return data_ != nullptr;
  }

  void UnMap();
  void Write(size_t new_size, const char* filepath);

 private:
#ifdef _WIN32
  HANDLE hFile_, hMap_;
#else
  int fd_;
#endif // _WIN32
  void* data_;
  size_t size_;
};

#endif  // FILEIO_H_
