#ifndef FORMATS_ZIP_H_
#define FORMATS_ZIP_H_

#include "../main.h"

class Zip {
 public:
  explicit Zip(void* p, size_t s) : fp_(static_cast<uint8_t*>(p)), size_(s) {}

  size_t Leanify(const ECTOptions& Options, unsigned long* files);
  uint32_t RecompressFile(unsigned char* data, uint32_t size, uint32_t size_leanified, std::string filename, const ECTOptions& Options);

  static const uint8_t header_magic[4];

protected:
  // pointer to the file content
  uint8_t* fp_;
  // size of the file
  size_t size_;
};

#endif  // FORMATS_ZIP_H_
