#include "server.h"
#include "session.h"
#include "log.h"

namespace puck {
Server::Server(asio::io_context& io, uint16_t port) :
               acceptor_(io, tcp::endpoint(tcp::v4(), port)),
               io_(io) {
  DoAccept();
}

void Server::DoAccept() {
  acceptor_.async_accept([this](std::error_code ec, tcp::socket socket)->void {
    if(!ec) {
      auto s = std::make_shared<Session>(io_, std::move(socket));
      s->SetOnConnection(on_connection_);
      s->SetOnMessage(on_message_);
      s->SetOnWriteComplete(on_write_complete_);
      s->SetOnClose(on_close_);
      s->Start();
    } else {
      WARN("accept error : ", ec.message());
    }
    DoAccept();
  });
}

void Server::SetOnMessage(const callback_type& cb) {
  on_message_ = cb;
}

void Server::SetOnConnection(const callback_type& cb) {
  on_connection_ = cb;
}

void Server::SetOnWriteComplete(const callback_type& cb) {
  on_write_complete_ = cb;
}

void Server::SetOnClose(const callback_type& cb) {
  on_close_ = cb;
}
}
