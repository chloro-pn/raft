#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <unordered_map>

namespace raft {
class Config {
public:
  static Config& instance() {
    static Config obj;
    return obj;
  }

  void Init(std::string filename);

  uint64_t MyId() const {
    return mid_;
  }

  const std::unordered_map<uint64_t, std::string>& Nodes() const {
    return others_;
  }

private:
  Config();

  uint64_t mid_;
  std::unordered_map<uint64_t, std::string> others_;
};
}

#endif // CONFIG_H
