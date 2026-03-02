#include <cstdlib>
#include <iostream>

#include "server/app_server.h"

int main(int argc, char** argv) {
  uint16_t port = 7777;
  std::string dataDir = "data";
  if (argc >= 2) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  if (argc >= 3) {
    dataDir = argv[2];
  }
  std::cerr << "Texas Hold'em server listening on port " << port << "\n";
  std::cerr << "Protocol: one JSON object per line (JSONL)\n";
  server::AppServer s(port, dataDir);
  return s.run();
}

