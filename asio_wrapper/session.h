#ifndef SESSION_H
#define SESSION_H

#include "asio.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "tcp_connection.h"

using asio::ip::tcp;
namespace puck {
class Session : public std::enable_shared_from_this<Session> {
public:
  friend class TcpConnection;
  using callback_type = std::function<void(std::shared_ptr<TcpConnection> con)>;

  Session(asio::io_context& io, tcp::socket socket);

  Session(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(const Session&) = delete;
  Session& operator=(Session&&) = delete;

  void SetOnConnection(const callback_type& cb) {
    on_connection_ = cb;
  }

  void SetOnMessage(const callback_type& cb) {
    on_message_ = cb;
  }

  void SetOnWriteComplete(const callback_type& cb) {
    on_write_complete_ = cb;
  }

  void SetOnClose(const callback_type& cb) {
    on_close_ = cb;
  }

  void Start();

  void RunAfter(std::function<void(std::shared_ptr<TcpConnection>)> fn, asio::chrono::microseconds ms);

private:
  void ReadLength();
  void ReadBody();
  void DoWrite(const std::string& content);
  void ContinueToSend();

  constexpr static size_t BUF_SIZE = 4096;
  asio::io_context& io_;
  tcp::socket socket_;
  uint32_t length_;
  char data_[BUF_SIZE];
  bool writing_;
  std::vector<std::string> write_bufs_;

  callback_type on_connection_;
  callback_type on_message_;
  callback_type on_close_;
  bool have_closed_;
  callback_type on_write_complete_;

  std::shared_ptr<TcpConnection> tcp_connection_;

  bool closed() const {
    return have_closed_;
  }

  void OnMessage() {
    if(on_message_) {
      on_message_(tcp_connection_);
    }
  }

  void OnConnection() {
    if(on_connection_) {
      on_connection_(tcp_connection_);
    }
  }

  //可能被多次调用
  void OnClose() {
    if(closed() == true) {
      return;
    }
    have_closed_ = true;
    if(on_close_) {
      on_close_(tcp_connection_);
    }
    socket_.close();
  }

  void OnWriteComplete() {
    if(on_write_complete_) {
      on_write_complete_(tcp_connection_);
    }
  }
};
}

#endif // SESSION_H
