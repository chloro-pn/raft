#include "log.h"
#include <iostream>
#include <cstdlib>

namespace puck {
void LogInfo(const std::string& message, const char* filename, int line) {
  std::cout << "[info] " << message << " [[" << filename << "]]" << " [[" << line << "]]" << std::endl;
}

void LogWarn(const std::string& message, const char* filename, int line) {
  std::cout << "[warn] " << message << " [[" << filename << "]]" << " [[" << line << "]]" << std::endl;
}

void LogError(const std::string& message, const char* filename, int line) {
  std::cerr << "[error] " << message << " [[" << filename << "]]" << " [[" << line << "]]" << std::endl;
  exit(-1);
}
}
