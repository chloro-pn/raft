#ifndef CLIENT_H
#define CLIENT_H

#include "session.h"
#include "asio.hpp"
#include <memory>
#include <functional>

using asio::ip::tcp;

namespace puck {
// 回调函数捕获了this指针，因此Client类的对象是non-copy and non-move的。
class Client {
public:
  using callback_type = std::function<void(std::shared_ptr<TcpConnection> con)>;

  Client(asio::io_context& io, std::string ip, uint16_t port);

  Client(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(const Client&) = delete;
  Client& operator=(Client&&) = delete;

  void Connect();

  void SetOnMessage(const callback_type& cb);

  void SetOnConnection(const callback_type& cb);

  void SetOnClose(const callback_type& cb);

  void SetOnWriteComplete(const callback_type& cb);

  void SetRetry(int time, int second) {
    assert(time > 0 && second > 0);
    retry_time_ = time;
    retry_duration_second_ = second;
  }

private:
  asio::io_context& io_;
  tcp::resolver::results_type ret_;
  tcp::socket socket_;
  size_t retry_time_;
  int retry_duration_second_;
  size_t retry_count_;

  callback_type on_connection_;
  callback_type on_message_;
  callback_type on_write_complete_;
  callback_type on_close_;
};
}

#endif // CLIENT_H
