#ifndef _openvr_test_out_h_
#define _openvr_test_out_h_

#include <iostream>
// #include <fstream>
#include <functional>

extern std::string dllDir;
// extern std::ofstream out;
extern std::string logSuffix;

std::ostream &getOut();
void TRACE(const char *module, const std::function<void()> &fn);

#endif