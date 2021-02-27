#ifndef STORAGE_ENGINE_BASE_H
#define STORAGE_ENGINE_BASE_H

#include <cstdint>
#include <string>
#include <vector>

namespace raft {
struct Item {
  uint64_t term_;
  std::string command_;

  Item(uint64_t t, const std::string& c) : term_(t), command_(c) {

  }
};

class StorageEngineBase {
public:
  //当节点首次启动或者recover时，调用该函数，恢复状态
  virtual void Init() = 0;
  virtual void Storage(const Item& item) = 0;
  virtual void Storage(uint64_t current_term, uint64_t voted_for) = 0;
  virtual uint64_t GetCurrentTerm() const = 0;
  virtual uint64_t GetVotedFor() const = 0;
  virtual uint64_t GetLastLogIndex() const = 0;
  virtual uint64_t GetLastLogTerm() const = 0;
  virtual uint64_t GetTermFromIndex(uint64_t index) const = 0;
  virtual const std::string& GetCommandFromIndex(uint64_t index) const = 0;
  virtual std::vector<std::string> GetLogsAfterPrevLog(uint64_t index) const = 0;
  virtual size_t Size() const = 0;
  virtual bool MoreOrEqualUpToDate(uint64_t index, uint64_t term) const = 0;
  virtual bool Check(uint64_t index, uint64_t term) const = 0;
  virtual void RewriteOrAppendAfter(uint64_t index, uint64_t current_term, const std::vector<std::string>& logs) = 0;
  virtual ~StorageEngineBase() {}
};
}

#endif // STORAGE_ENGINE_BASE_H
