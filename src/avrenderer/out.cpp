#include "out.h"

// std::ofstream out;

std::ostream &getOut() {
  /* if (!out.is_open()) {
    std::string logPath = dllDir + "log" + logSuffix + ".txt";
    out.open(logPath.c_str(), std::ofstream::out|std::ofstream::app|std::ofstream::binary);
    out << "--------------------------------------------------------------------------------" << std::endl;
  } */
  return std::cout;
}