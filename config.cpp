#include "config.h"
#include <string>
#include <fstream>
#include <cassert>
#include "third_party/json.hpp"
#include <iostream>

using json = nlohmann::json;

namespace puck {
Config::Config() : mid_(0) {

}

void Config::Init(std::string filename) {
  std::ifstream in(filename.c_str(), std::ios::in);
  json j;
  in >> j;
  mid_ = j["id"].get<uint64_t>();
  for (json::iterator it = j["nodes"].begin(); it != j["nodes"].end(); ++it) {
    uint64_t id = (*it)["id"].get<uint64_t>();
    std::string iport = (*it)["address"].get<std::string>();
    assert(others_.find(id) == others_.end());
    others_[id] = iport;
  }
  in.close();
}
}
