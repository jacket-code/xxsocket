#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace server {

struct ServerConfig {
  int64_t defaultBalance = 100000;
  int64_t actionTimeoutMs = 15000;
  int64_t storeFlushMs = 5000;
  std::string adminKey; // empty => admin disabled

  int64_t rlCapacity = 60;
  int64_t rlRefillPerSecond = 40;
};

class ConfigManager {
public:
  explicit ConfigManager(std::string path);

  bool loadNow(std::string& err);
  bool tickReload(int64_t nowMs, std::string& err);

  const ServerConfig& config() const { return cfg_; }

private:
  std::string path_;
  ServerConfig cfg_;
  int64_t lastMtimeMs_ = 0;
  int64_t lastCheckMs_ = 0;
};

} // namespace server

