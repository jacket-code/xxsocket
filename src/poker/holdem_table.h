#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker/card.h"

namespace poker {

enum class Street : uint8_t { Preflop = 0, Flop = 1, Turn = 2, River = 3, Showdown = 4 };

enum class PlayerStatus : uint8_t { Empty = 0, Seated = 1, InHand = 2, Folded = 3, AllIn = 4 };

struct SeatState {
  std::string playerId;
  std::string name;
  int64_t chips = 0;
  bool ready = false;

  PlayerStatus status = PlayerStatus::Empty;
  std::array<Card, 2> hole{};
  bool hasHole = false;

  int64_t betThisStreet = 0;
  int64_t contributed = 0; // total contributed this hand
};

struct LegalActions {
  bool canFold = false;
  bool canCheck = false;
  bool canCall = false;
  bool canBet = false;
  bool canRaise = false;

  int64_t callAmount = 0;   // amount to call (delta)
  int64_t minBetOrRaiseTo = 0; // "to" amount (total bet this street)
  int64_t maxBetOrRaiseTo = 0; // "to" amount (total bet this street), limited by stack
};

struct TableSnapshot {
  int tableId = 0;
  int maxSeats = 0;
  int dealer = -1;
  int sbSeat = -1;
  int bbSeat = -1;
  Street street = Street::Preflop;

  int toAct = -1;
  int64_t pot = 0;
  int64_t currentBet = 0;
  int64_t smallBlind = 0;
  int64_t bigBlind = 0;

  std::vector<Card> board;
  std::vector<SeatState> seats;
  LegalActions legal;
};

class HoldemTable {
public:
  struct Config {
    int tableId = 1;
    int maxSeats = 6;
    int64_t smallBlind = 50;
    int64_t bigBlind = 100;
  };

  explicit HoldemTable(Config cfg);

  const Config& config() const { return cfg_; }
  TableSnapshot snapshotFor(const std::string& viewerPlayerId) const;

  bool seatPlayer(int seatIdx, std::string playerId, std::string name, int64_t chips);
  bool setReady(const std::string& playerId, bool ready);
  bool leave(const std::string& playerId);

  bool startHandIfReady();

  struct Action {
    std::string type;   // "fold","check","call","bet","raise"
    int64_t amount = 0; // for bet/raise: "to" total amount bet this street
  };
  bool act(const std::string& playerId, const Action& action, std::string& err);

  int64_t pot() const { return pot_; }

private:
  Config cfg_;
  std::vector<SeatState> seats_;

  // hand state
  Street street_ = Street::Preflop;
  int dealer_ = -1;
  int sbSeat_ = -1;
  int bbSeat_ = -1;
  int toAct_ = -1;

  int64_t pot_ = 0;
  int64_t currentBet_ = 0;      // highest betThisStreet in current street
  int64_t minRaiseSize_ = 0;    // min raise increment
  int64_t lastAggressiveTo_ = 0; // last aggressive total-to on this street

  std::vector<Card> board_;
  std::vector<Card> deck_;
  size_t deckPos_ = 0;

  std::vector<bool> needAction_;

  std::mt19937_64 rng_;

  int activePlayersInHand() const;
  int seatedPlayersReady() const;
  bool isHandRunning() const;

  void resetForNewHand();
  void shuffleDeck();
  Card draw();

  int nextParticipatingSeat(int start) const; // InHand/AllIn, with chips>0 at hand start
  int nextSeatedWithChipsSeat(int start) const; // any non-empty with chips>0
  int nextInHandCanActSeat(int start) const;
  int findSeatByPlayerId(const std::string& playerId) const;
  bool postBlind(int seatIdx, int64_t amount);
  void startBettingRound(Street st, int firstToAct);
  void advanceStreetOrShowdown();
  void settleShowdown();

  LegalActions legalForSeat(int seatIdx) const;
  bool applyBetTo(int seatIdx, int64_t betTo, std::string& err);
};

} // namespace poker

