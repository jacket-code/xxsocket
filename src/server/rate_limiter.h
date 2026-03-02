#pragma once

#include <cstdint>
#include <unordered_map>

namespace server {

// Simple token bucket per client.
class RateLimiter {
public:
  struct Config {
    int64_t capacity = 30;        // max tokens
    int64_t refillPerSecond = 20; // tokens per second
  };

  explicit RateLimiter(Config cfg) : cfg_(cfg) {}

  // Returns true if allowed.
  bool allow(int64_t key, int64_t nowMs, int64_t cost = 1) {
    auto& b = buckets_[key];
    if (b.lastMs == 0) {
      b.tokens = cfg_.capacity;
      b.lastMs = nowMs;
    }
    if (nowMs > b.lastMs) {
      const int64_t elapsedMs = nowMs - b.lastMs;
      const int64_t add = (elapsedMs * cfg_.refillPerSecond) / 1000;
      if (add > 0) {
        b.tokens = (b.tokens + add > cfg_.capacity) ? cfg_.capacity : (b.tokens + add);
        b.lastMs = nowMs;
      }
    }
    if (cost <= 0) cost = 1;
    if (b.tokens < cost) return false;
    b.tokens -= cost;
    return true;
  }

  void forget(int64_t key) { buckets_.erase(key); }

private:
  struct Bucket { int64_t tokens = 0; int64_t lastMs = 0; };
  Config cfg_;
  std::unordered_map<int64_t, Bucket> buckets_;
};

} // namespace server

