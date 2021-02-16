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

RequestVote GetRequestVote(const json& str);

struct RequestVoteReply {
  uint64_t term;
  bool vote_granted;
};

std::string CreateRequestVoteReply(const RequestVoteReply& rvr);

RequestVoteReply GetRequestVoteReply(const json& str);
}

#endif // MESSAGE_H
