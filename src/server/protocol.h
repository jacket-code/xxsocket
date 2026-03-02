#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker/holdem_table.h"
#include "server/tcp_server.h"

namespace server {

class GameServer {
public:
  explicit GameServer(uint16_t port);
  int run();

private:
  struct ClientSession {
    int64_t clientId = 0;
    std::string playerId;
    std::string name;
    int tableId = 1;
  };

  uint16_t port_;
  TcpServer tcp_;
  poker::HoldemTable table_;

  std::unordered_map<int64_t, ClientSession> byClient_;
  std::unordered_map<std::string, int64_t> clientByPlayerId_;

  void onLine(const ClientInfo& c, const std::string& line);
  void onDisconnect(const ClientInfo& c);

  std::string newPlayerId() const;
  void sendError(int64_t clientId, const std::string& msg);
  void sendInfo(int64_t clientId, const std::string& msg);
  void sendWelcome(int64_t clientId, const ClientSession& sess);
  void broadcastTableState();
  std::string serializeSnapshotFor(const std::string& viewerPlayerId) const;
};

} // namespace server

