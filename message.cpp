#include "message.h"
#include <cassert>
#include <string>

namespace raft {
std::string CreateRequestVote(const RequestVote& rv) {
  json j;
  j["type"] = "request_vote";
  j["term"] = rv.term;
  j["id"] = rv.candidate_id;
  j["last_log_index"] = rv.last_log_index;
  j["last_log_term"] = rv.last_log_term;
  return j.dump();
}

RequestVote GetRequestVote(const json& j) {
  assert(j["type"].get<std::string>() == "request_vote");
  RequestVote rv;
  rv.term = j["term"].get<uint64_t>();
  rv.candidate_id = j["id"].get<uint64_t>();
  rv.last_log_index = j["last_log_index"].get<uint64_t>();
  rv.last_log_term = j["last_log_term"].get<uint64_t>();
  return rv;
}

std::string CreateRequestVoteReply(const RequestVoteReply& rvr) {
  json j;
  j["type"] = "request_vote_reply";
  j["term"] = rvr.term;
  j["voted_granted"] = rvr.vote_granted;
  return j.dump();
}

RequestVoteReply GetRequestVoteReply(const json& j) {
  assert(j["type"].get<std::string>() == "request_vote_reply");
  RequestVoteReply rvr;
  rvr.term = j["term"].get<uint64_t>();
  rvr.vote_granted = j["voted_granted"].get<bool>();
  return rvr;
}

std::string CreateAppendEntries(const AppendEntries& ae) {
  json j;
  j["type"] = "append_entries";
  j["term"] = ae.term;
  j["id"] = ae.leader_id;
  j["prev_log_index"] = ae.prev_log_index;
  j["prev_log_term"] = ae.prev_log_term;
  j["leader_commit"] = ae.leader_commit;
  for(const auto& each : ae.entries) {
    j["entries"].push_back(each);
  }
  return j.dump();
}

AppendEntries GetAppendEntries(const json& j) {
  AppendEntries ae;
  ae.leader_id = j["id"].get<uint64_t>();
  ae.term = j["term"].get<uint64_t>();
  ae.prev_log_index = j["prev_log_index"].get<uint64_t>();
  ae.prev_log_term = j["prev_log_term"].get<uint64_t>();
  if(j.contains("entries")) {
    for(auto it = j["entries"].cbegin(); it != j["entries"].cend(); ++it) {
      ae.entries.push_back((*it).get<std::string>());
    }
  }
  ae.leader_commit = j["leader_commit"].get<uint64_t>();
  return ae;
}

std::string CreateAppendEntriesReply(const AppendEntriesReply& aer) {
  json j;
  j["type"] = "append_entries_reply";
  j["term"] = aer.term;
  j["success"] = aer.success;
  j["next_index"] = aer.next_index;
  return j.dump();
}

AppendEntriesReply GetAppendEntriesReply(const json& j) {
  AppendEntriesReply aer;
  aer.term = j["term"].get<uint64_t>();
  aer.success = j["success"].get<bool>();
  aer.next_index = j["next_index"].get<uint64_t>();
  return aer;
}
}
