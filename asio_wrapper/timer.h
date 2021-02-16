#ifndef TIMER_H
#define TIMER_H
#include "asio.hpp"
#include "log.h"
#include <memory>
#include <functional>

namespace puck {
template <typename T>
class Timer : public std::enable_shared_from_this<Timer<T>> {
private:
  std::function<void(const asio::error_code& ec)> fn_;
  asio::io_context& io_;
  T ms_;
  asio::steady_timer st_;
  std::shared_ptr<bool> handle_;

public:
  Timer(std::function<void(const asio::error_code& ec)> fn, asio::io_context& io, T s) :
          fn_(fn),
          io_(io),
          ms_(s),
          st_(io_),
          handle_(std::make_shared<bool>(false)){

  }

  std::shared_ptr<bool> Start() {
    size_t i = st_.expires_after(ms_);
    assert(i == 0);
    auto self = this->shared_from_this();
    st_.async_wait([self](const asio::error_code& ec)-> void {
      if(*(self->handle_) == true) {
        return;
      }
      self->fn_(ec);
    });
    return handle_;
  }
};
}

#endif // TIMER_H
