#ifndef LOG_H
#define LOG_H

#include <string>

namespace puck {
void LogInfo(const std::string& message, const char* filename, int line);

void LogWarn(const std::string& message, const char* filename, int line);

void LogError(const std::string& message, const char* filename, int line);

namespace detail {
template <typename T>
std::string convert_to_string(const T& t) {
  return std::to_string(t);
}

inline std::string convert_to_string(const std::string& str) {
  return str;
}

inline std::string convert_to_string(const char* ptr) {
  return std::string(ptr);
}

template <size_t n>
std::string convert_to_string(const char(&ref)[n]) {
  return std::string(ref);
}
}

template <typename LastType>
std::string piece(LastType&& last) {
  return detail::convert_to_string(std::forward<LastType>(last));
}

template <typename ThisType, typename... Types>
std::string piece(ThisType&& tt, Types&&... args) {
  std::string tmp(detail::convert_to_string(std::forward<ThisType>(tt)));
  tmp.append(piece(std::forward<Types>(args)...));
  return tmp;
}
}

// need c++11 : function-like macro with variable number of arguments.
#define INFO(...) puck::LogInfo(puck::piece(__VA_ARGS__), __FILE__, __LINE__)
#define WARN(...) puck::LogWarn(puck::piece(__VA_ARGS__), __FILE__, __LINE__)
#define ERROR(...) puck::LogError(puck::piece(__VA_ARGS__), __FILE__, __LINE__)

#endif // LOG_H
