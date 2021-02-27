#ifndef COMMAND_STORAGE_H
#define COMMAND_STORAGE_H

#include <cstdint>
#include <vector>
#include <string>
#include <cassert>
#include "storage_engine_base.h"
#include "log.h"

namespace raft {
// empty item : command_.empty() == true.

class CommandStorage : public StorageEngineBase {
public:
  CommandStorage() : logs_(),
                     current_term_(-1),
                     voted_for_(-1) {
    // logs_ first index == 1，所以需要在开头放一个空的item
    // 第一条日志的term一定是0
    Item item {0, ""};
    logs_.push_back(item);
  }
  void Init() override {
    // 严格来说这个版本的存储是不符合要求的，无法持久化存储，因此
    // 本函数只能在节点启动时调用，不能在重启时调用。
    current_term_ = 0;
    voted_for_ = -1;
  }

  void Storage(const Item& item) override {
    logs_.push_back(item);
  }

  void Storage(uint64_t current_term, uint64_t voted_for) override {
    current_term_ = static_cast<int64_t>(current_term);
    voted_for_ = static_cast<int64_t>(voted_for);
  }

  uint64_t GetCurrentTerm() const override {
    return current_term_;
  }

  uint64_t GetVotedFor() const override {
    return voted_for_;
  }

  uint64_t GetLastLogIndex() const override {
    // logs_.size() >= 1.
    return logs_.size() - 1;
  }

  uint64_t GetLastLogTerm() const override {
    // logs_ never be empty.
    return logs_.back().term_;
  }

  uint64_t GetTermFromIndex(uint64_t index) const override {
    return logs_.at(index).term_;
  }

  const std::string& GetCommandFromIndex(uint64_t index) const override {
    return logs_.at(index).command_;
  }

  // 当前版本不考虑一次发送最大logs的限制.
  std::vector<std::string> GetLogsAfterPrevLog(uint64_t index) const override {
    std::vector<std::string> result;
    for(uint64_t i = index + 1; i < logs_.size(); ++i) {
      result.push_back(logs_[i].command_);
    }
    return result;
  }

  size_t Size() const override {
    return logs_.size();
  }

  bool MoreOrEqualUpToDate(uint64_t index, uint64_t term) const override {
    if(term > GetLastLogTerm()) {
      return true;
    } else if(term == GetLastLogTerm() && index >= GetLastLogIndex()) {
      return true;
    }
    return false;
  }

  bool Check(uint64_t index, uint64_t term) const override {
    if(logs_.size() <= index) {
      return false;
    }
    if(logs_[index].term_ != term) {
      return false;
    }
    return true;
  }

  // fix stupid bug.
  void RewriteOrAppendAfter(uint64_t index, uint64_t current_term, const std::vector<std::string>& logs) override {
    assert(index < logs_.size());
    uint64_t count = 0;
    for(uint64_t i = index + 1; i < logs_.size() && count < logs.size(); ++i) {
      logs_[i].term_ = current_term;
      logs_[i].command_ = logs[count];
      ++count;
    }
    for(uint64_t i = count; i < logs.size(); ++i) {
      Item tmp {current_term, logs[i]};
      logs_.push_back(tmp);
    }
  }

  ~CommandStorage() override {

  }
private:
  std::vector<Item> logs_;
  int64_t current_term_;
  int64_t voted_for_;
};
}

#endif // COMMAND_STORAGE_H
