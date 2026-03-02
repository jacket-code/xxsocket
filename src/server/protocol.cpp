#include "server/protocol.h"

#include <chrono>
#include <random>
#include <sstream>

#include "server/jsonlite.h"

namespace server {
namespace {

static std::string q(const std::string& s) { return "\"" + jsonlite::escape(s) + "\""; }

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

GameServer::GameServer(uint16_t port)
    : port_(port),
      table_(poker::HoldemTable::Config{1, 6, 50, 100}) {}

std::string GameServer::newPlayerId() const {
  static thread_local std::mt19937_64 rng(
      static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  std::uniform_int_distribution<uint64_t> dist;
  uint64_t a = dist(rng);
  uint64_t b = dist(rng);
  std::ostringstream ss;
  ss << std::hex;
  ss << a << b;
  return ss.str();
}

void GameServer::sendError(int64_t clientId, const std::string& msg) {
  tcp_.sendLine(clientId, std::string("{\"type\":\"error\",\"message\":") + q(msg) + "}");
}

void GameServer::sendInfo(int64_t clientId, const std::string& msg) {
  tcp_.sendLine(clientId, std::string("{\"type\":\"info\",\"message\":") + q(msg) + "}");
}

void GameServer::sendWelcome(int64_t clientId, const ClientSession& sess) {
  std::ostringstream ss;
  ss << "{\"type\":\"welcome\",\"playerId\":" << q(sess.playerId)
     << ",\"name\":" << q(sess.name)
     << ",\"table\":{\"id\":1,\"maxSeats\":6,\"sb\":50,\"bb\":100}}";
  tcp_.sendLine(clientId, ss.str());
  tcp_.sendLine(clientId, serializeSnapshotFor(sess.playerId));
}

std::string GameServer::serializeSnapshotFor(const std::string& viewerPlayerId) const {
  auto snap = table_.snapshotFor(viewerPlayerId);
  std::ostringstream ss;
  ss << "{\"type\":\"table_state\""
     << ",\"tableId\":" << snap.tableId
     << ",\"street\":" << q(streetToString(snap.street))
     << ",\"dealer\":" << snap.dealer
     << ",\"sbSeat\":" << snap.sbSeat
     << ",\"bbSeat\":" << snap.bbSeat
     << ",\"toAct\":" << snap.toAct
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
       << ",\"bet\":" << s.betThisStreet
       << ",\"contributed\":" << s.contributed;
    if (s.hasHole) {
      ss << ",\"hole\":[" << q(s.hole[0].toString()) << "," << q(s.hole[1].toString()) << "]";
    }
    ss << "}";
  }
  ss << "]";

  // If viewer is toAct, include legal actions
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

void GameServer::broadcastTableState() {
  std::vector<int64_t> clients;
  clients.reserve(byClient_.size());
  for (const auto& kv : byClient_) {
    if (!kv.second.playerId.empty()) clients.push_back(kv.first);
  }
  for (const auto& kv : byClient_) {
    const auto& sess = kv.second;
    if (sess.playerId.empty()) continue;
    tcp_.sendLine(sess.clientId, serializeSnapshotFor(sess.playerId));
  }
}

void GameServer::onDisconnect(const ClientInfo& c) {
  auto it = byClient_.find(c.id);
  if (it == byClient_.end()) return;
  if (!it->second.playerId.empty()) {
    clientByPlayerId_.erase(it->second.playerId);
  }
  byClient_.erase(it);
}

void GameServer::onLine(const ClientInfo& c, const std::string& line) {
  auto& sess = byClient_[c.id];
  sess.clientId = c.id;

  auto objOpt = jsonlite::parseObject(line);
  if (!objOpt) {
    sendError(c.id, "invalid json object");
    return;
  }
  const auto& obj = *objOpt;
  auto typeOpt = obj.getString("type");
  if (!typeOpt) {
    sendError(c.id, "missing type");
    return;
  }
  const std::string type = *typeOpt;

  if (type == "hello") {
    auto nameOpt = obj.getString("name");
    if (!nameOpt || nameOpt->empty()) {
      sendError(c.id, "missing name");
      return;
    }
    std::string playerId;
    if (auto pid = obj.getString("playerId"); pid && !pid->empty()) {
      playerId = *pid;
      auto old = clientByPlayerId_.find(playerId);
      if (old != clientByPlayerId_.end() && old->second != c.id) {
        // kick old session mapping; seat stays as playerId in engine
        byClient_.erase(old->second);
        clientByPlayerId_.erase(old);
      }
    } else {
      playerId = newPlayerId();
    }
    sess.playerId = playerId;
    sess.name = *nameOpt;
    clientByPlayerId_[playerId] = c.id;
    sendWelcome(c.id, sess);
    broadcastTableState();
    return;
  }

  if (sess.playerId.empty()) {
    sendError(c.id, "please hello first");
    return;
  }

  if (type == "sit") {
    auto seat = obj.getInt("seat");
    auto chips = obj.getInt("chips");
    if (!seat || !chips) {
      sendError(c.id, "sit requires seat,chips");
      return;
    }
    bool ok = table_.seatPlayer(static_cast<int>(*seat), sess.playerId, sess.name, *chips);
    if (!ok) sendError(c.id, "sit failed");
    broadcastTableState();
    return;
  }

  if (type == "ready") {
    auto r = obj.getBool("ready");
    if (!r) { sendError(c.id, "ready requires ready:true/false"); return; }
    if (!table_.setReady(sess.playerId, *r)) {
      sendError(c.id, "ready failed (maybe hand running or not seated)");
      return;
    }
    (void)table_.startHandIfReady();
    broadcastTableState();
    return;
  }

  if (type == "action") {
    auto a = obj.getString("action");
    if (!a) { sendError(c.id, "action requires action"); return; }
    poker::HoldemTable::Action act;
    act.type = *a;
    if (act.type == "bet" || act.type == "raise") {
      auto amt = obj.getInt("amount");
      if (!amt) { sendError(c.id, "bet/raise requires amount (to)"); return; }
      act.amount = *amt;
    }
    std::string err;
    if (!table_.act(sess.playerId, act, err)) {
      sendError(c.id, err);
      return;
    }
    (void)table_.startHandIfReady(); // auto-start next hand if everyone still ready (we clear ready on end)
    broadcastTableState();
    return;
  }

  if (type == "leave") {
    if (!table_.leave(sess.playerId)) sendError(c.id, "leave failed");
    broadcastTableState();
    return;
  }

  sendError(c.id, "unknown type");
}

int GameServer::run() {
  std::string err;
  if (!tcp_.listen(port_, err)) {
    return 2;
  }
  tcp_.run(
      [&](const ClientInfo& c, const std::string& line) { onLine(c, line); },
      [&](const ClientInfo& c) { onDisconnect(c); });
  return 0;
}

} // namespace server

