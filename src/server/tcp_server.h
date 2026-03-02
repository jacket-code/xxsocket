#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "xxsocket.h"

namespace server {

struct ClientInfo {
  int64_t id = 0;
  purelib::net::socket_native_type fd = 0;
};

class TcpServer {
public:
  using LineHandler = std::function<void(const ClientInfo&, const std::string& line)>;
  using DisconnectHandler = std::function<void(const ClientInfo&)>;
  using TickHandler = std::function<void(int64_t nowMs)>;

  TcpServer() = default;
  bool listen(uint16_t port, std::string& err);
  // Process I/O once and return; timeoutMs is the max wait in select().
  void pollOnce(int timeoutMs, const LineHandler& onLine, const DisconnectHandler& onDisconnect);
  void run(const LineHandler& onLine, const DisconnectHandler& onDisconnect, const TickHandler& onTick = {});

  bool sendLine(int64_t clientId, const std::string& line);
  void broadcastLine(const std::vector<int64_t>& clientIds, const std::string& line);

private:
  struct Client {
    int64_t id = 0;
    purelib::net::xxsocket sock;
    std::string inbuf;
  };

  purelib::net::xxsocket listenSock_;
  int64_t nextClientId_ = 1;
  std::vector<Client> clients_;

  Client* findClient(int64_t clientId);
  void dropClient(size_t idx);
};

} // namespace server

