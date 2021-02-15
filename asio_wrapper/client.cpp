#include "client.h"
#include "session.h"
#include "util.h"
#include "log.h"

namespace puck {
Client::Client(asio::io_context& io, std::string ip, uint16_t port) :
               io_(io),
               socket_(io),
               retry_time_(0),
               retry_duration_second_(0),
               retry_count_(0) {
  tcp::resolver resolver(io_);
  ret_ = resolver.resolve(ip, std::to_string(port));
}

void Client::Connect() {
  asio::async_connect(socket_, ret_, [this](std::error_code ec, tcp::endpoint) -> void {
    if(!ec) {
      auto s = std::make_shared<Session>(io_, std::move(socket_));
      s->SetOnConnection(on_connection_);
      s->SetOnMessage(on_message_);
      s->SetOnWriteComplete(on_write_complete_);
      s->SetOnClose(on_close_);
      s->Start();
    } else {
      WARN("connect error : ", ec.message());
      if(retry_count_ < retry_time_) {
        ++retry_count_;
        util::SleepS(retry_duration_second_);
        // 保险起见，重新申请一个socket，不知道asio是否支持socket连接失败后重用。
        socket_ = asio::ip::tcp::socket(io_);
        Connect();
      } else {
        ERROR("can not connect.");
      }
    }
  });
}

void Client::SetOnMessage(const callback_type& cb) {
  on_message_ = cb;
}

void Client::SetOnConnection(const callback_type& cb) {
  on_connection_ = cb;
}

void Client::SetOnWriteComplete(const callback_type& cb) {
  on_write_complete_ = cb;
}

void Client::SetOnClose(const callback_type& cb) {
  on_close_ = cb;
}
}
