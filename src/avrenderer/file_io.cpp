#include "file_io.h"

std::vector<char> readFile(const std::string &filename) {
  // open the file:
  std::ifstream file(filename, std::ios::binary);

  // Stop eating new lines in binary mode!!!
  file.unsetf(std::ios::skipws);

  // get its size:
  std::streampos fileSize;

  file.seekg(0, std::ios::end);
  fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // reserve capacity
  std::vector<char> vec;
  vec.resize(fileSize);

  // read the data:
  file.read(vec.data(), fileSize);

  return vec;
}