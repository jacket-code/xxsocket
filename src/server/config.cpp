#include "server/config.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "server/jsonlite.h"

namespace server {
namespace {

static int64_t statMtimeMs(const std::string& path) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1000 + st.st_mtimespec.tv_nsec / 1000000;
#else
  return static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
}

static std::optional<std::string> readAll(const std::string& path, std::string& err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    err = std::string("open failed: ") + std::strerror(errno);
    return std::nullopt;
  }
  std::string out;
  char buf[4096];
  for (;;) {
    size_t n = std::fread(buf, 1, sizeof(buf), f);
    if (n > 0) out.append(buf, n);
    if (n < sizeof(buf)) break;
  }
  std::fclose(f);
  return out;
}

} // namespace

ConfigManager::ConfigManager(std::string path) : path_(std::move(path)) {}

bool ConfigManager::loadNow(std::string& err) {
  err.clear();
  if (path_.empty()) return true;
  auto contentOpt = readAll(path_, err);
  if (!contentOpt) {
    // missing config is ok: use defaults
    return true;
  }
  auto objOpt = jsonlite::parseObject(*contentOpt);
  if (!objOpt) {
    err = "config parse failed (expects one flat JSON object)";
    return false;
  }
  const auto& obj = *objOpt;
  if (auto v = obj.getInt("defaultBalance")) cfg_.defaultBalance = *v;
  if (auto v = obj.getInt("actionTimeoutMs")) cfg_.actionTimeoutMs = *v;
  if (auto v = obj.getInt("storeFlushMs")) cfg_.storeFlushMs = *v;
  if (auto v = obj.getString("adminKey")) cfg_.adminKey = *v;
  if (auto v = obj.getInt("rlCapacity")) cfg_.rlCapacity = *v;
  if (auto v = obj.getInt("rlRefillPerSecond")) cfg_.rlRefillPerSecond = *v;

  lastMtimeMs_ = statMtimeMs(path_);
  return true;
}

bool ConfigManager::tickReload(int64_t nowMs, std::string& err) {
  err.clear();
  if (path_.empty()) return true;
  if (nowMs - lastCheckMs_ < 1000) return true;
  lastCheckMs_ = nowMs;

  int64_t mt = statMtimeMs(path_);
  if (mt == 0 || mt == lastMtimeMs_) return true;
  return loadNow(err);
}

} // namespace server

