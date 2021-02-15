#include "session.h"
#include "util.h"
#include "multi_buffers.h"
#include "asio_wrapper/timer.h"
#include <cassert>
#include <memory>
#include <string>

namespace puck {
Session::Session(asio::io_context& io, tcp::socket socket):
                 io_(io),
                 socket_(std::move(socket)),
                 length_(0),
                 writing_(false),
                 have_closed_(false),
                 tcp_connection_(std::make_shared<TcpConnection>(*this)) {

}

void Session::Start() {
  OnConnection();
  if(tcp_connection_->ShouldClose() == true) {
    OnClose();
  }
  ReadLength();
}

void Session::RunAfter(std::function<void(std::shared_ptr<TcpConnection>)> fn, asio::chrono::microseconds ms) {
  using timer_type = puck::Timer<asio::chrono::microseconds>;
  auto self(shared_from_this());
  std::shared_ptr<timer_type> timer = std::make_shared<timer_type>([fn, self](const asio::error_code& ec)->void {
    if(self->closed() == true) {
      return;
    }
    if(ec) {
      self->tcp_connection_->SetState(TcpConnection::ConnState::TimeError);
      self->OnClose();
      return;
    }
    fn(self->tcp_connection_);
    if(self->tcp_connection_->ShouldClose() == true) {
      self->OnClose();
    }
    return;
    }, io_, ms);
  timer->Start();
}

void Session::ReadLength() {
  auto self(shared_from_this());
  asio::async_read(socket_, asio::buffer(&length_, sizeof(length_)),
                   [self](std::error_code ec, std::size_t)-> void {
    if(self->closed() == true) {
      return;
    }
    if(!ec) {
      self->length_ = util::NetworkToHost(self->length_);
      if(self->length_ > sizeof(self->data_) - 1) {
        self->tcp_connection_->SetState(TcpConnection::ConnState::LengthError);
        self->OnClose();
      } else {
        self->ReadBody();
      }
    } else {
      if(ec == asio::error::eof) {
        self->tcp_connection_->SetState(TcpConnection::ConnState::ReadZero);
      } else {
        self->tcp_connection_->SetState(TcpConnection::ConnState::ReadError);
      }
      self->OnClose();
    }
  });
}

void Session::ReadBody() {
  auto self(shared_from_this());
  asio::async_read(socket_, asio::buffer(data_, length_),
                   [self](std::error_code ec, size_t length) -> void {
    if(self->closed() == true) {
      return;
    }
    if(!ec) {
      assert(length == self->length_);
      self->OnMessage();
      if(self->tcp_connection_->ShouldClose() == true) {
        self->OnClose();
      } else {
        self->ReadLength();
      }
    } else {
      if(ec == asio::error::eof) {
        if(self->length_ == length) {
          self->OnMessage();
        }
        self->tcp_connection_->SetState(TcpConnection::ConnState::ReadZero);
      } else {
        self->tcp_connection_->SetState(TcpConnection::ConnState::ReadError);
      }
      self->OnClose();
    }
  });
}

void Session::DoWrite(const std::string& content) {
  uint32_t n = content.size();
  n = util::HostToNetwork(n);
  write_bufs_.push_back(std::string(reinterpret_cast<const char*>(&n), sizeof(n)));
  write_bufs_.push_back(content);
  if(writing_ == true) {
    return;
  } else {
    writing_ = true;
    ContinueToSend();
  }
}

void Session::ContinueToSend() {
  assert(writing_ == true);
  if(write_bufs_.empty() == true) {
    writing_ = false;
    OnWriteComplete();
    if(tcp_connection_->ShouldClose() == true) {
      OnClose();
      return;
    }
  } else {
    MultiBuffer bufs;
    for(auto& each : write_bufs_) {
      bufs.Insert(std::move(each));
    }
    write_bufs_.clear();
    auto self(shared_from_this());
    asio::async_write(socket_, bufs, [self](std::error_code ec, std::size_t) -> void {
      if(self->closed() == true) {
        return;
      }
      if(!ec) {
        assert(self->writing_ == true);
        self->ContinueToSend();
      } else {
        self->tcp_connection_->SetState(TcpConnection::ConnState::WriteError);
        self->OnClose();
      }
    });
  }
}
}
