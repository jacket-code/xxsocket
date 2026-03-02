#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "poker/holdem_table.h"
#include "server/rate_limiter.h"
#include "server/store.h"
#include "server/tcp_server.h"

namespace server {

enum class RoomType : uint8_t { Cash = 0, Private = 1, Sng = 2, Mtt = 3 };

struct RoomConfig {
  int roomId = 1;
  RoomType type = RoomType::Cash;
  std::string name = "lobby";
  std::string password; // private rooms

  int maxTables = 4;
  int maxSeats = 6;
  int64_t sb = 50;
  int64_t bb = 100;

  int64_t buyinMin = 1000;
  int64_t buyinMax = 20000;
};

struct TableNode {
  int tableId = 1;
  int roomId = 1;
  poker::HoldemTable table;
  std::unordered_set<std::string> observers;

  explicit TableNode(int tid, int rid, poker::HoldemTable::Config cfg) : tableId(tid), roomId(rid), table(cfg) {}
};

class AppServer {
public:
  AppServer(uint16_t port, std::string dataDir);
  int run();

private:
  struct Session {
    int64_t clientId = 0;
    std::string playerId;
    std::string name;
    int64_t lastSeenMs = 0;
    int currentRoomId = 1;
    int currentTableId = 1;
  };

  uint16_t port_;
  TcpServer tcp_;
  RateLimiter rl_;
  Store store_;

  int nextRoomId_ = 2;
  int nextTableId_ = 2;

  std::unordered_map<int64_t, Session> sessionsByClient_;
  std::unordered_map<std::string, int64_t> clientByPlayerId_;

  std::unordered_map<int, RoomConfig> rooms_;
  std::unordered_map<int, TableNode> tables_;

  struct Tournament {
    int roomId = 0;
    RoomType type = RoomType::Sng;
    int64_t buyin = 0;
    int64_t startingChips = 20000;
    bool started = false;
    bool finished = false;
    int64_t prizePool = 0;
    int levelIndex = 0;
    int64_t levelStartMs = 0;
    int64_t levelDurationMs = 120000; // 2 minutes per level (demo)
    std::vector<std::pair<int64_t, int64_t>> blindLevels; // (sb,bb)
    std::vector<std::string> registrants;
    std::vector<int> tableIds;
  };
  std::unordered_map<int, Tournament> tournaments_; // key=roomId

  // metrics
  int64_t totalConnections_ = 0;
  int64_t totalCommands_ = 0;
  int64_t rejectedByRateLimit_ = 0;

  void onLine(const ClientInfo& c, const std::string& line, int64_t nowMs);
  void onDisconnect(const ClientInfo& c);
  void onTick(int64_t nowMs);

  // helpers
  static std::string q(const std::string& s);
  std::string newPlayerId(int64_t nowMs) const;

  void sendError(int64_t clientId, const std::string& msg);
  void sendInfo(int64_t clientId, const std::string& msg);
  void sendWelcome(int64_t clientId, const Session& sess, const Account& acc);

  std::string roomTypeToString(RoomType t) const;
  std::string serializeRooms() const;
  std::string serializeTables(int roomId) const;
  std::string serializeSnapshotFor(const TableNode& tn, const std::string& viewerPlayerId) const;

  bool ensureLobbyCreated(int64_t nowMs, std::string& err);
  TableNode* getTable(int tableId);
  const TableNode* getTable(int tableId) const;
  const RoomConfig* getRoom(int roomId) const;
  bool checkRoomPassword(const RoomConfig& r, const std::string& pass) const;

  void broadcastTable(const TableNode& tn);
  void sendTableToClient(const TableNode& tn, const Session& sess);

  // tournament helpers
  void maybeStartTournament(int roomId, int64_t nowMs);
  void tickTournaments(int64_t nowMs);
};

} // namespace server

