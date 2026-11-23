#include "mp/api.hpp"
#include "mp/serialize.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <vector>

namespace mp {

static std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((unsigned char)c < 0x20) o += " ";
    else o += c;
  }
  return o;
}

static std::map<std::string, std::string> parseQuery(const std::string& q) {
  std::map<std::string, std::string> out;
  size_t p = 0;
  while (p < q.size()) {
    size_t amp = q.find('&', p);
    if (amp == std::string::npos) amp = q.size();
    size_t eq = q.find('=', p);
    if (eq != std::string::npos && eq < amp)
      out[q.substr(p, eq - p)] = q.substr(eq + 1, amp - eq - 1);
    p = amp + 1;
  }
  return out;
}

static double toNum(const std::string& s) {
  return std::strtod(s.c_str(), nullptr);
}

static void splitCSV(const std::string& s, std::vector<std::string>& out) {
  size_t p = 0;
  while (p <= s.size()) {
    size_t c = s.find(',', p);
    if (c == std::string::npos) c = s.size();
    if (c > p) out.push_back(s.substr(p, c - p));
    p = c + 1;
  }
}

bool ApiServer::start(std::string& err) {
  listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0) { err = "socket() failed"; return false; }
  int one = 1;
  ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // ТОЛЬКО loopback (10.x)
  addr.sin_port = htons((unsigned short)port_);
  if (::bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
    err = "bind(127.0.0.1:" + std::to_string(port_) + ") failed";
    ::close(listenFd_); listenFd_ = -1; return false;
  }
  if (::listen(listenFd_, 8) != 0) { err = "listen() failed"; ::close(listenFd_); listenFd_ = -1; return false; }
  running_ = true;
  std::thread([this]() {
    while (running_) {
      sockaddr_in cli{}; socklen_t cl = sizeof(cli);
      int fd = ::accept(listenFd_, (sockaddr*)&cli, &cl);
      if (fd < 0) break;
      serveClient(fd);
      ::close(fd);
    }
  }).detach();
  return true;
}

void ApiServer::stop() {
  running_ = false;
  if (listenFd_ >= 0) { ::shutdown(listenFd_, SHUT_RDWR); ::close(listenFd_); listenFd_ = -1; }
}

void ApiServer::serveClient(int fd) {
  char buf[8192];
  std::string req;
  ssize_t n;
  while (req.find("\r\n\r\n") == std::string::npos && (n = ::recv(fd, buf, sizeof(buf), 0)) > 0)
    req.append(buf, (size_t)n);
  std::istringstream in(req);
  std::string method, uri, proto;
  in >> method >> uri >> proto;
  if (uri.empty()) return;
  size_t qm = uri.find('?');
  std::string path = qm == std::string::npos ? uri : uri.substr(0, qm);
  std::string query = qm == std::string::npos ? "" : uri.substr(qm + 1);

  std::string body = handle(path, query);
  std::string resp =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
      "Connection: close\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\n\r\n" + body;
  ::send(fd, resp.data(), resp.size(), 0);
}

std::string ApiServer::handle(const std::string& path, const std::string& query) {
  auto q = parseQuery(query);

  // --- 10.1 get_neuron?id= ---
  if (path == "/get_neuron") {
    std::string id = q.count("id") ? q["id"] : "";
    std::ostringstream o;
    const Neuron* n = doc_.neuron(id);
    if (!n) return "{\"error\":1,\"msg\":\"no such neuron\"}";
    o << "{\"id\":\"" << jsonEscape(n->id) << "\",\"label\":\"" << jsonEscape(n->label)
      << "\",\"zone\":\"" << jsonEscape(n->zone) << "\"";
    o << ",\"type\":" << (n->type.set ? n->type.v : -1);
    o << ",\"u\":" << n->u << ",\"uThrAdapt\":" << n->uThrAdapt
      << ",\"homeoAdj\":" << n->homeoAdj << ",\"inWeightSum\":" << n->inWeightSum;
    o << ",\"spiked\":" << (n->spiked ? "true" : "false")
      << ",\"spikesPerSec\":0";   // детальная Гц-статистика — в /stream
    o << ",\"disabled\":" << (n->disabled ? "true" : "false")
      << ",\"byCode\":" << (n->byCode ? "true" : "false") << "}";
    return o.str();
  }

  // --- 10.2 set_sensor / set_sensors: только входы, структура не трогается ---
  if (path == "/set_sensor" || path == "/set_sensors") {
    std::vector<std::string> ids;
    if (q.count("id")) ids.push_back(q["id"]);
    if (q.count("ids")) splitCSV(q["ids"], ids);
    if (ids.empty()) return "{\"error\":1,\"msg\":\"no ids\"}";
    if (!q.count("v")) return "{\"error\":1,\"msg\":\"no v\"}";
    std::vector<std::string> vs;
    splitCSV(q["v"], vs);
    for (size_t i = 0; i < ids.size(); ++i) {
      double v = vs.size() == ids.size() ? toNum(vs[i])
               : (vs.size() ? toNum(vs[0]) : 0);
      eng_.setSensorValue(ids[i], v);
    }
    return "{\"ok\":1,\"applied\":" + std::to_string(ids.size()) + "}";
  }


  // --- 10.3 get_schema(export): полный дамп ВСЕХ полей документа (R5) ---
  if (path == "/get_schema" || path == "/export")
    return saveProjectJson(doc_);

  // --- 10.4 stream?ids=A,B&zone=Z&hz=2.5 — текущая активность (NDJSON) ---
  // Фильтры пересекаются: ids по идентификаторам, zone по коду зоны, hz —
  // нижняя граница частоты спайков за время прогона. streaming не мутирует ничего.
  if (path == "/stream") {
    std::vector<std::string> ids;
    if (q.count("ids")) splitCSV(q["ids"], ids);
    std::string zone = q.count("zone") ? q["zone"] : "";
    bool hasHz = q.count("hz");
    double hzMin = hasHz ? toNum(q["hz"]) : -1;
    double secs = std::max(1e-9, eng_.ticks() / 1000.0);
    std::ostringstream o;
    for (const auto& n : doc_.neurons) {
      if (!ids.empty() && std::find(ids.begin(), ids.end(), n.id) == ids.end()) continue;
      if (!zone.empty() && n.zone != zone) continue;
      double hz = (double)n.spikeCount / secs;
      if (hasHz && hz < hzMin) continue;
      o << "{\"id\":\"" << jsonEscape(n.id) << "\",\"zone\":\"" << jsonEscape(n.zone)
        << "\",\"u\":" << n.u << ",\"spiked\":" << (n.spiked ? "true" : "false")
        << ",\"hz\":" << hz << ",\"spikeCount\":" << n.spikeCount
        << ",\"inWeightSum\":" << n.inWeightSum << "}\n";
    }
    return o.str();
  }

  return "{\"error\":1,\"msg\":\"unknown path\"}";
}

} // namespace mp
