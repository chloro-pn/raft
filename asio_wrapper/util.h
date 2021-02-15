#ifndef UTIL_H
#define UTIL_H

#include <cstdint>
#include <string>

namespace puck {
namespace util {
uint16_t HostToNetwork(uint16_t n);

uint16_t NetworkToHost(uint16_t n);

uint32_t HostToNetwork(uint32_t n);

uint32_t NetworkToHost(uint32_t n);

void SleepS(int s);

void SleepUs(int us);

std::string GetIpFromIport(const std::string& iport);

int32_t GetPortFromIport(const std::string& iport);

uint32_t GetRandomFromTo(uint32_t from, uint32_t to);
}
}

#endif // UTIL_H
