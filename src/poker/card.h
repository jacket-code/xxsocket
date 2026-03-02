#pragma once

#include <cstdint>
#include <string>

namespace poker {

enum class Suit : uint8_t { Clubs = 0, Diamonds = 1, Hearts = 2, Spades = 3 };

struct Card {
  // Encoded as 0..51:
  // suit = id / 13 (0..3), rankIndex = id % 13 (0..12), rank = rankIndex + 2 (2..14)
  uint8_t id = 0;

  constexpr Card() = default;
  constexpr explicit Card(uint8_t encoded) : id(encoded) {}

  constexpr Suit suit() const { return static_cast<Suit>(id / 13); }
  constexpr uint8_t rank() const { return static_cast<uint8_t>(id % 13 + 2); } // 2..14

  std::string toString() const;
};

std::string rankToString(uint8_t rank); // 2..14
std::string suitToString(Suit suit);

} // namespace poker

