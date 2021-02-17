#include "raft_node.h"
#include "asio.hpp"
#include "config.h"
#include "asio_wrapper/util.h"
#include "asio_wrapper/timer.h"
#include "message.h"
#include "log.h"
#include "third_party/json.hpp"
#include <sstream>
#include <string>
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

static asio::chrono::microseconds get_candidate_timeout() {
  return asio::chrono::microseconds(800);
}

static asio::chrono::seconds get_heartbeat_timeout() {
  return asio::chrono::seconds(3);
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
    con->SetContext(std::make_shared<int64_t>(-1));
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
    client->SetOnConnection([=, this](std::shared_ptr<puck::TcpConnection> con) -> void {
      INFO("new connection : ", con->Iport());
      con->SetContext(std::make_shared<uint64_t>(i));
      json j;
      j["type"] = "init";
      j["id"] = my_id_;
      con->Send(j.dump());
      if(other_nodes_.find(i) != other_nodes_.end()) {
        ERROR("duplicate id : ", i);
      }
      other_nodes_[i] = con;
      assert(state_ == State::Init);
      if(all_connected(other_nodes_.size())) {
        INFO("node ", my_id_, " start.");
        BecomeFollower(0);
      }
    });

    client->SetOnMessage([this](std::shared_ptr<puck::TcpConnection> con) -> void {
      if(state_ == State::Init) {
        ERROR("client node should not get any message on state init.");
      }
      this->OnMessage(con);
    });

    client->SetOnClose([this](std::shared_ptr<puck::TcpConnection> con) -> void {
      this->NodeLeave(con);
    });

    client->Connect();
    clients_.push_back(client);
  }
}

void RaftNode::BecomeFollower(uint64_t new_term_) {
  INFO("node ", my_id_, " become follower from ", GetStateStr());
  state_ = State::Follower;
  current_term_ = new_term_;
  leader_visited_ = false;
  voted_for_ = -1;
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
}

void RaftNode::BecomeCandidate() {
  // 只能从 follower 或者 candidate 变成 candidate 状态
  assert(state_ == State::Follower || state_ == State::Candidate);
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  asio::chrono::microseconds ms = get_random_election_timeout();
  std::shared_ptr<timer_type> timer = std::make_shared<timer_type>([this](const asio::error_code& ec) -> void {
    if(ec) {
      ERROR("asio timer error : ", ec.message());
    }
    INFO("node ", my_id_, " become candidate from ", GetStateStr());
    state_ = State::Candidate;
    ++current_term_;
    voted_for_ = -1;
    voted_count_ = 0;
    RunForLeader();
  }, io_, ms);
  timer_handle_ = timer->Start();
}

void RaftNode::RunForLeader() {
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
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  asio::chrono::microseconds ms = get_candidate_timeout();
  std::shared_ptr<timer_type> timer = std::make_shared<timer_type>([this](const asio::error_code& ec) -> void {
    if(ec) {
      ERROR("asio timer error : ", ec.message());
    }
    // 如果变成leader或者follower，则本timer根本不会执行，所以这里只写超时逻辑。
    assert(state_ == State::Candidate);
    INFO("candidate out of date, try again.");
    BecomeCandidate();
  }, io_, ms);
  timer_handle_ = timer->Start();
}

void RaftNode::BecomeLeader() {
  assert(state_ == State::Candidate);
  state_ = State::Leader;
  INFO("node ", my_id_, " become leader.");
  // TODO ...
  StartLeaderTimer();
}

void RaftNode::StartLeaderTimer() {
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  AppendEntries ae;
  ae.term = current_term_;
  ae.leader_id = my_id_;
  std::string message = CreateAppendEntries(ae);
  for(auto& each : other_nodes_) {
    each.second->Send(message);
  }
  INFO("leader send heartbeat to all followers.");
  auto timer = std::make_shared<timer_type>([this](asio::error_code ec) -> void {
    assert(state_ == State::Leader);
    StartLeaderTimer();
  }, io_, get_heartbeat_timeout());
  timer_handle_ = timer->Start();
}

void RaftNode::OnMessage(std::shared_ptr<puck::TcpConnection> con) {
  if(state_ == State::Init) {
    OnMessageInit(con);
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

// only server node.
void RaftNode::OnMessageInit(std::shared_ptr<puck::TcpConnection> con) {
  int64_t id = con->getContext<int64_t>();
  assert(id == -1);
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
  if(j["type"].get<std::string>() != "init") {
    ERROR("node ", my_id_, " get error type message :", j["type"].get<std::string>(), "on state init.");
  }
  id = j["id"].get<uint64_t>();
  if(other_nodes_.find(id) != other_nodes_.end() || my_id_ == id) {
    ERROR("duplicate id : ", id);
  }
  other_nodes_[id] = con;
  con->SetContext(std::make_shared<uint64_t>(id));
  INFO("node ", id, " connected.");
  assert(state_ == State::Init);
  if(all_connected(other_nodes_.size())) {
    INFO("node ", my_id_, " start.");
    BecomeFollower(0);
  }
}

void RaftNode::OnMessageFollower(std::shared_ptr<puck::TcpConnection> con) {
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
  if(j["type"].get<std::string>() == "request_vote") {
    RequestVote rv = GetRequestVote(j);

    RequestVoteReply rvr;
    rvr.term = current_term_;
    rvr.vote_granted = false;

    if(rv.term > current_term_) {
      BecomeFollower(rv.term);
      rvr.term = current_term_;
      if(rv.last_log_term > logs_.GetLastLogTerm() ||
         (rv.last_log_term == logs_.GetLastLogTerm() && rv.last_log_index >= logs_.GetLastLogIndex())) {
        rvr.vote_granted = true;
        voted_for_ = rv.candidate_id;
      } else {
        assert(voted_for_ == -1);
      }
    } else if(rv.term == current_term_ && voted_for_ == -1) {
      if(rv.last_log_term > logs_.GetLastLogTerm() ||
         (rv.last_log_term == logs_.GetLastLogTerm() && rv.last_log_index >= logs_.GetLastLogIndex())) {
        rvr.vote_granted = true;
        voted_for_ = rv.candidate_id;
      } else {
        assert(voted_for_ == -1);
      }
    } else {
      // 要么是过时消息，要么是本term已经投过票了
    }

    std::string reply_msg = CreateRequestVoteReply(rvr);
    con->Send(reply_msg);
  } else if(j["type"].get<std::string>() == "append_entries") {
    // new leader send heartbeat. restart follower timeout.
    AppendEntries ae = GetAppendEntries(j);
    if(ae.term >= current_term_) {
      BecomeFollower(ae.term);
    } else {
      // 过期，目前不考虑回复
    }
  } else {
    // 其他消息直接丢掉，因为有可能本节点刚从leader/candidate变成follower
  }
}

void RaftNode::OnMessageCandidate(std::shared_ptr<puck::TcpConnection> con) {
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
  if(j["type"].get<std::string>() == "request_vote_reply") {
    RequestVoteReply rvr = GetRequestVoteReply(j);
    if(rvr.term > current_term_) {
      BecomeFollower(rvr.term);
    } else {
      // 只接受本term的投票
      if(rvr.term == current_term_ && rvr.vote_granted == true) {
        ++voted_count_;
        // TODO : 如果收到过半通过票,BecomeLeader.
        if(voted_count_ * 2 > puck::Config::instance().Nodes().size()) {
          BecomeLeader();
        }
      }
    }
  } else if(j["type"].get<std::string>() == "request_vote") {
    RequestVote rv = GetRequestVote(j);
    RequestVoteReply rvr;
    rvr.term = current_term_;
    rvr.vote_granted = false;

    if(rv.term > current_term_) {
      BecomeFollower(rv.term);
      rvr.term = current_term_;
      if(rv.last_log_term > logs_.GetLastLogTerm() ||
         (rv.last_log_term == logs_.GetLastLogTerm() && rv.last_log_index >= logs_.GetLastLogIndex())) {
        rvr.vote_granted = true;
        voted_for_ = rv.candidate_id;
      } else {
        assert(voted_for_ == -1);
      }
    } else {
      // 由于是candidate状态，因此voted_for_ == my_id_, 已经把票投给了自己。
      assert(voted_for_ == my_id_);
    }

    std::string reply_msg = CreateRequestVoteReply(rvr);
    con->Send(reply_msg);
  } else if(j["type"].get<std::string>() == "append_entries") {
    AppendEntries ae = GetAppendEntries(j);
    AppendEntriesReply aer;
    aer.term = current_term_;
    aer.success = true;
    if(ae.term > current_term_) {
      BecomeFollower(ae.term);
      aer.term = current_term_;
      // determine aer.success.
    } else if(ae.term == current_term_) {
      // determine aer.success.
    } else {
      aer.success = false;
    }
    std::string message = CreateAppendEntriesReply(aer);
    con->Send(message);
  }
}

void RaftNode::OnMessageLeader(std::shared_ptr<puck::TcpConnection> con) {
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
  if(j["type"] == "append_entries_reply") {
    AppendEntriesReply aer = GetAppendEntriesReply(j);
    if(aer.term > current_term_) {
      BecomeFollower(aer.term);
    } else {
      //本版本只将该信息当作心跳
    }
  }
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
