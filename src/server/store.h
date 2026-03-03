#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace server {

struct Account {
  std::string playerId;
  std::string name;
  int64_t balance = 0;
  int64_t createdAtMs = 0;
  int64_t updatedAtMs = 0;
};

// Minimal persistence using JSONL with flat objects (parseable by jsonlite).
class Store {
public:
  explicit Store(std::string dataDir);

  bool load(std::string& err);
  bool flush(std::string& err) const;

  std::optional<Account> getAccount(const std::string& playerId) const;
  Account upsertAccount(const Account& a);

  bool addBalance(const std::string& playerId, int64_t delta, int64_t nowMs, std::string& err);

private:
  std::string dir_;
  std::unordered_map<std::string, Account> accounts_;

  static std::string accountsPath(const std::string& dir);
};

} // namespace server

