#pragma once

#include <array>
#include <cstdint>

#include "poker/card.h"

namespace poker {

// Hand strength encoding (higher is better):
// [ category (8 bits) | primary kickers (remaining bits) ]
// category: 8=straight flush, 7=four, 6=full house, 5=flush, 4=straight, 3=trips, 2=two pair, 1=pair, 0=high card
using HandValue = uint64_t;

HandValue eval5(const std::array<Card, 5>& cards);
HandValue eval7(const std::array<Card, 7>& cards);

} // namespace poker

