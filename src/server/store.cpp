#include "server/store.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "server/jsonlite.h"

namespace server {
namespace {

static std::string q(const std::string& s) { return "\"" + jsonlite::escape(s) + "\""; }

} // namespace

Store::Store(std::string dataDir) : dir_(std::move(dataDir)) {}

std::string Store::accountsPath(const std::string& dir) { return dir + "/accounts.jsonl"; }

bool Store::load(std::string& err) {
  accounts_.clear();
  const std::string path = accountsPath(dir_);
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    // ok if missing
    return true;
  }
  char* line = nullptr;
  size_t cap = 0;
  for (;;) {
    ssize_t n = getline(&line, &cap, f);
    if (n <= 0) break;
    std::string s(line, static_cast<size_t>(n));
    if (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    }
    if (s.empty()) continue;
    auto objOpt = jsonlite::parseObject(s);
    if (!objOpt) continue;
    const auto& obj = *objOpt;
    auto pid = obj.getString("playerId");
    if (!pid || pid->empty()) continue;
    Account a;
    a.playerId = *pid;
    if (auto v = obj.getString("name")) a.name = *v;
    if (auto v = obj.getInt("balance")) a.balance = *v;
    if (auto v = obj.getInt("createdAtMs")) a.createdAtMs = *v;
    if (auto v = obj.getInt("updatedAtMs")) a.updatedAtMs = *v;
    accounts_[a.playerId] = a;
  }
  if (line) free(line);
  std::fclose(f);
  return true;
}

bool Store::flush(std::string& err) const {
  // Ensure directory exists
  std::string mkdirCmd = "mkdir -p " + dir_;
  int rc = std::system(mkdirCmd.c_str());
  (void)rc;

  const std::string path = accountsPath(dir_);
  const std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) {
    err = std::string("open failed: ") + std::strerror(errno);
    return false;
  }
  for (const auto& kv : accounts_) {
    const auto& a = kv.second;
    std::ostringstream ss;
    ss << "{"
       << "\"playerId\":" << q(a.playerId)
       << ",\"name\":" << q(a.name)
       << ",\"balance\":" << a.balance
       << ",\"createdAtMs\":" << a.createdAtMs
       << ",\"updatedAtMs\":" << a.updatedAtMs
       << "}\n";
    auto str = ss.str();
    if (std::fwrite(str.data(), 1, str.size(), f) != str.size()) {
      err = "write failed";
      std::fclose(f);
      return false;
    }
  }
  std::fclose(f);
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    err = std::string("rename failed: ") + std::strerror(errno);
    return false;
  }
  return true;
}

std::optional<Account> Store::getAccount(const std::string& playerId) const {
  auto it = accounts_.find(playerId);
  if (it == accounts_.end()) return std::nullopt;
  return it->second;
}

Account Store::upsertAccount(const Account& a) {
  accounts_[a.playerId] = a;
  return a;
}

bool Store::addBalance(const std::string& playerId, int64_t delta, int64_t nowMs, std::string& err) {
  auto it = accounts_.find(playerId);
  if (it == accounts_.end()) {
    err = "account not found";
    return false;
  }
  if (delta < 0 && it->second.balance + delta < 0) {
    err = "insufficient balance";
    return false;
  }
  it->second.balance += delta;
  it->second.updatedAtMs = nowMs;
  return true;
}

} // namespace server

