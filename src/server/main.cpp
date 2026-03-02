#include <cstdlib>
#include <iostream>

#include "server/protocol.h"

int main(int argc, char** argv) {
  uint16_t port = 7777;
  if (argc >= 2) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  std::cerr << "Texas Hold'em server listening on port " << port << "\n";
  std::cerr << "Protocol: one JSON object per line (JSONL)\n";
  server::GameServer s(port);
  return s.run();
}

