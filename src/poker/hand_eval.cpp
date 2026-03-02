#include "poker/hand_eval.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace poker {
namespace {

struct RankCounts {
  std::array<uint8_t, 15> c{}; // 0..14, use 2..14
};

static inline uint64_t packKickers(const std::array<uint8_t, 5>& ks) {
  // ks sorted desc; pack into 4-bit nibbles
  uint64_t v = 0;
  for (uint8_t r : ks) v = (v << 4) | (r & 0xF);
  return v;
}

static inline bool isFlush(const std::array<Card, 5>& cards) {
  const auto s0 = cards[0].suit();
  for (int i = 1; i < 5; ++i) {
    if (cards[i].suit() != s0) return false;
  }
  return true;
}

static inline uint8_t straightHighFromRanks(std::array<uint8_t, 5> ranksDesc) {
  // ranksDesc sorted desc, but may include duplicates (shouldn't for 5-card straight test after uniq)
  // return 0 if not straight; otherwise high card of straight (5 for wheel A-5).
  std::sort(ranksDesc.begin(), ranksDesc.end(), std::greater<uint8_t>());
  // unique in-place; if duplicates exist, it's not a straight in 5 cards.
  for (int i = 1; i < 5; ++i) {
    if (ranksDesc[i] == ranksDesc[i - 1]) return 0;
  }

  // Wheel: A,5,4,3,2
  if (ranksDesc[0] == 14 && ranksDesc[1] == 5 && ranksDesc[2] == 4 && ranksDesc[3] == 3 && ranksDesc[4] == 2) {
    return 5;
  }
  for (int i = 1; i < 5; ++i) {
    if (ranksDesc[i - 1] != static_cast<uint8_t>(ranksDesc[i] + 1)) return 0;
  }
  return ranksDesc[0];
}

static inline HandValue makeValue(uint8_t category, const std::array<uint8_t, 5>& ks) {
  return (static_cast<uint64_t>(category) << 56) | packKickers(ks);
}

} // namespace

HandValue eval5(const std::array<Card, 5>& cardsIn) {
  std::array<Card, 5> cards = cardsIn;
  std::array<uint8_t, 5> ranks{};
  for (int i = 0; i < 5; ++i) ranks[i] = cards[i].rank();

  const bool flush = isFlush(cards);
  const uint8_t straightHigh = straightHighFromRanks(ranks);

  RankCounts rc{};
  for (uint8_t r : ranks) rc.c[r]++;

  // Collect groups: (count, rank) sorted by count desc, rank desc
  std::array<std::pair<uint8_t, uint8_t>, 5> groups{};
  int gN = 0;
  for (uint8_t r = 2; r <= 14; ++r) {
    if (rc.c[r]) groups[gN++] = {rc.c[r], r};
  }
  std::sort(groups.begin(), groups.begin() + gN, [](auto a, auto b) {
    if (a.first != b.first) return a.first > b.first;
    return a.second > b.second;
  });

  if (straightHigh && flush) {
    return makeValue(8, {straightHigh, 0, 0, 0, 0});
  }

  // Four of a kind
  if (groups[0].first == 4) {
    uint8_t quad = groups[0].second;
    uint8_t kicker = 0;
    for (int i = 1; i < gN; ++i) if (groups[i].first == 1) kicker = groups[i].second;
    return makeValue(7, {quad, kicker, 0, 0, 0});
  }

  // Full house
  if (groups[0].first == 3 && groups[1].first == 2) {
    return makeValue(6, {groups[0].second, groups[1].second, 0, 0, 0});
  }

  if (flush) {
    std::sort(ranks.begin(), ranks.end(), std::greater<uint8_t>());
    return makeValue(5, ranks);
  }

  if (straightHigh) {
    return makeValue(4, {straightHigh, 0, 0, 0, 0});
  }

  // Trips
  if (groups[0].first == 3) {
    uint8_t trip = groups[0].second;
    std::array<uint8_t, 5> ks{trip, 0, 0, 0, 0};
    int idx = 1;
    // remaining kickers desc
    std::array<uint8_t, 2> kick{};
    int kN = 0;
    for (int i = 1; i < gN; ++i) if (groups[i].first == 1) kick[kN++] = groups[i].second;
    std::sort(kick.begin(), kick.begin() + kN, std::greater<uint8_t>());
    for (int i = 0; i < kN; ++i) ks[idx++] = kick[i];
    return makeValue(3, ks);
  }

  // Two pair
  if (groups[0].first == 2 && groups[1].first == 2) {
    uint8_t hiPair = std::max(groups[0].second, groups[1].second);
    uint8_t loPair = std::min(groups[0].second, groups[1].second);
    uint8_t kicker = 0;
    for (int i = 2; i < gN; ++i) if (groups[i].first == 1) kicker = std::max(kicker, groups[i].second);
    return makeValue(2, {hiPair, loPair, kicker, 0, 0});
  }

  // One pair
  if (groups[0].first == 2) {
    uint8_t pair = groups[0].second;
    std::array<uint8_t, 5> ks{pair, 0, 0, 0, 0};
    std::array<uint8_t, 3> kick{};
    int kN = 0;
    for (int i = 1; i < gN; ++i) if (groups[i].first == 1) kick[kN++] = groups[i].second;
    std::sort(kick.begin(), kick.begin() + kN, std::greater<uint8_t>());
    for (int i = 0; i < kN; ++i) ks[i + 1] = kick[i];
    return makeValue(1, ks);
  }

  // High card
  std::sort(ranks.begin(), ranks.end(), std::greater<uint8_t>());
  return makeValue(0, ranks);
}

HandValue eval7(const std::array<Card, 7>& cards) {
  HandValue best = 0;
  // choose 5 out of 7: 21 combos
  for (int a = 0; a < 3; ++a) {
    for (int b = a + 1; b < 4; ++b) {
      for (int c = b + 1; c < 5; ++c) {
        for (int d = c + 1; d < 6; ++d) {
          for (int e = d + 1; e < 7; ++e) {
            std::array<Card, 5> five{cards[a], cards[b], cards[c], cards[d], cards[e]};
            best = std::max(best, eval5(five));
          }
        }
      }
    }
  }
  return best;
}

} // namespace poker

