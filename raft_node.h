#ifndef RAFT_NODE_H
#define RAFT_NODE_H

#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>
#include <vector>
#include <limits>
#include "asio.hpp"
#include "log.h"
#include "asio_wrapper/server.h"
#include "asio_wrapper/client.h"

namespace raft {
class RaftNodeContext {

};

class RaftNode {
public:
  enum class State {
    Init,
    Leader,
    Follower,
    Candidate,
  };

  const char* GetStateStr() const {
    if(state_ == State::Init) {
      return "init";
    } else if(state_ == State::Leader) {
      return "leader";
    } else if(state_ == State::Follower) {
      return "follower";
    } else {
      assert(state_ == State::Candidate);
      return "candidate";
    }
  }

  explicit RaftNode(asio::io_context& io);

  void BecomeFollower();

  void StartFollowerTimer();

  void BecomeCandidate();

  void IncreaseTerm() {
    assert(std::numeric_limits<uint64_t>::max() > current_term_);
    INFO("term from ", current_term_, " to ", current_term_ + 1);
    ++current_term_;
  }

  void RunForLeader();

  void StartCandidateTimer();

private:
  State state_;
  asio::io_context& io_;

  size_t my_id_;
  std::unordered_map<size_t, std::shared_ptr<puck::TcpConnection>> other_nodes_;

  uint64_t current_term_;
  // voted_for_ == -1 means not vote in current term.
  int64_t voted_for_;
  puck::Server server_;
  std::vector<std::shared_ptr<puck::Client>> clients_;

  bool leader_visited_;
};
}


#endif // RAFT_NODE_H
