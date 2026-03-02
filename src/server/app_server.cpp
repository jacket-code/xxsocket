#include "server/app_server.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

#include "server/jsonlite.h"

namespace server {
namespace {

static int64_t nowSteadyMs() {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())
      .time_since_epoch()
      .count();
}

static std::string streetToString(poker::Street st) {
  switch (st) {
    case poker::Street::Preflop: return "preflop";
    case poker::Street::Flop: return "flop";
    case poker::Street::Turn: return "turn";
    case poker::Street::River: return "river";
    case poker::Street::Showdown: return "showdown";
  }
  return "unknown";
}

static std::string statusToString(poker::PlayerStatus st) {
  switch (st) {
    case poker::PlayerStatus::Empty: return "empty";
    case poker::PlayerStatus::Seated: return "seated";
    case poker::PlayerStatus::InHand: return "in_hand";
    case poker::PlayerStatus::Folded: return "folded";
    case poker::PlayerStatus::AllIn: return "all_in";
  }
  return "unknown";
}

} // namespace

std::string AppServer::q(const std::string& s) { return "\"" + jsonlite::escape(s) + "\""; }

AppServer::AppServer(uint16_t port, std::string dataDir)
    : port_(port),
      rl_(RateLimiter::Config{60, 40}),
      cfgMgr_(dataDir + "/config.json"),
      store_(std::move(dataDir)) {}

std::string AppServer::roomTypeToString(RoomType t) const {
  switch (t) {
    case RoomType::Cash: return "cash";
    case RoomType::Private: return "private";
    case RoomType::Sng: return "sng";
    case RoomType::Mtt: return "mtt";
  }
  return "unknown";
}

std::string AppServer::newPlayerId(int64_t nowMs) const {
  static thread_local std::mt19937_64 rng(static_cast<uint64_t>(nowMs));
  std::uniform_int_distribution<uint64_t> dist;
  uint64_t a = dist(rng);
  uint64_t b = dist(rng);
  std::ostringstream ss;
  ss << std::hex << a << b;
  return ss.str();
}

bool AppServer::ensureLobbyCreated(int64_t nowMs, std::string& err) {
  if (rooms_.find(1) != rooms_.end()) return true;
  RoomConfig lobby;
  lobby.roomId = 1;
  lobby.type = RoomType::Cash;
  lobby.name = "lobby";
  rooms_[1] = lobby;

  poker::HoldemTable::Config cfg;
  cfg.tableId = 1;
  cfg.maxSeats = lobby.maxSeats;
  cfg.smallBlind = lobby.sb;
  cfg.bigBlind = lobby.bb;
  tables_.emplace(1, TableNode(1, 1, cfg));
  tables_.at(1).table.setActionTimeoutMs(cfgMgr_.config().actionTimeoutMs);
  (void)nowMs;
  return true;
}

TableNode* AppServer::getTable(int tableId) {
  auto it = tables_.find(tableId);
  if (it == tables_.end()) return nullptr;
  return &it->second;
}

const TableNode* AppServer::getTable(int tableId) const {
  auto it = tables_.find(tableId);
  if (it == tables_.end()) return nullptr;
  return &it->second;
}

const RoomConfig* AppServer::getRoom(int roomId) const {
  auto it = rooms_.find(roomId);
  if (it == rooms_.end()) return nullptr;
  return &it->second;
}

bool AppServer::checkRoomPassword(const RoomConfig& r, const std::string& pass) const {
  if (r.type != RoomType::Private) return true;
  return r.password == pass;
}

void AppServer::sendError(int64_t clientId, const std::string& msg) {
  tcp_.sendLine(clientId, std::string("{\"type\":\"error\",\"message\":") + q(msg) + "}");
}

void AppServer::sendInfo(int64_t clientId, const std::string& msg) {
  tcp_.sendLine(clientId, std::string("{\"type\":\"info\",\"message\":") + q(msg) + "}");
}

void AppServer::sendWelcome(int64_t clientId, const Session& sess, const Account& acc) {
  std::ostringstream ss;
  ss << "{\"type\":\"welcome\",\"playerId\":" << q(sess.playerId)
     << ",\"name\":" << q(sess.name)
     << ",\"balance\":" << acc.balance
     << "}";
  tcp_.sendLine(clientId, ss.str());
  tcp_.sendLine(clientId, serializeRooms());
  tcp_.sendLine(clientId, serializeTables(sess.currentRoomId));
  auto* tn = getTable(sess.currentTableId);
  if (tn) tcp_.sendLine(clientId, serializeSnapshotFor(*tn, sess.playerId));
}

std::string AppServer::serializeRooms() const {
  std::ostringstream ss;
  ss << "{\"type\":\"rooms\",\"rooms\":[";
  bool first = true;
  for (const auto& kv : rooms_) {
    const auto& r = kv.second;
    if (!first) ss << ",";
    first = false;
    ss << "{"
       << "\"roomId\":" << r.roomId
       << ",\"roomType\":" << q(roomTypeToString(r.type))
       << ",\"name\":" << q(r.name)
       << ",\"maxTables\":" << r.maxTables
       << ",\"maxSeats\":" << r.maxSeats
       << ",\"sb\":" << r.sb
       << ",\"bb\":" << r.bb
       << ",\"buyinMin\":" << r.buyinMin
       << ",\"buyinMax\":" << r.buyinMax
       << "}";
  }
  ss << "]}";
  return ss.str();
}

std::string AppServer::serializeTables(int roomId) const {
  std::ostringstream ss;
  ss << "{\"type\":\"tables\",\"roomId\":" << roomId << ",\"tables\":[";
  bool first = true;
  for (const auto& kv : tables_) {
    const auto& t = kv.second;
    if (t.roomId != roomId) continue;
    if (!first) ss << ",";
    first = false;
    ss << "{\"tableId\":" << t.tableId
       << ",\"maxSeats\":" << t.table.config().maxSeats
       << ",\"sb\":" << t.table.config().smallBlind
       << ",\"bb\":" << t.table.config().bigBlind
       << "}";
  }
  ss << "]}";
  return ss.str();
}

std::string AppServer::serializeSnapshotFor(const TableNode& tn, const std::string& viewerPlayerId) const {
  auto snap = tn.table.snapshotFor(viewerPlayerId);
  std::ostringstream ss;
  ss << "{\"type\":\"table_state\""
     << ",\"roomId\":" << tn.roomId
     << ",\"tableId\":" << tn.tableId
     << ",\"handId\":" << snap.handId
     << ",\"street\":" << q(streetToString(snap.street))
     << ",\"dealer\":" << snap.dealer
     << ",\"sbSeat\":" << snap.sbSeat
     << ",\"bbSeat\":" << snap.bbSeat
     << ",\"toAct\":" << snap.toAct
     << ",\"actionDeadlineMs\":" << snap.actionDeadlineMs
     << ",\"pot\":" << snap.pot
     << ",\"currentBet\":" << snap.currentBet
     << ",\"board\":[";
  for (size_t i = 0; i < snap.board.size(); ++i) {
    if (i) ss << ",";
    ss << q(snap.board[i].toString());
  }
  ss << "],\"seats\":[";
  for (size_t i = 0; i < snap.seats.size(); ++i) {
    if (i) ss << ",";
    const auto& s = snap.seats[i];
    ss << "{"
       << "\"seat\":" << i
       << ",\"status\":" << q(statusToString(s.status))
       << ",\"playerId\":" << q(s.playerId)
       << ",\"name\":" << q(s.name)
       << ",\"chips\":" << s.chips
       << ",\"ready\":" << (s.ready ? "true" : "false")
       << ",\"auto\":" << (s.autoMode ? "true" : "false")
       << ",\"bet\":" << s.betThisStreet
       << ",\"contributed\":" << s.contributed;
    if (s.hasHole) ss << ",\"hole\":[" << q(s.hole[0].toString()) << "," << q(s.hole[1].toString()) << "]";
    ss << "}";
  }
  ss << "],\"observers\":" << static_cast<int>(tn.observers.size());

  const auto& la = snap.legal;
  ss << ",\"legal\":{"
     << "\"fold\":" << (la.canFold ? "true" : "false")
     << ",\"check\":" << (la.canCheck ? "true" : "false")
     << ",\"call\":" << (la.canCall ? "true" : "false")
     << ",\"bet\":" << (la.canBet ? "true" : "false")
     << ",\"raise\":" << (la.canRaise ? "true" : "false")
     << ",\"callAmount\":" << la.callAmount
     << ",\"minTo\":" << la.minBetOrRaiseTo
     << ",\"maxTo\":" << la.maxBetOrRaiseTo
     << "}";
  ss << "}";
  return ss.str();
}

void AppServer::broadcastTable(const TableNode& tn) {
  for (const auto& kv : sessionsByClient_) {
    const auto& sess = kv.second;
    if (sess.playerId.empty()) continue;
    if (sess.currentTableId != tn.tableId) continue;
    tcp_.sendLine(sess.clientId, serializeSnapshotFor(tn, sess.playerId));
  }
  for (const auto& pid : tn.observers) {
    auto it = clientByPlayerId_.find(pid);
    if (it != clientByPlayerId_.end()) {
      tcp_.sendLine(it->second, serializeSnapshotFor(tn, pid));
    }
  }
}

void AppServer::sendTableToClient(const TableNode& tn, const Session& sess) {
  tcp_.sendLine(sess.clientId, serializeSnapshotFor(tn, sess.playerId));
}

void AppServer::onDisconnect(const ClientInfo& c) {
  rl_.forget(c.id);
  auto it = sessionsByClient_.find(c.id);
  if (it == sessionsByClient_.end()) return;
  if (!it->second.playerId.empty()) {
    clientByPlayerId_.erase(it->second.playerId);
  }
  sessionsByClient_.erase(it);
}

void AppServer::onTick(int64_t nowMs) {
  // hot-reload config (best-effort)
  {
    std::string err;
    if (cfgMgr_.tickReload(nowMs, err)) {
      const auto& cfg = cfgMgr_.config();
      rl_ = RateLimiter(RateLimiter::Config{cfg.rlCapacity, cfg.rlRefillPerSecond});
      for (auto& kv : tables_) {
        kv.second.table.setActionTimeoutMs(cfg.actionTimeoutMs);
      }
    }
  }

  // tick all tables: timeout/autoplay + autostart
  for (auto& kv : tables_) {
    kv.second.table.tick(nowMs);
    (void)kv.second.table.startHandIfReady();
  }
  tickTournaments(nowMs);
  // flush store periodically (best-effort)
  static int64_t lastFlush = 0;
  int64_t flushMs = cfgMgr_.config().storeFlushMs;
  if (flushMs < 1000) flushMs = 1000;
  if (nowMs - lastFlush > flushMs) {
    std::string err;
    (void)store_.flush(err);
    lastFlush = nowMs;
  }
}

void AppServer::maybeStartTournament(int roomId, int64_t nowMs) {
  auto it = tournaments_.find(roomId);
  if (it == tournaments_.end()) return;
  auto& t = it->second;
  if (t.started || t.finished) return;
  if (t.registrants.size() < 2) return;
  const RoomConfig* r = getRoom(roomId);
  if (!r) return;

  // For MTT, create enough tables to seat all registrants.
  if (t.type == RoomType::Mtt) {
    int maxSeats = r->maxSeats;
    if (maxSeats < 2) maxSeats = 2;
    int need = static_cast<int>((t.registrants.size() + static_cast<size_t>(maxSeats) - 1) / static_cast<size_t>(maxSeats));
    while (static_cast<int>(t.tableIds.size()) < need) {
      int tid = nextTableId_++;
      poker::HoldemTable::Config cfg;
      cfg.tableId = tid;
      cfg.maxSeats = maxSeats;
      cfg.smallBlind = t.blindLevels.empty() ? r->sb : t.blindLevels[0].first;
      cfg.bigBlind = t.blindLevels.empty() ? r->bb : t.blindLevels[0].second;
      tables_.emplace(tid, TableNode(tid, roomId, cfg));
      tables_.at(tid).table.setActionTimeoutMs(cfgMgr_.config().actionTimeoutMs);
      t.tableIds.push_back(tid);
    }
  }

  if (t.tableIds.empty()) return;

  t.started = true;
  t.levelIndex = 0;
  t.levelStartMs = nowMs;

  // Apply initial blinds
  if (!t.blindLevels.empty()) {
    for (int tid : t.tableIds) {
      if (auto* tn = getTable(tid)) tn->table.setBlinds(t.blindLevels[0].first, t.blindLevels[0].second);
    }
  }

  // Seat registrants across tables
  size_t p = 0;
  for (int tid : t.tableIds) {
    auto* tn = getTable(tid);
    if (!tn) continue;
    auto maxSeats = tn->table.config().maxSeats;
    for (int seat = 0; seat < maxSeats && p < t.registrants.size(); ++seat) {
      const auto& pid = t.registrants[p++];
      // Name from account (best-effort)
      std::string name = pid;
      if (auto acc = store_.getAccount(pid)) name = acc->name;
      (void)tn->table.seatPlayer(seat, pid, name, t.startingChips);
      (void)tn->table.setReady(pid, true);
      (void)tn->table.setAutoMode(pid, true);
    }
    (void)tn->table.startHandIfReady();
    broadcastTable(*tn);
  }
}

void AppServer::tickTournaments(int64_t nowMs) {
  for (auto& kv : tournaments_) {
    auto& t = kv.second;
    if (!t.started || t.finished) continue;

    // Blind level advance
    if (!t.blindLevels.empty() && (nowMs - t.levelStartMs) >= t.levelDurationMs) {
      t.levelIndex++;
      if (t.levelIndex >= static_cast<int>(t.blindLevels.size())) t.levelIndex = static_cast<int>(t.blindLevels.size()) - 1;
      for (int tid : t.tableIds) {
        if (auto* tn = getTable(tid)) {
          tn->table.setBlinds(t.blindLevels[t.levelIndex].first, t.blindLevels[t.levelIndex].second);
          broadcastTable(*tn);
        }
      }
      t.levelStartMs = nowMs;
    }

    // Determine remaining players with chips > 0
    std::unordered_set<std::string> alive;
    for (int tid : t.tableIds) {
      auto* tn = getTable(tid);
      if (!tn) continue;
      auto snap = tn->table.snapshotFor(""); // hides holes only; we just inspect chips
      for (const auto& s : snap.seats) {
        if (!s.playerId.empty() && s.chips > 0) alive.insert(s.playerId);
      }
    }

    if (alive.size() <= 1) {
      t.finished = true;
      t.started = false;
      if (alive.size() == 1) {
        std::string winner = *alive.begin();
        // prize pool: buyin * entrants (demo, no rake)
        int64_t prize = t.prizePool;
        if (prize <= 0) prize = t.buyin * static_cast<int64_t>(t.registrants.size());
        std::string err;
        (void)store_.addBalance(winner, prize, nowMs, err);
        // Notify online winner
        auto itc = clientByPlayerId_.find(winner);
        if (itc != clientByPlayerId_.end()) sendInfo(itc->second, "tournament_won prize=" + std::to_string(prize));
      }
    }
  }
}

void AppServer::onLine(const ClientInfo& c, const std::string& line, int64_t nowMs) {
  totalCommands_++;
  if (!rl_.allow(c.id, nowMs, 1)) {
    rejectedByRateLimit_++;
    sendError(c.id, "rate_limited");
    return;
  }

  auto& sess = sessionsByClient_[c.id];
  sess.clientId = c.id;
  sess.lastSeenMs = nowMs;

  auto objOpt = jsonlite::parseObject(line);
  if (!objOpt) { sendError(c.id, "invalid json object"); return; }
  const auto& obj = *objOpt;
  auto typeOpt = obj.getString("type");
  if (!typeOpt) { sendError(c.id, "missing type"); return; }
  const std::string type = *typeOpt;

  if (type == "ping") { tcp_.sendLine(c.id, "{\"type\":\"pong\"}"); return; }

  if (type == "hello") {
    auto nameOpt = obj.getString("name");
    if (!nameOpt || nameOpt->empty()) { sendError(c.id, "missing name"); return; }
    std::string playerId;
    if (auto pid = obj.getString("playerId"); pid && !pid->empty()) playerId = *pid;
    else playerId = newPlayerId(nowMs);

    // takeover old session for reconnect
    auto old = clientByPlayerId_.find(playerId);
    if (old != clientByPlayerId_.end() && old->second != c.id) {
      sessionsByClient_.erase(old->second);
      clientByPlayerId_.erase(old);
    }

    sess.playerId = playerId;
    sess.name = *nameOpt;
    clientByPlayerId_[playerId] = c.id;

    std::string err;
    (void)ensureLobbyCreated(nowMs, err);
    sess.currentRoomId = 1;
    sess.currentTableId = 1;

    Account acc;
    if (auto a = store_.getAccount(playerId)) {
      acc = *a;
      if (acc.name.empty()) acc.name = sess.name;
    } else {
      acc.playerId = playerId;
      acc.name = sess.name;
      acc.balance = cfgMgr_.config().defaultBalance;
      acc.createdAtMs = nowMs;
      acc.updatedAtMs = nowMs;
    }
    store_.upsertAccount(acc);
    sendWelcome(c.id, sess, acc);
    return;
  }

  if (sess.playerId.empty()) { sendError(c.id, "please hello first"); return; }

  if (type == "list_rooms") {
    tcp_.sendLine(c.id, serializeRooms());
    return;
  }

  if (type == "create_room") {
    RoomConfig rc;
    rc.roomId = nextRoomId_++;
    if (auto v = obj.getString("name")) rc.name = *v;
    auto t = obj.getString("roomType");
    if (!t) { sendError(c.id, "create_room requires roomType"); return; }
    if (*t == "cash") rc.type = RoomType::Cash;
    else if (*t == "private") rc.type = RoomType::Private;
    else if (*t == "sng") rc.type = RoomType::Sng;
    else if (*t == "mtt") rc.type = RoomType::Mtt;
    else { sendError(c.id, "unknown roomType"); return; }
    if (rc.type == RoomType::Private) {
      if (auto p = obj.getString("password")) rc.password = *p;
    }
    if (auto v = obj.getInt("maxSeats")) rc.maxSeats = static_cast<int>(*v);
    if (auto v = obj.getInt("sb")) rc.sb = *v;
    if (auto v = obj.getInt("bb")) rc.bb = *v;
    if (auto v = obj.getInt("buyinMin")) rc.buyinMin = *v;
    if (auto v = obj.getInt("buyinMax")) rc.buyinMax = *v;
    if (rc.maxSeats < 2) rc.maxSeats = 2;
    if (rc.maxSeats > 9) rc.maxSeats = 9;

    rooms_[rc.roomId] = rc;
    // create first table for cash/private
    if (rc.type == RoomType::Cash || rc.type == RoomType::Private) {
      int tid = nextTableId_++;
      poker::HoldemTable::Config cfg;
      cfg.tableId = tid;
      cfg.maxSeats = rc.maxSeats;
      cfg.smallBlind = rc.sb;
      cfg.bigBlind = rc.bb;
      tables_.emplace(tid, TableNode(tid, rc.roomId, cfg));
      tables_.at(tid).table.setActionTimeoutMs(cfgMgr_.config().actionTimeoutMs);
    } else if (rc.type == RoomType::Sng || rc.type == RoomType::Mtt) {
      Tournament t;
      t.roomId = rc.roomId;
      t.type = rc.type;
      t.buyin = rc.buyinMin > 0 ? rc.buyinMin : 1000;
      t.prizePool = 0;
      t.startingChips = 20000;
      t.blindLevels = {{25, 50}, {50, 100}, {75, 150}, {100, 200}, {150, 300}, {200, 400}, {300, 600}, {400, 800}};
      // create one table; MTT can scale later by adding more
      int tid = nextTableId_++;
      poker::HoldemTable::Config cfg;
      cfg.tableId = tid;
      cfg.maxSeats = rc.maxSeats;
      cfg.smallBlind = t.blindLevels[0].first;
      cfg.bigBlind = t.blindLevels[0].second;
      tables_.emplace(tid, TableNode(tid, rc.roomId, cfg));
      tables_.at(tid).table.setActionTimeoutMs(cfgMgr_.config().actionTimeoutMs);
      t.tableIds.push_back(tid);
      tournaments_[rc.roomId] = t;
    }
    tcp_.sendLine(c.id, serializeRooms());
    return;
  }

  if (type == "join_room") {
    auto rid = obj.getInt("roomId");
    if (!rid) { sendError(c.id, "join_room requires roomId"); return; }
    const RoomConfig* r = getRoom(static_cast<int>(*rid));
    if (!r) { sendError(c.id, "room not found"); return; }
    std::string pass;
    if (auto p = obj.getString("password")) pass = *p;
    if (!checkRoomPassword(*r, pass)) { sendError(c.id, "bad password"); return; }
    sess.currentRoomId = r->roomId;
    // pick first table in room if exists
    for (const auto& kv : tables_) {
      if (kv.second.roomId == r->roomId) { sess.currentTableId = kv.second.tableId; break; }
    }
    tcp_.sendLine(c.id, serializeTables(sess.currentRoomId));
    if (auto* tn = getTable(sess.currentTableId)) sendTableToClient(*tn, sess);
    // tournament info if needed
    auto tit = tournaments_.find(sess.currentRoomId);
    if (tit != tournaments_.end()) {
      std::ostringstream ss;
      ss << "{\"type\":\"tournament_info\",\"roomId\":" << tit->second.roomId
         << ",\"started\":" << (tit->second.started ? "true" : "false")
         << ",\"finished\":" << (tit->second.finished ? "true" : "false")
         << ",\"buyin\":" << tit->second.buyin
         << ",\"startingChips\":" << tit->second.startingChips
         << ",\"registrants\":" << tit->second.registrants.size()
         << ",\"level\":" << tit->second.levelIndex
         << "}";
      tcp_.sendLine(c.id, ss.str());
    }
    return;
  }

  if (type == "tournament_register") {
    auto rid = obj.getInt("roomId");
    if (!rid) { sendError(c.id, "tournament_register requires roomId"); return; }
    auto it = tournaments_.find(static_cast<int>(*rid));
    if (it == tournaments_.end()) { sendError(c.id, "not a tournament room"); return; }
    auto& t = it->second;
    if (t.finished) { sendError(c.id, "tournament finished"); return; }
    if (t.started) { sendError(c.id, "tournament already started"); return; }
    // already registered?
    for (const auto& pid : t.registrants) if (pid == sess.playerId) { sendError(c.id, "already registered"); return; }
    std::string err;
    if (!store_.addBalance(sess.playerId, -t.buyin, nowMs, err)) { sendError(c.id, err); return; }
    t.registrants.push_back(sess.playerId);
    t.prizePool += t.buyin;
    tcp_.sendLine(c.id, "{\"type\":\"tournament_registered\"}");
    maybeStartTournament(t.roomId, nowMs);
    return;
  }

  if (type == "tournament_unregister") {
    auto rid = obj.getInt("roomId");
    if (!rid) { sendError(c.id, "tournament_unregister requires roomId"); return; }
    auto it = tournaments_.find(static_cast<int>(*rid));
    if (it == tournaments_.end()) { sendError(c.id, "not a tournament room"); return; }
    auto& t = it->second;
    if (t.started) { sendError(c.id, "tournament already started"); return; }
    auto pos = std::find(t.registrants.begin(), t.registrants.end(), sess.playerId);
    if (pos == t.registrants.end()) { sendError(c.id, "not registered"); return; }
    t.registrants.erase(pos);
    t.prizePool -= t.buyin;
    std::string err;
    (void)store_.addBalance(sess.playerId, t.buyin, nowMs, err);
    tcp_.sendLine(c.id, "{\"type\":\"tournament_unregistered\"}");
    return;
  }

  if (type == "list_tables") {
    auto rid = obj.getInt("roomId");
    int roomId = rid ? static_cast<int>(*rid) : sess.currentRoomId;
    tcp_.sendLine(c.id, serializeTables(roomId));
    return;
  }

  if (type == "observe") {
    auto tid = obj.getInt("tableId");
    auto on = obj.getBool("on");
    if (!tid || !on) { sendError(c.id, "observe requires tableId,on"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    if (*on) tn->observers.insert(sess.playerId);
    else tn->observers.erase(sess.playerId);
    sess.currentTableId = tn->tableId;
    sess.currentRoomId = tn->roomId;
    sendTableToClient(*tn, sess);
    return;
  }

  if (type == "sit") {
    auto tid = obj.getInt("tableId");
    auto seat = obj.getInt("seat");
    auto buyin = obj.getInt("buyin");
    if (!tid || !seat || !buyin) { sendError(c.id, "sit requires tableId,seat,buyin"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    const RoomConfig* r = getRoom(tn->roomId);
    if (!r) { sendError(c.id, "room not found"); return; }
    if (r->type != RoomType::Cash && r->type != RoomType::Private) { sendError(c.id, "sit only for cash/private"); return; }
    if (*buyin < r->buyinMin || *buyin > r->buyinMax) { sendError(c.id, "buyin out of range"); return; }

    std::string err;
    if (!store_.addBalance(sess.playerId, -*buyin, nowMs, err)) { sendError(c.id, err); return; }

    if (!tn->table.seatPlayer(static_cast<int>(*seat), sess.playerId, sess.name, *buyin)) {
      (void)store_.addBalance(sess.playerId, *buyin, nowMs, err); // refund best-effort
      sendError(c.id, "sit failed");
      return;
    }
    sess.currentRoomId = tn->roomId;
    sess.currentTableId = tn->tableId;
    broadcastTable(*tn);
    return;
  }

  if (type == "leave") {
    auto tid = obj.getInt("tableId");
    if (!tid) { sendError(c.id, "leave requires tableId"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    // Refund remaining chips back to balance only if player is seated and not in hand.
    auto snap = tn->table.snapshotFor(sess.playerId);
    int seatIdx = -1;
    for (size_t i = 0; i < snap.seats.size(); ++i) if (snap.seats[i].playerId == sess.playerId) seatIdx = static_cast<int>(i);
    if (seatIdx >= 0 && (snap.seats[seatIdx].status == poker::PlayerStatus::Seated)) {
      std::string e;
      (void)store_.addBalance(sess.playerId, snap.seats[seatIdx].chips, nowMs, e);
    }
    if (!tn->table.leave(sess.playerId)) { sendError(c.id, "leave failed"); return; }
    broadcastTable(*tn);
    return;
  }

  if (type == "ready") {
    auto tid = obj.getInt("tableId");
    auto r = obj.getBool("ready");
    if (!tid || !r) { sendError(c.id, "ready requires tableId,ready"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    if (!tn->table.setReady(sess.playerId, *r)) { sendError(c.id, "ready failed"); return; }
    (void)tn->table.startHandIfReady();
    broadcastTable(*tn);
    return;
  }

  if (type == "auto") {
    auto tid = obj.getInt("tableId");
    auto on = obj.getBool("on");
    if (!tid || !on) { sendError(c.id, "auto requires tableId,on"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    if (!tn->table.setAutoMode(sess.playerId, *on)) { sendError(c.id, "auto failed"); return; }
    broadcastTable(*tn);
    return;
  }

  if (type == "topup") {
    auto tid = obj.getInt("tableId");
    auto amt = obj.getInt("amount");
    if (!tid || !amt) { sendError(c.id, "topup requires tableId,amount"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    const RoomConfig* r = getRoom(tn->roomId);
    if (!r || (r->type != RoomType::Cash && r->type != RoomType::Private)) { sendError(c.id, "topup only for cash/private"); return; }
    std::string err;
    if (!store_.addBalance(sess.playerId, -*amt, nowMs, err)) { sendError(c.id, err); return; }
    if (!tn->table.topUp(sess.playerId, *amt)) {
      (void)store_.addBalance(sess.playerId, *amt, nowMs, err);
      sendError(c.id, "topup failed");
      return;
    }
    broadcastTable(*tn);
    return;
  }

  if (type == "hand_history") {
    auto tid = obj.getInt("tableId");
    if (!tid) { sendError(c.id, "hand_history requires tableId"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    uint64_t hid = tn->table.currentHandId();
    if (auto v = obj.getInt("handId"); v && *v > 0) hid = static_cast<uint64_t>(*v);
    auto events = tn->table.getHandHistory(hid, 500);
    std::ostringstream ss;
    ss << "{\"type\":\"hand_history\",\"roomId\":" << tn->roomId << ",\"tableId\":" << tn->tableId
       << ",\"handId\":" << hid << ",\"events\":[";
    for (size_t i = 0; i < events.size(); ++i) {
      if (i) ss << ",";
      ss << events[i];
    }
    ss << "]}";
    tcp_.sendLine(c.id, ss.str());
    return;
  }

  if (type == "action") {
    auto tid = obj.getInt("tableId");
    auto a = obj.getString("action");
    if (!tid || !a) { sendError(c.id, "action requires tableId,action"); return; }
    auto* tn = getTable(static_cast<int>(*tid));
    if (!tn) { sendError(c.id, "table not found"); return; }
    poker::HoldemTable::Action act;
    act.type = *a;
    if (act.type == "bet" || act.type == "raise") {
      auto amt = obj.getInt("amount");
      if (!amt) { sendError(c.id, "bet/raise requires amount"); return; }
      act.amount = *amt;
    }
    std::string err;
    if (!tn->table.act(sess.playerId, act, err)) { sendError(c.id, err); return; }
    (void)tn->table.startHandIfReady();
    broadcastTable(*tn);
    return;
  }

  if (type == "metrics") {
    const auto& ck = cfgMgr_.config().adminKey;
    if (!ck.empty()) {
      auto k = obj.getString("adminKey");
      if (!k || *k != ck) { sendError(c.id, "admin unauthorized"); return; }
    }
    std::ostringstream ss;
    ss << "{\"type\":\"metrics\""
       << ",\"connectionsTotal\":" << totalConnections_
       << ",\"commandsTotal\":" << totalCommands_
       << ",\"rateLimited\":" << rejectedByRateLimit_
       << ",\"rooms\":" << rooms_.size()
       << ",\"tables\":" << tables_.size()
       << "}";
    tcp_.sendLine(c.id, ss.str());
    return;
  }

  if (type == "admin") {
    const auto& ck = cfgMgr_.config().adminKey;
    if (ck.empty()) { sendError(c.id, "admin disabled"); return; }
    auto k = obj.getString("adminKey");
    if (!k || *k != ck) { sendError(c.id, "admin unauthorized"); return; }
    auto cmd = obj.getString("cmd");
    if (!cmd) { sendError(c.id, "admin requires cmd"); return; }
    if (*cmd == "set_blinds") {
      auto tid = obj.getInt("tableId");
      auto sb = obj.getInt("sb");
      auto bb = obj.getInt("bb");
      if (!tid || !sb || !bb) { sendError(c.id, "set_blinds requires tableId,sb,bb"); return; }
      auto* tn = getTable(static_cast<int>(*tid));
      if (!tn) { sendError(c.id, "table not found"); return; }
      tn->table.setBlinds(*sb, *bb);
      broadcastTable(*tn);
      sendInfo(c.id, "ok");
      return;
    }
    if (*cmd == "create_table") {
      auto rid = obj.getInt("roomId");
      if (!rid) { sendError(c.id, "create_table requires roomId"); return; }
      const RoomConfig* r = getRoom(static_cast<int>(*rid));
      if (!r) { sendError(c.id, "room not found"); return; }
      int tid = nextTableId_++;
      poker::HoldemTable::Config cfg;
      cfg.tableId = tid;
      cfg.maxSeats = r->maxSeats;
      cfg.smallBlind = r->sb;
      cfg.bigBlind = r->bb;
      tables_.emplace(tid, TableNode(tid, r->roomId, cfg));
      tables_.at(tid).table.setActionTimeoutMs(cfgMgr_.config().actionTimeoutMs);
      tcp_.sendLine(c.id, serializeTables(r->roomId));
      return;
    }
    sendError(c.id, "unknown admin cmd");
    return;
  }

  sendError(c.id, "unknown type");
}

int AppServer::run() {
  std::string err;
  (void)cfgMgr_.loadNow(err);
  rl_ = RateLimiter(RateLimiter::Config{cfgMgr_.config().rlCapacity, cfgMgr_.config().rlRefillPerSecond});
  if (!store_.load(err)) {
    // continue in memory
  }
  if (!ensureLobbyCreated(nowSteadyMs(), err)) {
    return 2;
  }
  if (!tcp_.listen(port_, err)) {
    return 2;
  }
  tcp_.run(
      [&](const ClientInfo& c, const std::string& line) { onLine(c, line, nowSteadyMs()); },
      [&](const ClientInfo& c) { onDisconnect(c); },
      [&](int64_t nowMs) { onTick(nowMs); });
  return 0;
}

} // namespace server

