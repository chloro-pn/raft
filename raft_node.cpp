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
#include <limits>
#include <memory>

static uint16_t get_server_port_from_config() {
  uint64_t mid = raft::Config::instance().MyId();
  const auto& nodes = raft::Config::instance().Nodes();
  assert(nodes.find(mid) != nodes.end());

  const std::string& address = nodes.find(mid)->second;
  int32_t port = puck::util::GetPortFromIport(address);
  if(port == -1) {
    ERROR("error config format.");
  }
  return static_cast<uint16_t>(port);
}

static uint64_t get_mid_from_config() {
  return raft::Config::instance().MyId();
}

static std::string get_iport_from_id(const uint64_t& id) {
  const auto& nodes = raft::Config::instance().Nodes();
  auto it = nodes.find(id);
  if(it == nodes.end()) {
    ERROR("id : ", id, " is not in config");
  }
  return it->second;
}

static bool all_connected(uint64_t count) {
  return raft::Config::instance().Nodes().size() == count + 1;
}

// random timeout from 150 to 300 ms.
static asio::chrono::milliseconds get_random_election_timeout() {
  return asio::chrono::milliseconds(puck::util::GetRandomFromTo(150, 300));
}

static asio::chrono::milliseconds get_candidate_timeout() {
  return asio::chrono::milliseconds(400);
}

static asio::chrono::seconds get_heartbeat_timeout() {
  return asio::chrono::seconds(2);
}

static uint64_t CLIENT_CONTEXT = std::numeric_limits<uint64_t>::max();

namespace raft {
RaftNode::RaftNode(asio::io_context& io) : state_(RaftNode::State::Init),
                                           io_(io),
                                           my_id_(get_mid_from_config()),
                                           current_term_(0),
                                           voted_for_(-1),
                                           leader_id_(-1),
                                           // need c++14
                                           logs_(std::make_unique<CommandStorage>()),
                                           commit_index_(0),
                                           last_applied_(0),
                                           server_(io_, get_server_port_from_config()),
                                           voted_count_(0) {
  server_.SetOnConnection([this](std::shared_ptr<puck::TcpConnection> con) -> void {
    INFO("new connection : ", con->Iport());
    con->SetContext(std::make_shared<int64_t>(-1));
  });

  server_.SetOnMessage([this](std::shared_ptr<puck::TcpConnection> con) -> void {
    this->OnMessage(con);
  });

  server_.SetOnClose([this](std::shared_ptr<puck::TcpConnection> con) -> void {
    uint64_t id = con->getContext<uint64_t>();
    if(id == CLIENT_CONTEXT) {
      this->ClientLeave(con);
    } else {
      this->NodeLeave(con);
    }
  });

  // 开启client，连接id比自己小的服务器
  for(uint64_t i = 0; i < my_id_; ++i) {
    std::string iport = get_iport_from_id(i);
    std::shared_ptr<puck::Client> client = std::make_shared<puck::Client>(io_,
                                                                          puck::util::GetIpFromIport(iport),
                                                                          puck::util::GetPortFromIport(iport));
    client->SetRetry(5, 3);
    // set callback
    client->SetOnConnection([i, this](std::shared_ptr<puck::TcpConnection> con) -> void {
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
        logs_->Init();
        BecomeFollower(logs_->GetCurrentTerm(), -1, logs_->GetVotedFor());
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

// BUG FIX
void RaftNode::BecomeFollower(uint64_t new_term, int64_t new_leader, int64_t voted_for) {
  INFO("node ", my_id_, " become follower from ", GetStateStr());
  if(state_ == State::Leader) {
    // 从leader变成follower，及时向客户端回复ddos
    DdosAllClient();
  }
  state_ = State::Follower;
  if(new_term > current_term_) {
    current_term_ = new_term;
    voted_for_ = voted_for;
    leader_id_ = new_leader;
  }
  StartFollowerTimer();
}

void RaftNode::StartFollowerTimer() {
  assert(state_ == State::Follower);
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  asio::chrono::seconds s = asio::chrono::seconds(2);
  std::shared_ptr<timer_type> timer = std::make_shared<timer_type>([this](const asio::error_code& ec) -> void {
    if(ec) {
      ERROR("asio timer error : ", ec.message());
    }
    assert(state_ == State::Follower);
    // become candidate and try to run for leader.
    BecomeCandidate();
  }, io_, s);
  timer_handle_ = timer->Start();
}

void RaftNode::BecomeCandidate() {
  // 只能从 follower 或者 candidate 变成 candidate 状态
  assert(state_ == State::Follower || state_ == State::Candidate);
  // 立刻将知道的leader_id_修改为-1，防止超时时间到来前有客户端连接。
  leader_id_ = -1;
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  asio::chrono::milliseconds ms = get_random_election_timeout();
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
  logs_->Storage(current_term_, my_id_);
  voted_for_ = my_id_;
  ++voted_count_;

  RequestVote rv;
  rv.term = current_term_;
  rv.candidate_id = my_id_;
  rv.last_log_index = logs_->GetLastLogIndex();
  rv.last_log_term = logs_->GetLastLogTerm();

  std::string message = CreateRequestVote(rv);
  for(auto& each : other_nodes_) {
    each.second->Send(message);
  }
  // TODO 开启Candidate timeout.
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  asio::chrono::milliseconds ms = get_candidate_timeout();
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
  leader_id_ = my_id_;
  INFO("node ", my_id_, " become leader, term : ", current_term_);

  // init next_index_ and match_index_
  for(auto& each : other_nodes_) {
    next_index_[each.first] = logs_->GetLastLogIndex() + 1;
    match_index_[each.first] = 0;
  }
  SendHeartBeat();
  StartLeaderTimer();
}

void RaftNode::StartLeaderTimer() {
  assert(state_ == State::Leader);
  if(timer_handle_) {
    timer_handle_->Cancel();
  }
  AppendEntriesToOthers();
  auto timer = std::make_shared<timer_type>([this](asio::error_code ec) -> void {
    if(ec) {
      ERROR("asio timer error : ", ec.message());
  }
    StartLeaderTimer();
  }, io_, get_heartbeat_timeout());
  timer_handle_ = timer->Start();
}

void RaftNode::AppendEntriesToOthers() {
  assert(static_cast<int64_t>(my_id_) == leader_id_);
  AppendEntries ae;
  ae.term = current_term_;
  ae.leader_id = my_id_;
  ae.leader_commit = commit_index_;
  ae.heart_beat = false;
  for(auto& each : other_nodes_) {
    // next_index_ always > 0.
    uint64_t prev_log_index = next_index_[each.first] - 1;
    ae.prev_log_index = prev_log_index;
    ae.prev_log_term = logs_->GetTermFromIndex(prev_log_index);
    ae.entries = logs_->GetLogsAfterPrevLog(ae.prev_log_index);

    std::string message = CreateAppendEntries(ae);
    each.second->Send(message);
  }
  INFO("leader appends entries to all followers.");
}

void RaftNode::SendHeartBeat() {
  assert(static_cast<int64_t>(my_id_) == leader_id_);
  AppendEntries ae;
  ae.term = current_term_;
  ae.leader_id = my_id_;
  ae.leader_commit = commit_index_;
  ae.heart_beat = true;
  for(auto& each : other_nodes_) {
    // next_index_ always > 0.
    uint64_t prev_log_index = next_index_[each.first] - 1;
    ae.prev_log_index = prev_log_index;
    ae.prev_log_term = logs_->GetTermFromIndex(prev_log_index);
    ae.entries.clear();

    std::string message = CreateAppendEntries(ae);
    each.second->Send(message);
  }
  INFO("leader sends heartbeat to other.");
}

using json = nlohmann::json;

void RaftNode::DdosToClient(std::shared_ptr<puck::TcpConnection> con) {
  Ddos d;
  d.leader_id = leader_id_;
  if(leader_id_ != -1) {
    std::string iport = get_iport_from_id(leader_id_);
    d.ip = puck::util::GetIpFromIport(iport);
    d.port = puck::util::GetPortFromIport(iport);
  } else {
    d.ip = "";
    d.port = 0;
  }
  con->Send(CreateDdos(d));
  con->ShutdownW();
}

void RaftNode::DdosAllClient() {
  Ddos d;
  d.leader_id = -1;
  d.ip = "";
  d.port = 0;
  std::string message = CreateDdos(d);
  for(auto& each : real_clients_) {
    each.second.client->Send(message);
  }
  real_clients_.clear();
}

void RaftNode::OnMessage(std::shared_ptr<puck::TcpConnection> con) {
  std::string message(con->MessageData(), con->MessageLength());
  json j = json::parse(message);
  if(j["type"].get<std::string>() == "propose") {
    if(state_ == State::Leader) {
      OnMessageLeader(con, j);
    } else {
      DdosToClient(con);
    }
    return;
  }
  if(state_ == State::Init) {
    OnMessageInit(con, j);
  } else if(state_ == State::Follower) {
    OnMessageFollower(con, j);
  } else if(state_ == State::Candidate) {
    OnMessageCandidate(con, j);
  } else if(state_ == State::Leader) {
    OnMessageLeader(con, j);
  } else {
    ERROR("get message from unknow state.");
  }
}

// only server node.
void RaftNode::OnMessageInit(std::shared_ptr<puck::TcpConnection> con, const json& j) {
  int64_t id = con->getContext<int64_t>();
  assert(id == -1);
  if(j["type"].get<std::string>() != "init") {
    ERROR("node ", my_id_, " get error type message :", j["type"].get<std::string>(), "on state init.");
  }
  id = j["id"].get<uint64_t>();
  if(other_nodes_.find(id) != other_nodes_.end() || static_cast<int64_t>(my_id_) == id) {
    ERROR("duplicate id : ", id);
  }
  other_nodes_[id] = con;
  con->SetContext(std::make_shared<uint64_t>(id));
  INFO("node ", id, " connected.");
  assert(state_ == State::Init);
  if(all_connected(other_nodes_.size())) {
    INFO("node ", my_id_, " start.");
    logs_->Init();
    BecomeFollower(logs_->GetCurrentTerm(), -1, logs_->GetVotedFor());
    BecomeFollower(0, -1);
  }
}

void RaftNode::OnMessageFollower(std::shared_ptr<puck::TcpConnection> con, const json& j) {
  if(j["type"].get<std::string>() == "request_vote") {
    RequestVote rv = GetRequestVote(j);
    RequestVoteReply rvr = HandleRequestVote(rv);
    std::string reply_msg = CreateRequestVoteReply(rvr);
    con->Send(reply_msg);
  } else if(j["type"].get<std::string>() == "append_entries") {
    AppendEntries ae = GetAppendEntries(j);
    auto aer = HandleAppendEntries(ae);
    std::string reply_msg = CreateAppendEntriesReply(aer);
    con->Send(reply_msg);
  } else {
    // 有可能本节点刚从leader/candidate变成follower，只需要判断term即可
    std::string type = j["type"].get<std::string>();
    if(type != "request_vote_reply" && type != "append_entries_reply") {
      ERROR("error message type : ", type, " on state follower.");
    }
    uint64_t term = j["term"].get<uint64_t>();
    if(term > current_term_) {
      BecomeFollower(term, -1);
    }
  }
}

void RaftNode::OnMessageCandidate(std::shared_ptr<puck::TcpConnection> con, const json& j) {
  if(j["type"].get<std::string>() == "request_vote_reply") {
    RequestVoteReply rvr = GetRequestVoteReply(j);
    if(rvr.term > current_term_) {
      BecomeFollower(rvr.term, -1);
    } else {
      // 只接受本term的投票
      if(rvr.term == current_term_ && rvr.vote_granted == true) {
        ++voted_count_;
        // TODO : 如果收到过半通过票,BecomeLeader.
        if(voted_count_ * 2 > raft::Config::instance().Nodes().size()) {
          BecomeLeader();
        }
      }
    }
  } else if(j["type"].get<std::string>() == "request_vote") {
    RequestVote rv = GetRequestVote(j);
    RequestVoteReply rvr = HandleRequestVote(rv);
    std::string reply_msg = CreateRequestVoteReply(rvr);
    con->Send(reply_msg);
  } else if(j["type"].get<std::string>() == "append_entries") {
    AppendEntries ae = GetAppendEntries(j);
    AppendEntriesReply aer = HandleAppendEntries(ae);
    std::string message = CreateAppendEntriesReply(aer);
    con->Send(message);
  } else {
    std::string type = j["type"].get<std::string>();
    if(type != "append_entries_reply") {
      ERROR("error message type : ", type, " on state candidate.");
    }
    uint64_t term = j["term"].get<uint64_t>();
    if(term > current_term_) {
      BecomeFollower(term, -1);
    }
  }
}

AppendEntriesReply RaftNode::HandleAppendEntries(const AppendEntries& ae) {
  AppendEntriesReply aer;
  aer.success = true;
  aer.term = current_term_;
  aer.heart_beat = false;
  if(ae.term >= current_term_) {
    BecomeFollower(ae.term, ae.leader_id);
    aer.term = current_term_;
    // 如果是心跳包，只考虑term就行了
    if(ae.heart_beat == true) {
      aer.heart_beat = true;
      aer.next_index = 0;
      return aer;
    }
    if(logs_->Check(ae.prev_log_index, ae.prev_log_term) == true) {
      logs_->RewriteOrAppendAfter(ae.prev_log_index, current_term_, ae.entries);
      aer.next_index = ae.prev_log_index + ae.entries.size() + 1;
      if(ae.leader_commit > commit_index_) {
        commit_index_ = std::min(static_cast<uint64_t>(logs_->Size() - 1), ae.leader_commit);
      }
      ApplyToStateMachine();
    } else {
      // 日志不匹配
      aer.success = false;
      aer.next_index = 0;
    }
  } else {
    aer.success = false;
    aer.next_index = 0;
  }
  return aer;
}

RequestVoteReply RaftNode::HandleRequestVote(const RequestVote& rv) {
  RequestVoteReply rvr;
  rvr.term = current_term_;
  rvr.vote_granted = false;

  if(rv.term > current_term_) {
    BecomeFollower(rv.term, -1);
    rvr.term = current_term_;
  }

  if(rv.term == current_term_ && voted_for_ == -1) {
    if(logs_->MoreOrEqualUpToDate(rv.last_log_index, rv.last_log_term)) {
      // 在投票前持久化投票信息，这样可能导致重启后少一次投票
      // 但避免了同一个term内投多次票
      logs_->Storage(current_term_, rv.candidate_id);
      rvr.vote_granted = true;
      voted_for_ = rv.candidate_id;
      INFO("node ", my_id_, " vote for node ", rv.candidate_id, " in term ", rv.term);
    }
  }
  return rvr;
}

void RaftNode::OnMessageLeader(std::shared_ptr<puck::TcpConnection> con, const json& j) {
  if(j["type"].get<std::string>() == "propose") {
    Propose p = GetPropose(j);
    Item tmp {current_term_, p.command};
    logs_->Storage(tmp);
    real_clients_[logs_->GetLastLogIndex()] = ClientContext{p.id, con};
    con->SetContext(std::make_shared<uint64_t>(CLIENT_CONTEXT));
  } else if(j["type"].get<std::string>() == "append_entries_reply") {
    AppendEntriesReply aer = GetAppendEntriesReply(j);
    // current version.
    if(aer.term > current_term_) {
      BecomeFollower(aer.term, -1);
    } else {
      if(aer.heart_beat == true) {
        return;
      }
      if(aer.success == false) {
        // TODO : 当前连接对应的id对应的next_index_ 减1.
        uint64_t id = con->getContext<uint64_t>();
        next_index_[id] -= 1;
        // 立即或者等待下一次心跳发送。
        // 当前版本先将全部同步任务放在心跳，后续要考虑普通心跳和日至复制的分割，这个比较麻烦。
      } else {
        // 根据上次发送的entries多少，更新next_index_ 和 match_index_。
        // 根据match_index_更新commit_index_
        // 根据commit_index_更新last_applied_
        // 在append_entries_reply中添加新的字段，表示follower目前的存储进度。
        uint64_t id = con->getContext<uint64_t>();
        assert(next_index_[id] <= aer.next_index);
        next_index_[id] = aer.next_index;
        match_index_[id] = aer.next_index - 1;
        UpdateCommitIndexFromMatchIndex();
      }
    }
  } else if(j["type"].get<std::string>() == "request_vote_reply") {
    RequestVoteReply rev = GetRequestVoteReply(j);
    if(rev.term <= current_term_) {
      //忽略即可
    } else {
      BecomeFollower(rev.term, -1);
    }
  }
  else {
    // 正常情况下leader只会收到append entries reply信息、request vote reply信息和客户端的读写信息
    // 可能由于网络分区，抖动等问题收到其他信息。
    if(j["type"].get<std::string>() == "append_entries") {
      // 在进入常规的处理函数前，判断term不能相等
      uint64_t term = j["term"].get<uint64_t>();
      if(term == current_term_) {
        // 即使是网络分区，raft也不可能出现两个leader拥有相同的term，严重错误。
        ERROR("internal error : two leaders have the same term : ", term);
      }
      auto aer = HandleAppendEntries(GetAppendEntries(j));
      con->Send(CreateAppendEntriesReply(aer));
    } else if(j["type"].get<std::string>() == "request_vote") {
      // candidate变成leader，也不能修改voted_for_
      // 因为可能在leader状态可能会受到请求投票信息
      // 例如某个candidate获得过半票并变身成为leader，在通知其他节点之前
      // 某个节点follower超时并发送了request vote信息给leader
      assert(voted_for_ == static_cast<int64_t>(my_id_));
      auto rvr = HandleRequestVote(GetRequestVote(j));
      con->Send(CreateRequestVoteReply(rvr));
    } else {
      ERROR("error message type : ", j["type"].get<std::string>(), " on state leader.");
    }
  }
}

void RaftNode::UpdateCommitIndexFromMatchIndex() {
  std::vector<uint64_t> match_indexs;
  for(const auto& each : match_index_) {
    match_indexs.push_back(each.second);
  }
  std::sort(match_indexs.begin(), match_indexs.end());
  uint64_t total_size = 1 + match_indexs.size();
  // if total_size == 5
  // need 3th index. 5 / 2 + 1 == 3
  // 3th == array[3 - 1];
  // else if total_size == 4
  // need 3th index. 4 / 2 + 1 == 3
  // 3th == array[3 - 1];
  uint64_t should_commit = match_indexs[total_size / 2];
  // 注意， 本term不能直接提交以前term的日志
  if(current_term_ == logs_->GetTermFromIndex(should_commit)) {
    assert(commit_index_ >= should_commit);
    commit_index_ = should_commit;
    // Done : update last_applied_ form commit_index_;
    ApplyToStateMachine();
  } else {
    assert(current_term_ > logs_->GetTermFromIndex(should_commit));
  }
}

// fix stupid error.
void RaftNode::ApplyToStateMachine() {
  while(last_applied_ < commit_index_) {
    ++last_applied_;
    if(cb_) {
      cb_(logs_->GetCommandFromIndex(last_applied_));
      if(real_clients_.find(last_applied_) != real_clients_.end()) {
        auto id = real_clients_[last_applied_].id;
        auto client = real_clients_[last_applied_].client;
        client->Send(CreateProposeReply(ProposeReply{id}));
        real_clients_.erase(last_applied_);
      }
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
  if((other_nodes_.size() + 1) * 2 <= raft::Config::instance().Nodes().size()) {
    ERROR("too many nodes leave, now : ", other_nodes_.size() + 1, " total : ", raft::Config::instance().Nodes().size());
  }
}

void RaftNode::ClientLeave(std::shared_ptr<puck::TcpConnection> con) {
  INFO("client connection close");
  if(con->GetState() == puck::TcpConnection::ConnState::ReadZero) {
    INFO("tcp connection state : read_zero");
  } else {
    WARN("tcp connection state : ", con->GetStateStr());
  }
}
}
