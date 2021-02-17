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
};

std::string CreateAppendEntries(const AppendEntries& ae);

AppendEntries GetAppendEntries(const json& j);


struct AppendEntriesReply {
  uint64_t term;
  bool success;
};

std::string CreateAppendEntriesReply(const AppendEntriesReply& aer);

AppendEntriesReply GetAppendEntriesReply(const json& j);
}

#endif // MESSAGE_H
