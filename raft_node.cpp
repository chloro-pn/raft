#include "raft_node.h"
#include "asio.hpp"
#include "config.h"
#include "asio_wrapper/util.h"
#include "asio_wrapper/timer.h"
#include "message.h"
#include "log.h"
#include "third_party/json.hpp"
#include <sstream>
#include <cstdlib>
#include <algorithm>

static uint16_t get_server_port_from_config() {
  uint64_t mid = puck::Config::instance().MyId();
  const auto& nodes = puck::Config::instance().Nodes();
  assert(nodes.find(mid) != nodes.end());

  const std::string& address = nodes.find(mid)->second;
  int32_t port = puck::util::GetPortFromIport(address);
  if(port == -1) {
    ERROR("error config format.");
  }
  return static_cast<uint16_t>(port);
}

static uint64_t get_mid_from_config() {
  return puck::Config::instance().MyId();
}

static uint64_t get_id_from_iport(const std::string& iport) {
  const auto& nodes = puck::Config::instance().Nodes();
  // need c++14 : auto lambda.
  auto it = std::find_if(nodes.begin(), nodes.end(), [&](const auto& iter) -> bool {
    return iter.second == iport;
  });
  if(it == nodes.end()) {
    ERROR("new connection's iport ", iport, "is not in config.");
  }
  return it->first;
}

static std::string get_iport_from_id(const uint64_t& id) {
  const auto& nodes = puck::Config::instance().Nodes();
  auto it = nodes.find(id);
  if(it == nodes.end()) {
    ERROR("id : ", id, " is not in config");
  }
  return it->second;
}

static bool all_connected(uint64_t count) {
  return puck::Config::instance().Nodes().size() == count + 1;
}

// random timeout from 150 to 200 ms.
static asio::chrono::microseconds get_random_election_timeout() {
  return asio::chrono::microseconds(puck::util::GetRandomFromTo(150, 200));
}

namespace raft {
RaftNode::RaftNode(asio::io_context& io) : state_(RaftNode::State::Init),
                                           io_(io),
                                           my_id_(get_mid_from_config()),
                                           current_term_(0),
                                           voted_for_(-1),
                                           server_(io_, get_server_port_from_config()),
                                           leader_visited_(false),
                                           voted_count_(0) {
  server_.SetOnConnection([this](std::shared_ptr<puck::TcpConnection> con) -> void {
    INFO("new connection : ", con->Iport());
    uint64_t nid = get_id_from_iport(con->Iport());
    // 只有id比自己大的服务器可以主动连接自己。
    if(other_nodes_.find(nid) != other_nodes_.end() || nid <= my_id_) {
      ERROR("error connection : ", con->Iport(), " id = ", nid);
    } else {
      other_nodes_[nid] = con;
      con->SetContext(std::make_shared<uint64_t>(nid));
    }
    // 如果其他所有节点连接了本服务器，开启
    if(all_connected(other_nodes_.size())) {
      BecomeFollower();
    }
  });

  server_.SetOnMessage([this](std::shared_ptr<puck::TcpConnection> con) -> void {
    this->OnMessage(con);
  });

  server_.SetOnClose([this](std::shared_ptr<puck::TcpConnection> con) -> void {
    this->NodeLeave(con);
  });

  // 开启client，连接id比自己小的服务器
  for(uint64_t i = 0; i < my_id_; ++i) {
    std::string iport = get_iport_from_id(i);
    std::shared_ptr<puck::Client> client = std::make_shared<puck::Client>(io_,
                                                                          puck::util::GetIpFromIport(iport),
                                                                          puck::util::GetPortFromIport(iport));
    client->SetRetry(5, 3);
    // set callback
    client->SetOnConnection([this](std::shared_ptr<puck::TcpConnection> con) -> void {
      INFO("new connection : ", con->Iport());
      // 因为是自己发起的连接请求，故不做过多检查
      uint64_t nid = get_id_from_iport(con->Iport());
      assert(other_nodes_.find(nid) == other_nodes_.end());
      other_nodes_[nid] = con;
      con->SetContext(std::make_shared<uint64_t>(nid));
      if(all_connected(other_nodes_.size())) {
        BecomeFollower();
      }
    });

    client->SetOnClose([this](std::shared_ptr<puck::TcpConnection> con) -> void {
      this->NodeLeave(con);
    });

    client->Connect();
    clients_.push_back(client);
  }
}

void RaftNode::BecomeFollower() {
  INFO("node ", my_id_, " become follower from ", GetStateStr());
  state_ = State::Follower;
  StartFollowerTimer();
}

void RaftNode::StartFollowerTimer() {
  assert(state_ == State::Follower);
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  asio::chrono::seconds s = asio::chrono::seconds(5);
  std::shared_ptr<timer_type> timer = std::make_shared<timer_type>([this](const asio::error_code& ec) -> void {
    if(ec) {
      ERROR("asio timer error : ", ec.message());
    }
    assert(state_ == State::Follower);
    if(leader_visited_ == true) {
      leader_visited_ = false;
      StartFollowerTimer();
    } else {
      // become candidate and try to run for leader.
      BecomeCandidate();
    }
  }, io_, s);
  timer_handle_ = timer->Start();
  leader_visited_ = false;
}

void RaftNode::BecomeCandidate() {
  // 只能从follower变成candidate状态
  assert(state_ == State::Follower);
  asio::chrono::microseconds ms = get_random_election_timeout();
  std::shared_ptr<timer_type> timer = std::make_shared<timer_type>([this](const asio::error_code& ec) -> void {
    if(ec) {
      ERROR("asio timer error : ", ec.message());
    }
    INFO("node ", my_id_, " become candidate from ", GetStateStr());
    RunForLeader();
  }, io_, ms);
  timer_handle_ = timer->Start();
}

void RaftNode::RunForLeader() {
  IncreaseTerm();
  state_ = State::Candidate;
  ++current_term_;
  voted_count_ = 0;
  // vote for myself
  voted_for_ = my_id_;
  ++voted_count_;

  RequestVote rv;
  rv.term = current_term_;
  rv.candidate_id = my_id_;
  rv.last_log_index = logs_.GetLastLogIndex();
  rv.last_log_term = logs_.GetLastLogTerm();

  std::string message = CreateRequestVote(rv);
  for(auto& each : other_nodes_) {
    each.second->Send(message);
  }
  // TODO 开启Candidate timeout.
}

void RaftNode::OnMessage(std::shared_ptr<puck::TcpConnection> con) {
  if(state_ == State::Init) {
    ERROR("get message from ", con->Iport(), " in state init.");
  } else if(state_ == State::Follower) {
    OnMessageFollower(con);
  } else if(state_ == State::Candidate) {
    OnMessageCandidate(con);
  } else if(state_ == State::Leader) {
    OnMessageLeader(con);
  } else {
    ERROR("get message from unknow state.");
  }
}

using json = nlohmann::json;

void RaftNode::OnMessageFollower(std::shared_ptr<puck::TcpConnection> con) {
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
  if(j["type"].get<std::string>() == "request_vote") {
    RequestVote rv = GetRequestVote(j);

    RequestVoteReply rvr;
    rvr.term = current_term_;
    rvr.vote_granted = false;

    int state = 0;
    if(current_term_ == rv.term) {
      state = 0;
    } else if(current_term_ > rv.term) {
      state = -1;
    } else {
      state = 1;
    }

    if(state == 1) {
      if(rv.last_log_term >= logs_.GetLastLogTerm() ||
         (rv.last_log_term == logs_.GetLastLogTerm() && rv.last_log_index == logs_.GetLastLogIndex())) {
        rvr.vote_granted = true;
        voted_for_ = rv.candidate_id;
      } else {
        voted_for_ = -1;
      }
    } else if(state == 0 && voted_for_ == -1) {
      if(rv.last_log_term >= logs_.GetLastLogTerm() ||
         (rv.last_log_term == logs_.GetLastLogTerm() && rv.last_log_index == logs_.GetLastLogIndex())) {
        rvr.vote_granted = true;
        voted_for_ = rv.candidate_id;
      }
    } else {
      // 要么是过时消息， 要么是本term已经投过票
    }

    std::string reply_msg = CreateRequestVoteReply(rvr);
    con->Send(reply_msg);
    if(state <= 0) {
      return;
    } else {
      // 进入新的term
      current_term_ = rvr.term;
      //关闭之前的timer，开启新的follower timeout timer.
      if(!timer_handle_) {
        ERROR("follower state but have no timeout.");
      }
      timer_handle_->Cancel();
      StartFollowerTimer();
    }
  } else if(j["type"] == "append_entries") {
    // new leader send heartbeat. restart follower timeout.

  } else {
    // 其他消息直接丢掉，因为有可能本节点刚从leader/candidate变成follower
  }
}

void RaftNode::OnMessageCandidate(std::shared_ptr<puck::TcpConnection> con) {
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
  if(j["type"].get<std::string>() == "request_vote_reply") {
    RequestVoteReply rvr = GetRequestVoteReply(message);
    if(rvr.term > current_term_) {
      assert(rvr.vote_granted == false);
      current_term_ = rvr.term;
      voted_for_ = -1;
      BecomeFollower();
    } else {
      // 是否应该只接受本term的投票
      if(rvr.vote_granted == true) {
        ++voted_count_;
        // TODO : 如果收到过半通过票,BecomeLeader.
        if(voted_count_ * 2 > puck::Config::instance().Nodes().size()) {

        }
      }
    }
  } else if(j["type"].get<std::string>() == "request_vote") {
    RequestVote rv = GetRequestVote(j);

    RequestVoteReply rvr;
    rvr.term = current_term_;
    rvr.vote_granted = false;

    int state = 0;
    if(current_term_ == rv.term) {
      state = 0;
    } else if(current_term_ > rv.term) {
      state = -1;
    } else {
      state = 1;
    }

    if(state == 1) {
      if(rv.last_log_term >= logs_.GetLastLogTerm() ||
         (rv.last_log_term == logs_.GetLastLogTerm() && rv.last_log_index == logs_.GetLastLogIndex())) {
        rvr.vote_granted = true;
        voted_for_ = rv.candidate_id;
      } else {
        voted_for_ = -1;
      }
      // candidate状态下这个分支是不可能成立的，voted_for_ === my_id_.
    } else if(state == 0 && voted_for_ == -1) {
      if(rv.last_log_term >= logs_.GetLastLogTerm() ||
         (rv.last_log_term == logs_.GetLastLogTerm() && rv.last_log_index == logs_.GetLastLogIndex())) {
        rvr.vote_granted = true;
        voted_for_ = rv.candidate_id;
      }
    } else {
      // 要么是过时消息， 要么是本term已经投过票
    }

    std::string reply_msg = CreateRequestVoteReply(rvr);
    con->Send(reply_msg);
    if(state <= 0) {
      return;
    } else {
      // 进入新的term
      current_term_ = rvr.term;
      //关闭之前的timer，开启新的follower timeout timer.
      if(!timer_handle_) {
        ERROR("candidate state but have no timeout.");
      }
      timer_handle_->Cancel();
      StartFollowerTimer();
    }
  } else {

  }
}

void RaftNode::OnMessageLeader(std::shared_ptr<puck::TcpConnection> con) {
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
}

void RaftNode::NodeLeave(std::shared_ptr<puck::TcpConnection> con) {
  INFO("close connection : ", con->Iport());
  uint64_t id = con->getContext<uint64_t>();
  other_nodes_.erase(id);
  if(con->GetState() == puck::TcpConnection::ConnState::ReadZero) {
    INFO("tcp connection state : read_zero");
  } else {
    WARN("tcp connection state : ", con->GetStateStr());
  }
}
}
