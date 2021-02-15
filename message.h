#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include <cstdint>

namespace raft {
struct RequestVote {
  uint64_t term;
  uint64_t candidate_id;
  uint64_t last_log_index;
  uint64_t last_log_term;
};

std::string CreateRequestVote(const RequestVote& rv);

RequestVote GetRequestVote(const std::string& str);

struct RequestVoteReply {
  uint64_t term;
  bool vote_granted;
};

std::string CreateRequestVoteReply(const RequestVoteReply& rvr);

RequestVoteReply GetRVReply(const std::string& str);
}

#endif // MESSAGE_H
