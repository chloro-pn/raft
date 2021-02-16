#ifndef COMMAND_STORAGE_H
#define COMMAND_STORAGE_H

#include <cstdint>
#include <vector>

namespace raft {
// empty item : command_.empty() == true.
struct Item {
  uint64_t term_;
  std::vector<uint8_t> command_;
};

class CommandStorage {
public:
  CommandStorage() : logs_() {
    // logs_ first index == 1，所以需要在开头放一个空的item
    // 第一条日志的term一定是0
    Item item;
    item.term_ = 0;
    item.command_.clear();
    logs_.push_back(item);
  }

  void Storage(const Item& item) {
    logs_.push_back(item);
  }

  uint64_t GetLastLogIndex() const {
    // logs_.size() >= 1.
    return logs_.size() - 1;
  }

  uint64_t GetLastLogTerm() const {
    // logs_ never be empty.
    return logs_.back().term_;
  }

private:
  std::vector<Item> logs_;
};
}

#endif // COMMAND_STORAGE_H
