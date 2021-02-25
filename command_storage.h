#ifndef COMMAND_STORAGE_H
#define COMMAND_STORAGE_H

#include <cstdint>
#include <vector>
#include <string>
#include <cassert>

namespace raft {
// empty item : command_.empty() == true.
struct Item {
  uint64_t term_;
  std::string command_;
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

  uint64_t GetTermFromIndex(uint64_t index) const {
    return logs_.at(index).term_;
  }

  const std::string& GetCommandFromIndex(uint64_t index) const {
    return logs_.at(index).command_;
  }

  // 当前版本不考虑一次发送最大logs的限制.
  std::vector<std::string> GetLogsAfterPrevLog(uint64_t index) const {
    std::vector<std::string> result;
    for(uint64_t i = index + 1; i < logs_.size(); ++i) {
      result.push_back(logs_[i].command_);
    }
    return result;
  }

  size_t Size() const {
    return logs_.size();
  }

  bool MoreOrEqualUpToDate(uint64_t index, uint64_t term) const {
    if(term > GetLastLogTerm()) {
      return true;
    } else if(term == GetLastLogTerm() && index >= GetLastLogIndex()) {
      return true;
    }
    return false;
  }

  bool Check(uint64_t index, uint64_t term) const {
    if(logs_.size() <= index) {
      return false;
    }
    if(logs_[index].term_ != term) {
      return false;
    }
    return true;
  }

  void RewriteOrAppendAfter(uint64_t index, uint64_t current_term, const std::vector<std::string>& logs) {
    assert(index < logs_.size());
    uint64_t count = 0;
    for(uint64_t i = index + 1; i < logs_.size() && count < logs.size(); ++i) {
      logs_[i].command_ = logs[count];
      ++count;
    }
    for(uint64_t i = count; i < logs.size(); ++i) {
      Item tmp;
      tmp.term_ = current_term;
      tmp.command_ = logs[i];
      logs_.push_back(tmp);
    }
  }

private:
  std::vector<Item> logs_;
};
}

#endif // COMMAND_STORAGE_H
