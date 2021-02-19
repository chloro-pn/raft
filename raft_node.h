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
#include "asio_wrapper/timer.h"
#include "asio_wrapper/server.h"
#include "asio_wrapper/client.h"
#include "command_storage.h"

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

  void BecomeFollower(uint64_t new_term_);

  void StartFollowerTimer();

  void BecomeCandidate();

  void RunForLeader();

  void StartCandidateTimer();

  void BecomeLeader();

  void StartLeaderTimer();

  void OnMessage(std::shared_ptr<puck::TcpConnection> con);

  void OnMessageInit(std::shared_ptr<puck::TcpConnection> con);

  void OnMessageFollower(std::shared_ptr<puck::TcpConnection> con);

  void OnMessageCandidate(std::shared_ptr<puck::TcpConnection> con);

  void OnMessageLeader(std::shared_ptr<puck::TcpConnection> con);

  void NodeLeave(std::shared_ptr<puck::TcpConnection> con);

private:
  State state_;
  asio::io_context& io_;

  uint64_t my_id_;
  std::unordered_map<uint64_t, std::shared_ptr<puck::TcpConnection>> other_nodes_;

  // valid on leader state.
  std::unordered_map<uint64_t, uint64_t> next_index_;
  std::unordered_map<uint64_t, uint64_t> match_index_;

  uint64_t current_term_;
  // voted_for_ == -1 means not vote in current term.
  int64_t voted_for_;
  CommandStorage logs_;
  uint64_t commit_index_;
  uint64_t last_applied_;

  puck::Server server_;
  std::vector<std::shared_ptr<puck::Client>> clients_;

  // used in candidate state.
  uint64_t voted_count_;
  using timer_type = puck::Timer<asio::chrono::microseconds>;
  // 任何时候，Raft节点只有一个timer处于活动状态
  std::shared_ptr<puck::TimerHandle> timer_handle_;
};
}


#endif // RAFT_NODE_H
