#ifndef _avrenderer_file_io_h_
#define _avrenderer_file_io_h_

#include <vector>
#include <string>
#include <iterator>
#include <fstream>

std::vector<char> readFile(const std::string &filename);

#endif