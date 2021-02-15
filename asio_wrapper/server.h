#ifndef SERVER_H
#define SERVER_H

#include "session.h"
#include "asio.hpp"
#include <memory>
#include <functional>

using asio::ip::tcp;

namespace puck {
// 回调函数捕获了this指针，因此Server类对象是non-copy and non-move的
class Server {
public:
  using callback_type = std::function<void(std::shared_ptr<TcpConnection> con)>;

  Server(asio::io_context& io, uint16_t port);

  Server(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(const Server&) = delete;
  Server& operator=(Server&&) = delete;

  void SetOnMessage(const callback_type& cb);
  void SetOnConnection(const callback_type& cb);
  void SetOnWriteComplete(const callback_type& cb);
  void SetOnClose(const callback_type& cb);

private:
  void DoAccept();

  tcp::acceptor acceptor_;
  callback_type on_message_;
  callback_type on_connection_;
  callback_type on_close_;
  callback_type on_write_complete_;
  asio::io_context& io_;
};
}
#endif // SERVER_H
