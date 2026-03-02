#include <cassert>
#include <cstdint>
#include <iostream>

#include "poker/hand_eval.h"
#include "poker/holdem_table.h"

using poker::Card;

static void testHandEvalBasics() {
  // Royal flush in spades: As Ks Qs Js Ts
  std::array<Card, 5> rf{Card(3 * 13 + 12), Card(3 * 13 + 11), Card(3 * 13 + 10), Card(3 * 13 + 9), Card(3 * 13 + 8)};
  auto v1 = poker::eval5(rf);

  // Four of a kind: AAAA2
  std::array<Card, 5> quads{Card(0 * 13 + 12), Card(1 * 13 + 12), Card(2 * 13 + 12), Card(3 * 13 + 12), Card(0 * 13 + 0)};
  auto v2 = poker::eval5(quads);

  assert(v1 > v2);

  // 7-card: should detect best among 7
  std::array<Card, 7> seven{
      Card(3 * 13 + 12), Card(3 * 13 + 11), // As Ks
      Card(3 * 13 + 10), Card(3 * 13 + 9),  // Qs Js
      Card(3 * 13 + 8),  // Ts
      Card(0 * 13 + 0),  // 2c
      Card(1 * 13 + 5)   // 7d
  };
  auto v7 = poker::eval7(seven);
  assert(v7 == v1);
}

static void testTableConservation() {
  poker::HoldemTable t(poker::HoldemTable::Config{1, 2, 1, 2});
  const int64_t a0 = 100;
  const int64_t b0 = 100;
  assert(t.seatPlayer(0, "A", "Alice", a0));
  assert(t.seatPlayer(1, "B", "Bob", b0));

  auto playOneHand = [&]() {
    assert(t.setReady("A", true));
    assert(t.setReady("B", true));
    assert(t.startHandIfReady());

    for (int guard = 0; guard < 5000; ++guard) {
      auto sA = t.snapshotFor("A");
      auto sB = t.snapshotFor("B");
      if (sA.toAct < 0 && sB.toAct < 0) break;

      std::string pid;
      if (sA.toAct >= 0 && sA.toAct < 2 && sA.seats[sA.toAct].playerId == "A") pid = "A";
      else if (sB.toAct >= 0 && sB.toAct < 2 && sB.seats[sB.toAct].playerId == "B") pid = "B";
      else {
        // If viewer isn't the actor (shouldn't happen), just break to avoid infinite loop.
        break;
      }

      auto snap = t.snapshotFor(pid);
      poker::HoldemTable::Action act;
      if (snap.legal.canCheck) act.type = "check";
      else if (snap.legal.canCall) act.type = "call";
      else act.type = "fold";

      std::string err;
      bool ok = t.act(pid, act, err);
      assert(ok);
    }

    assert(t.pot() == 0);
  };

  int64_t total0 = a0 + b0;
  for (int i = 0; i < 20; ++i) {
    playOneHand();
    auto s = t.snapshotFor("A");
    int64_t total = 0;
    for (const auto& seat : s.seats) total += seat.chips;
    assert(total == total0);
  }
}

int main() {
  testHandEvalBasics();
  testTableConservation();
  std::cout << "OK\n";
  return 0;
}

