#include "server/tcp_server.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace server {

TcpServer::Client* TcpServer::findClient(int64_t clientId) {
  for (auto& c : clients_) if (c.id == clientId) return &c;
  return nullptr;
}

void TcpServer::dropClient(size_t idx) {
  if (idx >= clients_.size()) return;
  clients_[idx].sock.close();
  clients_.erase(clients_.begin() + static_cast<long>(idx));
}

bool TcpServer::listen(uint16_t port, std::string& err) {
  if (!listenSock_.open(AF_INET, SOCK_STREAM, 0)) {
    err = "socket open failed";
    return false;
  }
  int opt = 1;
  (void)listenSock_.set_optval(SOL_SOCKET, SO_REUSEADDR, opt);
  if (listenSock_.bind(nullptr, port) != 0) {
    err = std::string("bind failed: ") + purelib::net::xxsocket::get_error_msg(errno);
    return false;
  }
  if (listenSock_.listen(128) != 0) {
    err = std::string("listen failed: ") + purelib::net::xxsocket::get_error_msg(errno);
    return false;
  }
  (void)listenSock_.set_nonblocking(true);
  return true;
}

bool TcpServer::sendLine(int64_t clientId, const std::string& line) {
  Client* c = findClient(clientId);
  if (!c) return false;
  std::string out = line;
  if (out.empty() || out.back() != '\n') out.push_back('\n');
  int n = c->sock.send_i(out.data(), static_cast<int>(out.size()), 0);
  return n == static_cast<int>(out.size());
}

void TcpServer::broadcastLine(const std::vector<int64_t>& clientIds, const std::string& line) {
  for (auto id : clientIds) (void)sendLine(id, line);
}

void TcpServer::run(const LineHandler& onLine, const DisconnectHandler& onDisconnect) {
  while (true) {
    fd_set rfds;
    FD_ZERO(&rfds);
    auto listenFd = listenSock_.native_handle();
    FD_SET(listenFd, &rfds);
    purelib::net::socket_native_type maxfd = listenFd;
    for (const auto& c : clients_) {
      auto fd = c.sock.native_handle();
      FD_SET(fd, &rfds);
      if (fd > maxfd) maxfd = fd;
    }

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200 * 1000; // 200ms
    int rc = ::select(static_cast<int>(maxfd + 1), &rfds, nullptr, nullptr, &tv);
    if (rc < 0) {
      // keep running on EINTR
      if (errno == EINTR) continue;
      continue;
    }

    if (FD_ISSET(listenFd, &rfds)) {
      for (;;) {
        auto s = listenSock_.accept();
        if (!s.is_open()) break;
        (void)s.set_nonblocking(true);
        Client c;
        c.id = nextClientId_++;
        c.sock = std::move(s);
        clients_.push_back(std::move(c));
      }
    }

    // Read from clients
    for (size_t idx = 0; idx < clients_.size();) {
      auto fd = clients_[idx].sock.native_handle();
      if (!FD_ISSET(fd, &rfds)) { ++idx; continue; }

      char buf[4096];
      int n = clients_[idx].sock.recv_i(buf, sizeof(buf), 0);
      if (n <= 0) {
        ClientInfo info{clients_[idx].id, fd};
        if (onDisconnect) onDisconnect(info);
        dropClient(idx);
        continue;
      }

      clients_[idx].inbuf.append(buf, n);
      for (;;) {
        auto pos = clients_[idx].inbuf.find('\n');
        if (pos == std::string::npos) break;
        std::string line = clients_[idx].inbuf.substr(0, pos);
        clients_[idx].inbuf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        ClientInfo info{clients_[idx].id, fd};
        if (onLine) onLine(info, line);
      }
      ++idx;
    }
  }
}

} // namespace server

