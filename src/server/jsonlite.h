#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace jsonlite {

inline std::string trim(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

inline std::string escape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char ch : in) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(ch); break;
    }
  }
  return out;
}

struct Object {
  std::unordered_map<std::string, std::string> kv; // values stored as raw token or unescaped string (if quoted)

  std::optional<std::string> getString(const std::string& k) const {
    auto it = kv.find(k);
    if (it == kv.end()) return std::nullopt;
    return it->second;
  }
  std::optional<int64_t> getInt(const std::string& k) const {
    auto it = kv.find(k);
    if (it == kv.end()) return std::nullopt;
    try { return std::stoll(it->second); } catch (...) { return std::nullopt; }
  }
  std::optional<bool> getBool(const std::string& k) const {
    auto it = kv.find(k);
    if (it == kv.end()) return std::nullopt;
    if (it->second == "true") return true;
    if (it->second == "false") return false;
    return std::nullopt;
  }
};

// Minimal JSON object parser:
// - expects {"k":"v","n":123,"b":true}
// - supports only flat objects with string/number/bool values
inline std::optional<Object> parseObject(const std::string& line) {
  std::string s = trim(line);
  if (s.size() < 2 || s.front() != '{' || s.back() != '}') return std::nullopt;
  Object obj;
  size_t i = 1;
  auto skipWs = [&]() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  };
  auto parseString = [&]() -> std::optional<std::string> {
    if (i >= s.size() || s[i] != '"') return std::nullopt;
    ++i;
    std::string out;
    while (i < s.size()) {
      char ch = s[i++];
      if (ch == '"') return out;
      if (ch == '\\') {
        if (i >= s.size()) return std::nullopt;
        char esc = s[i++];
        switch (esc) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          default: out.push_back(esc); break;
        }
      } else {
        out.push_back(ch);
      }
    }
    return std::nullopt;
  };
  auto parseToken = [&]() -> std::optional<std::string> {
    skipWs();
    if (i >= s.size()) return std::nullopt;
    if (s[i] == '"') {
      auto str = parseString();
      return str;
    }
    // number / true / false
    size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i == start) return std::nullopt;
    return s.substr(start, i - start);
  };

  skipWs();
  if (i < s.size() && s[i] == '}') return obj;

  while (i < s.size()) {
    skipWs();
    auto key = parseString();
    if (!key) return std::nullopt;
    skipWs();
    if (i >= s.size() || s[i] != ':') return std::nullopt;
    ++i;
    auto val = parseToken();
    if (!val) return std::nullopt;
    obj.kv[*key] = *val;
    skipWs();
    if (i >= s.size()) return std::nullopt;
    if (s[i] == ',') { ++i; continue; }
    if (s[i] == '}') { ++i; break; }
    return std::nullopt;
  }
  return obj;
}

} // namespace jsonlite

