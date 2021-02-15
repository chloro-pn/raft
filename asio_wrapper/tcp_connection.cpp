#include "tcp_connection.h"
#include "session.h"

static void set_iport(asio::ip::tcp::socket& socket_, std::string& iport_) {
  auto lip = socket_.local_endpoint().address().to_string();
  auto lport = socket_.local_endpoint().port();
  auto pip = socket_.remote_endpoint().address().to_string();
  auto pport = socket_.remote_endpoint().port();
  iport_.append(lip);
  iport_.append(":");
  iport_.append(std::to_string(lport));
  iport_.append("->");
  iport_.append(pip);
  iport_.append(":");
  iport_.append(std::to_string(pport));
}

namespace puck {
TcpConnection::TcpConnection(Session& s):
                             session_(s),
                             iport_(),
                             state_(ConnState::GoOn) {
  set_iport(session_.socket_, iport_);
}

void TcpConnection::Send(const std::string& str) {
  session_.DoWrite(str);
}

const char* TcpConnection::MessageData() const {
  return session_.data_;
}

size_t TcpConnection::MessageLength() const {
  return session_.length_;
}

void TcpConnection::ShutdownW() {
  asio::error_code ec;
  session_.socket_.shutdown(asio::socket_base::shutdown_send, ec);
  if(!ec) {
    return;
  } else {
    SetState(ConnState::ShutdownError);
    return;
  }
}

void TcpConnection::ForceClose() {
  SetState(ConnState::ForceClose);
}

void TcpConnection::RunAfter(std::function<void(std::shared_ptr<TcpConnection>)> fn, asio::chrono::microseconds ms) {
  session_.RunAfter(fn, ms);
}
}
