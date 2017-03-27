#ifndef FILEIO_H_
#define FILEIO_H_

#include <sys/stat.h>
#ifdef _WIN32
#include <Windows.h>
#endif

class File {
 public:
#ifdef _WIN32
  explicit File(const wchar_t* filepath);
#else
  explicit File(const char* filepath);
#endif

  void* GetFilePionter() const {
    return fp_;
  }

  unsigned int GetSize() const {
    return size_;
  }

  bool IsOK() const {
    return fp_ != nullptr;
  }

  void UnMapFile(size_t new_size);

 private:
#ifdef _WIN32
  HANDLE hFile_, hMap_;
#else
  int fd_;
#endif // _WIN32
  void* fp_;
  size_t size_;
};

#endif  // FILEIO_H_
