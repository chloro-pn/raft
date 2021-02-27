#ifndef RAFT_NODE_H
#define RAFT_NODE_H

#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>
#include <vector>
#include <limits>
#include <functional>
#include "asio.hpp"
#include "log.h"
#include "asio_wrapper/timer.h"
#include "asio_wrapper/server.h"
#include "asio_wrapper/client.h"
#include "command_storage.h"
#include "message.h"

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

  void SetCallBackFunc(const std::function<void(const std::string&)>& cb) {
    cb_ = cb;
  }

  void BecomeFollower(uint64_t new_term, int64_t new_leader, int64_t voted_for = -1);

  void StartFollowerTimer();

  void BecomeCandidate();

  void RunForLeader();

  void StartCandidateTimer();

  void BecomeLeader();

  void StartLeaderTimer();

  void AppendEntriesToOthers();

  void SendHeartBeat();

  void OnMessage(std::shared_ptr<puck::TcpConnection> con);

  void DdosToClient(std::shared_ptr<puck::TcpConnection> con);

  void DdosAllClient();

  void OnMessageInit(std::shared_ptr<puck::TcpConnection> con, const json& j);

  void OnMessageFollower(std::shared_ptr<puck::TcpConnection> con, const json& j);

  void OnMessageCandidate(std::shared_ptr<puck::TcpConnection> con, const json& j);

  AppendEntriesReply HandleAppendEntries(const AppendEntries& ae);

  RequestVoteReply HandleRequestVote(const RequestVote& rv);

  void OnMessageLeader(std::shared_ptr<puck::TcpConnection> con, const json& j);

  void UpdateCommitIndexFromMatchIndex();

  void ApplyToStateMachine();

  void NodeLeave(std::shared_ptr<puck::TcpConnection> con);

  void ClientLeave(std::shared_ptr<puck::TcpConnection> con);

private:
  State state_;
  asio::io_context& io_;

  uint64_t my_id_;
  std::unordered_map<uint64_t, std::shared_ptr<puck::TcpConnection>> other_nodes_;

  // valid on leader state.
  std::unordered_map<uint64_t, uint64_t> next_index_;
  std::unordered_map<uint64_t, uint64_t> match_index_;
  // 提交propose的客户端[id, client]
  struct ClientContext {
    uint64_t id;
    std::shared_ptr<puck::TcpConnection> client;

    ClientContext(uint64_t id = 0, std::shared_ptr<puck::TcpConnection> client = nullptr) :
                  id(id),
                  client(client) {

    }
  };

  // [propose's index, client_context]
  std::unordered_map<uint64_t, ClientContext> real_clients_;

  uint64_t current_term_;
  // voted_for_ == -1 means not vote in current term.
  int64_t voted_for_;
  // 当不知道当前leader是谁时，leader_id_ == -1.
  int64_t leader_id_;
  std::unique_ptr<StorageEngineBase> logs_;
  uint64_t commit_index_;
  uint64_t last_applied_;
  std::function<void(const std::string&)> cb_;

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
