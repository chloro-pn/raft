#include "util.h"
#include "unistd.h"
#include "time.h"
#include "arpa/inet.h"
#include <sstream>
#include <cstdlib>
#include <cassert>
#include <mutex>

namespace puck {
namespace util {
uint16_t HostToNetwork(uint16_t n) {
  return htons(n);
}

uint16_t NetworkToHost(uint16_t n) {
  return ntohs(n);
}

uint32_t HostToNetwork(uint32_t n) {
  return htonl(n);
}

uint32_t NetworkToHost(uint32_t n) {
  return ntohl(n);
}

void SleepS(int s) {
  sleep(s);
}

void SleepUs(int us) {
  usleep(us);
}

std::string GetIpFromIport(const std::string& iport) {
  size_t count = 0;
  for(count = 0; count < iport.size(); ++count) {
    if(iport[count] == ':') {
      return iport.substr(0, count);
    }
  }
  return "";
}

int32_t GetPortFromIport(const std::string& iport) {
  size_t count = 0;
  for(count = 0; count < iport.size(); ++count) {
    if(iport[count] == ':') {
      std::stringstream ss;
      ss << iport.substr(count + 1);
      uint16_t port = 0;
      ss >> port;
      return port;
    }
  }
  return -1;
}

// [from, to]
uint32_t GetRandomFromTo(uint32_t from, uint32_t to) {
  assert(to >= from);
  static std::once_flag obj;
  std::call_once(obj, []()->void {
    srand(static_cast<unsigned int>(time(nullptr)));
  });
  return static_cast<uint32_t>(rand() % ( to - from + 1) + from);
}
}
}
