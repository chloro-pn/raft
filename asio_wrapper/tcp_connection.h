#ifndef TCP_CONNECTION_H
#define TCP_CONNECTION_H

#include <string>
#include <memory>
#include <functional>
#include "asio.hpp"
#include "log.h"

namespace puck {
class Session;

class TcpConnection {
public:
  friend class Session;

  enum class ConnState {  GoOn,
                          ReadZero,
                          ReadError,
                          WriteError,
                          ShutdownError,
                          ForceClose,
                          TimeError,
                          //单个包过长，缓冲区装不下
                          LengthError,
                       };

  TcpConnection(Session& sess);

  // write
  void Send(const std::string& str);

  // read
  const char* MessageData() const;

  size_t MessageLength() const;

  // close
  void ShutdownW();

  void ForceClose();

  // address
  const std::string& Iport() const {
    return iport_;
  }

  // get/set context.
  template <typename T>
  T& getContext() {
    return *static_cast<T*>(context_.get());
  }

  void SetContext(std::shared_ptr<void> cxt) {
    context_ = cxt;
  }

  void RunAfter(std::function<void(std::shared_ptr<TcpConnection>)> fn, asio::chrono::microseconds ms);

  ConnState GetState() const {
    return state_;
  }

  const char* GetStateStr() const {
    if(state_ == ConnState::GoOn) {
      return "go_on";
    } else if(state_ == ConnState::ForceClose) {
      return "force_close";
    } else if(state_ == ConnState::LengthError) {
      return "length_error";
    } else if(state_ == ConnState::ReadError) {
      return "read_error";
    } else if(state_ == ConnState::ReadZero) {
      return "read_zero";
    } else if(state_ == ConnState::ShutdownError) {
      return "shutdown_error";
    } else if(state_ == ConnState::TimeError) {
      return "time_error";
    } else if(state_ == ConnState::WriteError) {
      return "write_error";
    } else {
      ERROR("unknow state.");
    }
  }

private:
  void SetState(ConnState state) {
    state_ = state;
  }

  bool ShouldClose() const {
    return state_ != ConnState::GoOn;
  }

  Session& session_;
  // iport -> ip : port
  std::string iport_;
  // 连接状态
  ConnState state_;
  // 连接上下文
  std::shared_ptr<void> context_;
};
}

#endif // TCP_CONNECTION_H
