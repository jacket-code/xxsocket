#include "poker/holdem_table.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>

#include "poker/hand_eval.h"

namespace poker {
namespace {

static inline int64_t clampNonNeg(int64_t x) { return x < 0 ? 0 : x; }

} // namespace

HoldemTable::HoldemTable(Config cfg) : cfg_(cfg) {
  seats_.resize(cfg_.maxSeats);
  needAction_.resize(cfg_.maxSeats, false);
  rng_.seed(static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

void HoldemTable::setBlinds(int64_t smallBlind, int64_t bigBlind) {
  if (smallBlind <= 0 || bigBlind <= 0 || smallBlind >= bigBlind) return;
  cfg_.smallBlind = smallBlind;
  cfg_.bigBlind = bigBlind;
  if (minRaiseSize_ <= 0) minRaiseSize_ = cfg_.bigBlind;
}

void HoldemTable::setActionTimeoutMs(int64_t ms) {
  if (ms < 1000) ms = 1000;
  actionTimeoutMs_ = ms;
}

int HoldemTable::findSeatByPlayerId(const std::string& playerId) const {
  for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
    if (seats_[i].status != PlayerStatus::Empty && seats_[i].playerId == playerId) return i;
  }
  return -1;
}

bool HoldemTable::seatPlayer(int seatIdx, std::string playerId, std::string name, int64_t chips) {
  if (seatIdx < 0 || seatIdx >= static_cast<int>(seats_.size())) return false;
  if (chips <= 0) return false;
  if (findSeatByPlayerId(playerId) != -1) return false;
  auto& s = seats_[seatIdx];
  if (s.status != PlayerStatus::Empty) return false;
  s.playerId = std::move(playerId);
  s.name = std::move(name);
  s.chips = chips;
  s.ready = false;
  s.autoMode = false;
  s.status = PlayerStatus::Seated;
  return true;
}

bool HoldemTable::setReady(const std::string& playerId, bool ready) {
  int idx = findSeatByPlayerId(playerId);
  if (idx < 0) return false;
  if (isHandRunning()) return false;
  if (seats_[idx].status == PlayerStatus::Empty) return false;
  seats_[idx].ready = ready;
  return true;
}

bool HoldemTable::setAutoMode(const std::string& playerId, bool autoMode) {
  int idx = findSeatByPlayerId(playerId);
  if (idx < 0) return false;
  if (seats_[idx].status == PlayerStatus::Empty) return false;
  seats_[idx].autoMode = autoMode;
  return true;
}

bool HoldemTable::topUp(const std::string& playerId, int64_t amount) {
  if (amount <= 0) return false;
  int idx = findSeatByPlayerId(playerId);
  if (idx < 0) return false;
  if (seats_[idx].status == PlayerStatus::Empty) return false;
  if (isHandRunning() && (seats_[idx].status == PlayerStatus::InHand || seats_[idx].status == PlayerStatus::AllIn)) {
    return false;
  }
  seats_[idx].chips += amount;
  return true;
}

bool HoldemTable::leave(const std::string& playerId) {
  int idx = findSeatByPlayerId(playerId);
  if (idx < 0) return false;
  if (isHandRunning() && seats_[idx].status != PlayerStatus::Seated) return false;
  seats_[idx] = SeatState{};
  return true;
}

int HoldemTable::seatedPlayersReady() const {
  int n = 0;
  for (const auto& s : seats_) {
    if (s.status != PlayerStatus::Empty && s.ready && s.chips > 0) ++n;
  }
  return n;
}

int HoldemTable::activePlayersInHand() const {
  int n = 0;
  for (const auto& s : seats_) {
    if (s.status == PlayerStatus::InHand || s.status == PlayerStatus::AllIn) ++n;
  }
  return n;
}

bool HoldemTable::isHandRunning() const {
  for (const auto& s : seats_) {
    if (s.status == PlayerStatus::InHand || s.status == PlayerStatus::Folded || s.status == PlayerStatus::AllIn) return true;
  }
  return false;
}

void HoldemTable::shuffleDeck() {
  deck_.clear();
  deck_.reserve(52);
  for (int i = 0; i < 52; ++i) deck_.push_back(Card(static_cast<uint8_t>(i)));
  std::shuffle(deck_.begin(), deck_.end(), rng_);
  deckPos_ = 0;
}

Card HoldemTable::draw() {
  if (deckPos_ >= deck_.size()) return Card(0);
  return deck_[deckPos_++];
}

int HoldemTable::nextSeatedWithChipsSeat(int start) const {
  const int n = static_cast<int>(seats_.size());
  for (int d = 1; d <= n; ++d) {
    int i = (start + d) % n;
    if (seats_[i].status != PlayerStatus::Empty && seats_[i].chips > 0) return i;
  }
  return -1;
}

int HoldemTable::nextParticipatingSeat(int start) const {
  const int n = static_cast<int>(seats_.size());
  for (int d = 1; d <= n; ++d) {
    int i = (start + d) % n;
    const auto st = seats_[i].status;
    if ((st == PlayerStatus::InHand || st == PlayerStatus::AllIn) && seats_[i].chips >= 0) return i;
  }
  return -1;
}

int HoldemTable::nextInHandCanActSeat(int start) const {
  const int n = static_cast<int>(seats_.size());
  for (int d = 1; d <= n; ++d) {
    int i = (start + d) % n;
    const auto st = seats_[i].status;
    if ((st == PlayerStatus::InHand) && needAction_[i]) return i;
  }
  return -1;
}

bool HoldemTable::postBlind(int seatIdx, int64_t amount) {
  if (seatIdx < 0) return false;
  auto& s = seats_[seatIdx];
  if (s.status != PlayerStatus::InHand) return false;
  const int64_t pay = std::min<int64_t>(amount, s.chips);
  s.chips -= pay;
  s.betThisStreet += pay;
  s.contributed += pay;
  pot_ += pay;
  if (s.chips == 0) s.status = PlayerStatus::AllIn;
  return true;
}

void HoldemTable::resetForNewHand() {
  street_ = Street::Preflop;
  sbSeat_ = -1;
  bbSeat_ = -1;
  toAct_ = -1;
  actionDeadlineMs_ = 0;
  pot_ = 0;
  currentBet_ = 0;
  minRaiseSize_ = cfg_.bigBlind;
  lastAggressiveTo_ = 0;
  board_.clear();
  shuffleDeck();
  curEvents_.clear();
  handId_++;
  logEvent(std::string("{\"type\":\"hand_start\",\"handId\":") + std::to_string(handId_) + "}");

  for (auto& s : seats_) {
    if (s.status == PlayerStatus::Empty) continue;
    s.betThisStreet = 0;
    s.contributed = 0;
    s.hasHole = false;
    if (s.ready && s.chips > 0) {
      s.status = PlayerStatus::InHand;
    } else {
      s.status = PlayerStatus::Seated;
    }
  }
}

bool HoldemTable::startHandIfReady() {
  if (isHandRunning()) return false;
  if (seatedPlayersReady() < 2) return false;

  resetForNewHand();

  // Move dealer button to next occupied seat
  if (dealer_ < 0) {
    dealer_ = nextParticipatingSeat(0);
  } else {
    dealer_ = nextParticipatingSeat(dealer_);
  }

  sbSeat_ = nextParticipatingSeat(dealer_);
  bbSeat_ = nextParticipatingSeat(sbSeat_);

  // Deal hole cards to in-hand players
  for (int r = 0; r < 2; ++r) {
    for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
      int seat = (dealer_ + 1 + i) % static_cast<int>(seats_.size());
      if (seats_[seat].status == PlayerStatus::InHand) {
        seats_[seat].hole[r] = draw();
        seats_[seat].hasHole = true;
      }
    }
  }
  logEvent(std::string("{\"type\":\"deal_hole\",\"handId\":") + std::to_string(handId_) + "}");

  // Post blinds
  if (sbSeat_ >= 0) postBlind(sbSeat_, cfg_.smallBlind);
  if (bbSeat_ >= 0) postBlind(bbSeat_, cfg_.bigBlind);
  logEvent(std::string("{\"type\":\"post_blinds\",\"handId\":") + std::to_string(handId_) +
           ",\"sbSeat\":" + std::to_string(sbSeat_) +
           ",\"bbSeat\":" + std::to_string(bbSeat_) +
           ",\"sb\":" + std::to_string(cfg_.smallBlind) +
           ",\"bb\":" + std::to_string(cfg_.bigBlind) + "}");

  currentBet_ = cfg_.bigBlind;
  lastAggressiveTo_ = currentBet_;
  minRaiseSize_ = cfg_.bigBlind;

  // Preflop first to act: seat after BB (or SB in heads-up)
  int first = nextParticipatingSeat(bbSeat_);
  startBettingRound(Street::Preflop, first);
  return true;
}

void HoldemTable::startBettingRound(Street st, int firstToAct) {
  street_ = st;
  for (int i = 0; i < static_cast<int>(needAction_.size()); ++i) {
    needAction_[i] = (seats_[i].status == PlayerStatus::InHand);
  }
  // players who are already all-in cannot act
  for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
    if (seats_[i].status == PlayerStatus::AllIn) needAction_[i] = false;
  }

  toAct_ = firstToAct;
  // If first seat can't act, find next.
  if (toAct_ >= 0 && !needAction_[toAct_]) {
    toAct_ = nextInHandCanActSeat(toAct_);
  }
  actionDeadlineMs_ = 0;
}

LegalActions HoldemTable::legalForSeat(int seatIdx) const {
  LegalActions la{};
  if (seatIdx < 0 || seatIdx >= static_cast<int>(seats_.size())) return la;
  const auto& s = seats_[seatIdx];
  if (s.status != PlayerStatus::InHand) return la;

  la.canFold = true;
  const int64_t toCall = clampNonNeg(currentBet_ - s.betThisStreet);
  la.callAmount = toCall;
  la.canCheck = (toCall == 0);
  la.canCall = (toCall > 0 && s.chips > 0);

  const int64_t maxTo = s.betThisStreet + s.chips;
  la.maxBetOrRaiseTo = maxTo;

  if (currentBet_ == 0) {
    la.canBet = (s.chips > 0);
    la.minBetOrRaiseTo = std::min<int64_t>(maxTo, cfg_.bigBlind);
  } else {
    la.canRaise = (s.chips > toCall);
    const int64_t minTo = currentBet_ + minRaiseSize_;
    la.minBetOrRaiseTo = std::min<int64_t>(maxTo, minTo);
  }

  if (la.minBetOrRaiseTo > la.maxBetOrRaiseTo) {
    la.canBet = false;
    la.canRaise = false;
  }
  return la;
}

bool HoldemTable::applyBetTo(int seatIdx, int64_t betTo, std::string& err) {
  auto& s = seats_[seatIdx];
  if (betTo < 0) { err = "invalid amount"; return false; }
  const int64_t maxTo = s.betThisStreet + s.chips;
  if (betTo > maxTo) betTo = maxTo; // allow all-in cap
  if (betTo < s.betThisStreet) { err = "amount below current"; return false; }
  const int64_t delta = betTo - s.betThisStreet;
  if (delta == 0) return true;

  s.chips -= delta;
  s.betThisStreet += delta;
  s.contributed += delta;
  pot_ += delta;
  if (s.chips == 0) {
    s.status = PlayerStatus::AllIn;
    needAction_[seatIdx] = false;
  }
  return true;
}

bool HoldemTable::act(const std::string& playerId, const Action& action, std::string& err) {
  if (!isHandRunning()) { err = "no hand running"; return false; }
  int seatIdx = findSeatByPlayerId(playerId);
  if (seatIdx < 0) { err = "not seated"; return false; }
  if (seatIdx != toAct_) { err = "not your turn"; return false; }
  if (seats_[seatIdx].status != PlayerStatus::InHand) { err = "cannot act"; return false; }

  auto la = legalForSeat(seatIdx);
  const std::string t = action.type;
  {
    std::ostringstream ss;
    ss << "{\"type\":\"player_action\",\"handId\":" << handId_
       << ",\"seat\":" << seatIdx
       << ",\"playerId\":\"" << seats_[seatIdx].playerId << "\""
       << ",\"action\":\"" << t << "\"";
    if (t == "bet" || t == "raise") ss << ",\"amount\":" << action.amount;
    ss << "}";
    logEvent(ss.str());
  }

  if (t == "fold") {
    if (!la.canFold) { err = "fold not allowed"; return false; }
    seats_[seatIdx].status = PlayerStatus::Folded;
    needAction_[seatIdx] = false;
  } else if (t == "check") {
    if (!la.canCheck) { err = "check not allowed"; return false; }
    needAction_[seatIdx] = false;
  } else if (t == "call") {
    if (!la.canCall) { err = "call not allowed"; return false; }
    const int64_t toCall = seats_[seatIdx].betThisStreet + la.callAmount;
    if (!applyBetTo(seatIdx, toCall, err)) return false;
    needAction_[seatIdx] = false;
  } else if (t == "bet") {
    if (!la.canBet) { err = "bet not allowed"; return false; }
    int64_t betTo = action.amount;
    if (betTo < la.minBetOrRaiseTo) { err = "bet too small"; return false; }
    if (!applyBetTo(seatIdx, betTo, err)) return false;
    currentBet_ = std::max(currentBet_, seats_[seatIdx].betThisStreet);
    lastAggressiveTo_ = currentBet_;
    minRaiseSize_ = cfg_.bigBlind;
    // everyone else needs action again (except all-in/folded)
    for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
      if (i == seatIdx) continue;
      if (seats_[i].status == PlayerStatus::InHand) needAction_[i] = true;
    }
    needAction_[seatIdx] = false;
  } else if (t == "raise") {
    if (!la.canRaise) { err = "raise not allowed"; return false; }
    int64_t raiseTo = action.amount;
    if (raiseTo < la.minBetOrRaiseTo) { err = "raise too small"; return false; }
    const int64_t oldBet = seats_[seatIdx].betThisStreet;
    if (!applyBetTo(seatIdx, raiseTo, err)) return false;
    const int64_t newBet = seats_[seatIdx].betThisStreet;
    const int64_t raiseSize = newBet - currentBet_;
    currentBet_ = std::max(currentBet_, newBet);
    if (raiseSize > 0) {
      minRaiseSize_ = std::max<int64_t>(minRaiseSize_, raiseSize);
      lastAggressiveTo_ = currentBet_;
      for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
        if (i == seatIdx) continue;
        if (seats_[i].status == PlayerStatus::InHand) needAction_[i] = true;
      }
    } else {
      // should not happen normally
      (void)oldBet;
    }
    needAction_[seatIdx] = false;
  } else {
    err = "unknown action";
    return false;
  }

  // If only one player remains (not folded), award pot and end hand.
  int remaining = 0;
  int last = -1;
  for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
    if (seats_[i].status == PlayerStatus::InHand || seats_[i].status == PlayerStatus::AllIn) {
      remaining++;
      last = i;
    }
  }
  if (remaining == 1 && last >= 0) {
    seats_[last].chips += pot_;
    pot_ = 0;
    logEvent(std::string("{\"type\":\"hand_end\",\"handId\":") + std::to_string(handId_) + ",\"reason\":\"all_folded\"}");
    history_.push_back(HandLog{handId_, curEvents_});
    if (history_.size() > 200) history_.pop_front();
    curEvents_.clear();
    // end hand
    for (auto& s : seats_) {
      if (s.status != PlayerStatus::Empty) {
        s.status = PlayerStatus::Seated;
        s.betThisStreet = 0;
        s.contributed = 0;
        s.hasHole = false;
      }
    }
    toAct_ = -1;
    actionDeadlineMs_ = 0;
    board_.clear();
    return true;
  }

  // Find next actor; if no one needs action, advance street.
  int next = nextInHandCanActSeat(toAct_);
  if (next < 0) {
    advanceStreetOrShowdown();
  } else {
    toAct_ = next;
    actionDeadlineMs_ = 0;
  }
  return true;
}

void HoldemTable::advanceStreetOrShowdown() {
  // Move chips from betThisStreet into "currentBet_/needAction" state and deal next street.
  for (auto& s : seats_) {
    s.betThisStreet = 0;
  }
  currentBet_ = 0;
  minRaiseSize_ = cfg_.bigBlind;
  lastAggressiveTo_ = 0;

  // If all remaining players are all-in, fast-forward to river then showdown.
  auto anyCanAct = [&]() {
    for (const auto& s : seats_) if (s.status == PlayerStatus::InHand) return true;
    return false;
  };

  auto dealFlop = [&]() {
    (void)draw(); // burn
    board_.push_back(draw());
    board_.push_back(draw());
    board_.push_back(draw());
    logEvent(std::string("{\"type\":\"deal_flop\",\"handId\":") + std::to_string(handId_) + "}");
  };
  auto dealOne = [&]() {
    (void)draw(); // burn
    board_.push_back(draw());
    logEvent(std::string("{\"type\":\"deal_card\",\"handId\":") + std::to_string(handId_) + "}");
  };

  if (street_ == Street::Preflop) {
    dealFlop();
    street_ = Street::Flop;
  } else if (street_ == Street::Flop) {
    dealOne();
    street_ = Street::Turn;
  } else if (street_ == Street::Turn) {
    dealOne();
    street_ = Street::River;
  } else if (street_ == Street::River) {
    street_ = Street::Showdown;
  }

  if (street_ == Street::Showdown) {
    settleShowdown();
    return;
  }

  // Determine first to act postflop: seat after dealer (small blind acts first)
  int first = nextParticipatingSeat(dealer_);
  if (!anyCanAct()) {
    // no one can act -> fast-forward
    if (street_ != Street::River) {
      advanceStreetOrShowdown();
    } else {
      street_ = Street::Showdown;
      settleShowdown();
    }
    return;
  }
  startBettingRound(street_, first);
}

void HoldemTable::settleShowdown() {
  // Make sure board has 5 (fast-forward dealing if needed)
  while (board_.size() < 5) {
    (void)draw(); // burn
    board_.push_back(draw());
  }

  // Build side pots from total contributions (including folded players)
  struct Cont { int seat; int64_t amt; bool eligible; };
  std::vector<Cont> cont;
  cont.reserve(seats_.size());
  for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
    if (seats_[i].status == PlayerStatus::Empty) continue;
    if (seats_[i].contributed <= 0) continue;
    bool eligible = (seats_[i].status == PlayerStatus::InHand || seats_[i].status == PlayerStatus::AllIn);
    cont.push_back({i, seats_[i].contributed, eligible});
  }
  if (cont.empty()) return;

  // Sort by contribution ascending for pot levels
  std::sort(cont.begin(), cont.end(), [](const Cont& a, const Cont& b) { return a.amt < b.amt; });

  struct SidePot { int64_t size; std::vector<int> eligibleSeats; };
  std::vector<SidePot> pots;

  int64_t prev = 0;
  for (size_t idx = 0; idx < cont.size(); ++idx) {
    int64_t level = cont[idx].amt;
    if (level == prev) continue;
    int64_t slice = level - prev;
    int64_t contributors = 0;
    std::vector<int> eligibleSeats;
    for (const auto& c : cont) {
      if (c.amt >= level) {
        contributors++;
        if (c.eligible) eligibleSeats.push_back(c.seat);
      }
    }
    if (contributors > 0) {
      pots.push_back(SidePot{slice * contributors, eligibleSeats});
    }
    prev = level;
  }

  // Evaluate hands for eligible seats
  std::unordered_map<int, HandValue> hv;
  for (int i = 0; i < static_cast<int>(seats_.size()); ++i) {
    if (!(seats_[i].status == PlayerStatus::InHand || seats_[i].status == PlayerStatus::AllIn)) continue;
    if (!seats_[i].hasHole) continue;
    std::array<Card, 7> cards{};
    cards[0] = seats_[i].hole[0];
    cards[1] = seats_[i].hole[1];
    for (int b = 0; b < 5; ++b) cards[2 + b] = board_[b];
    hv[i] = eval7(cards);
  }

  // Distribute each side pot to best eligible hand(s)
  int64_t remainingPot = pot_;
  for (auto& p : pots) {
    if (p.size <= 0) continue;
    if (p.eligibleSeats.empty()) continue; // all eligible folded? shouldn't happen

    HandValue best = 0;
    for (int seat : p.eligibleSeats) {
      auto it = hv.find(seat);
      if (it != hv.end()) best = std::max(best, it->second);
    }
    std::vector<int> winners;
    for (int seat : p.eligibleSeats) {
      auto it = hv.find(seat);
      if (it != hv.end() && it->second == best) winners.push_back(seat);
    }
    if (winners.empty()) continue;
    const int64_t share = p.size / static_cast<int64_t>(winners.size());
    int64_t extra = p.size - share * static_cast<int64_t>(winners.size());
    for (int seat : winners) {
      seats_[seat].chips += share;
      if (extra > 0) { seats_[seat].chips += 1; extra--; }
    }
    remainingPot -= p.size;
  }
  // Any rounding residue (or pot not captured by computed slices) goes to first eligible from dealer+1
  if (remainingPot > 0) {
    int seat = nextParticipatingSeat(dealer_);
    for (int tries = 0; tries < static_cast<int>(seats_.size()); ++tries) {
      if (seat < 0) break;
      if (seats_[seat].status == PlayerStatus::InHand || seats_[seat].status == PlayerStatus::AllIn) {
        seats_[seat].chips += remainingPot;
        remainingPot = 0;
        break;
      }
      seat = nextParticipatingSeat(seat);
    }
  }
  pot_ = 0;

  logEvent(std::string("{\"type\":\"hand_end\",\"handId\":") + std::to_string(handId_) + ",\"reason\":\"showdown\"}");
  history_.push_back(HandLog{handId_, curEvents_});
  if (history_.size() > 200) history_.pop_front();
  curEvents_.clear();

  // End hand: reset statuses to seated and clear ready flags.
  for (auto& s : seats_) {
    if (s.status == PlayerStatus::Empty) continue;
    s.status = PlayerStatus::Seated;
    s.betThisStreet = 0;
    s.contributed = 0;
    s.hasHole = false;
  }
  toAct_ = -1;
  actionDeadlineMs_ = 0;
}

void HoldemTable::setToAct(int seatIdx, int64_t nowMs) {
  toAct_ = seatIdx;
  if (seatIdx < 0 || nowMs <= 0) {
    actionDeadlineMs_ = 0;
    return;
  }
  actionDeadlineMs_ = nowMs + actionTimeoutMs_;
}

void HoldemTable::logEvent(std::string ev) {
  curEvents_.push_back(std::move(ev));
}

std::vector<std::string> HoldemTable::getHandHistory(uint64_t handId, int limit) const {
  if (limit <= 0) return {};
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    if (it->handId == handId) {
      if (static_cast<int>(it->events.size()) <= limit) return it->events;
      return std::vector<std::string>(it->events.end() - limit, it->events.end());
    }
  }
  return {};
}

void HoldemTable::tick(int64_t nowMs) {
  if (toAct_ < 0) return;
  if (actionTimeoutMs_ <= 0) return;
  if (actionDeadlineMs_ == 0) {
    setToAct(toAct_, nowMs);
    return;
  }
  if (nowMs <= 0 || nowMs < actionDeadlineMs_) return;

  // Timeout policy: check if possible, else fold.
  if (toAct_ < 0 || toAct_ >= static_cast<int>(seats_.size())) return;
  auto& s = seats_[toAct_];
  if (s.status != PlayerStatus::InHand) return;
  auto la = legalForSeat(toAct_);
  Action a;
  a.type = la.canCheck ? "check" : "fold";
  std::string err;
  (void)act(s.playerId, a, err);

  if (toAct_ >= 0) setToAct(toAct_, nowMs);
}

TableSnapshot HoldemTable::snapshotFor(const std::string& viewerPlayerId) const {
  TableSnapshot snap;
  snap.tableId = cfg_.tableId;
  snap.maxSeats = cfg_.maxSeats;
  snap.dealer = dealer_;
  snap.sbSeat = sbSeat_;
  snap.bbSeat = bbSeat_;
  snap.street = street_;
  snap.handId = handId_;
  snap.toAct = toAct_;
  snap.actionDeadlineMs = actionDeadlineMs_;
  snap.pot = pot_;
  snap.currentBet = currentBet_;
  snap.smallBlind = cfg_.smallBlind;
  snap.bigBlind = cfg_.bigBlind;
  snap.board = board_;
  snap.seats = seats_;

  // Hide other players' hole cards unless viewer is that player (or hand ended, but we reset at end)
  for (auto& s : snap.seats) {
    if (s.status == PlayerStatus::Empty) continue;
    if (s.playerId != viewerPlayerId) {
      s.hasHole = false;
    }
  }

  if (toAct_ >= 0 && toAct_ < static_cast<int>(seats_.size()) && seats_[toAct_].playerId == viewerPlayerId) {
    snap.legal = legalForSeat(toAct_);
  }
  return snap;
}

} // namespace poker

