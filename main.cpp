#include <iostream>
#include <memory>
#include "asio_wrapper/server.h"
#include "asio_wrapper/client.h"
#include "third_party/json.hpp"
#include "config.h"
#include "raft_node.h"
#include "log.h"

class Echo {
public:
  explicit Echo(asio::io_context& io) : server_(io, 12346) {
    server_.SetOnConnection([](std::shared_ptr<puck::TcpConnection> con) {
      std::cout << "new connection : " << con->Iport() << std::endl;
      con->RunAfter([](std::shared_ptr<puck::TcpConnection> con) {
        std::cout << "hello world" << std::endl;
        con->ForceClose();
      }, asio::chrono::seconds(3));
    });

    server_.SetOnMessage([](std::shared_ptr<puck::TcpConnection> con) {
      std::string message(con->MessageData(), con->MessageLength());
      con->Send(message);
    });

    server_.SetOnClose([](std::shared_ptr<puck::TcpConnection> con) {
      std::cout << "close ";
      if(con->GetState() == puck::TcpConnection::ConnState::ReadZero) {
        con->ShutdownW();
        std::cout << "normally." << std::endl;
      } else {
        std::cout << " error." << std::endl;
      }
    });
  }

private:
  puck::Server server_;
};

int main() {
  puck::Config::instance().Init("raft.config");
  asio::io_context io;
  //puck::Client client(io, "127.0.0.1", 12345);
  //client.SetRetry(5, 1);
  //client.Connect();
  Echo echo(io);
  //puck::RaftNode raft(io);
  io.run();
  return 0;
}

