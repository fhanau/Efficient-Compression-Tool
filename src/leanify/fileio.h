#ifndef FILEIO_H_
#define FILEIO_H_

#include <sys/stat.h>

class File {
 public:
  explicit File(const char* filepath);

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
  int fd_;
  void* fp_;
  size_t size_;
};

#endif  // FILEIO_H_
