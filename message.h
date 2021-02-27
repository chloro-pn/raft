#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include <cstdint>
#include "third_party/json.hpp"

namespace raft {
using json = nlohmann::json;

struct RequestVote {
  uint64_t term;
  uint64_t candidate_id;
  uint64_t last_log_index;
  uint64_t last_log_term;
};

std::string CreateRequestVote(const RequestVote& rv);

RequestVote GetRequestVote(const json& j);

struct RequestVoteReply {
  uint64_t term;
  bool vote_granted;
};

std::string CreateRequestVoteReply(const RequestVoteReply& rvr);

RequestVoteReply GetRequestVoteReply(const json& j);

struct AppendEntries {
  uint64_t term;
  uint64_t leader_id;
  uint64_t prev_log_index;
  uint64_t prev_log_term;
  std::vector<std::string> entries;
  uint64_t leader_commit;
  bool heart_beat;
};

std::string CreateAppendEntries(const AppendEntries& ae);

AppendEntries GetAppendEntries(const json& j);


struct AppendEntriesReply {
  uint64_t term;
  bool success;
  // 当success == true， 下面这个属性代表了本节点和follower节点一致的下一个节点索引。
  uint64_t next_index;
  bool heart_beat;
};

std::string CreateAppendEntriesReply(const AppendEntriesReply& aer);

AppendEntriesReply GetAppendEntriesReply(const json& j);

struct Propose {
  uint64_t id;
  std::string command;
};

std::string CreatePropose(const Propose& p);

Propose GetPropose(const json& j);

struct ProposeReply {
  uint64_t id;

  explicit ProposeReply(uint64_t id) : id(id) {

  }
};

std::string CreateProposeReply(const ProposeReply& p);

ProposeReply GetProposeReply(const json& j);

struct Ddos {
  // 如果不知道当前leader，leader_id_ == -1.
  int64_t leader_id;
  std::string ip;
  uint16_t port;
};

std::string CreateDdos(const Ddos& p);

Ddos GetDdos(const json& j);
}

#endif // MESSAGE_H
