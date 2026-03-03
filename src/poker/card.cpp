#include "poker/card.h"

namespace poker {

std::string rankToString(uint8_t rank) {
  if (rank >= 2 && rank <= 9) return std::string(1, static_cast<char>('0' + rank));
  switch (rank) {
    case 10: return "T";
    case 11: return "J";
    case 12: return "Q";
    case 13: return "K";
    case 14: return "A";
    default: return "?";
  }
}

std::string suitToString(Suit suit) {
  switch (suit) {
    case Suit::Clubs: return "c";
    case Suit::Diamonds: return "d";
    case Suit::Hearts: return "h";
    case Suit::Spades: return "s";
    default: return "?";
  }
}

std::string Card::toString() const {
  return rankToString(rank()) + suitToString(suit());
}

} // namespace poker

