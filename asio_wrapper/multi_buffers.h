#ifndef MULTI_BUFFERS_H
#define MULTI_BUFFERS_H

#include "asio.hpp"
#include <memory>
#include <list>
#include <vector>

namespace puck {
class MultiBuffer {
public:
  explicit MultiBuffer():bufs_(), strs_(new std::list<std::string>()) {

  }

  template <typename T>
  void Insert(T&& buf) {
    strs_->push_back(std::forward<T>(buf));
    const std::string& tmp = strs_->back();
    bufs_.push_back(asio::const_buffer(tmp.c_str(), tmp.length()));
  }

  using value_type = asio::const_buffer;
  using const_iterator = std::vector<asio::const_buffer>::const_iterator;

  const_iterator begin() const {
    return bufs_.begin();
  }

  const_iterator end() const {
    return bufs_.end();
  }

private:
  std::vector<asio::const_buffer> bufs_;
  std::shared_ptr<std::list<std::string>> strs_;
};
}
#endif // MULTI_BUFFERS_H
