#pragma once
// mp/api.hpp — loopback HTTP API (10.1–10.5).
//  10.1 get_neuron?id=   — полное состояние нейрона (параметры + динамика)
//  10.2 set_sensor?id=&v= и set_sensors?ids=a,b&v=x[,y]  — только входы сенсоров
//  10.3 get_schema(export)  — JSON дамп ВСЕХ полей документа (7.4/R5)
//  10.4 stream с фильтрами ids=/zone=/hz=  — текущая активность (NDJSON-объект)
//  10.5 API НИКОГДА не мутирует структуру: никаких добавлений/удалений юнитов,
//      никаких правок параметров. Доступны только входы сенсоров и чтение.
// Сервер слушает ТОЛЬКО 127.0.0.1.
#include "mp/engine.hpp"
#include "mp/model.hpp"

#include <atomic>
#include <string>

namespace mp {

class ApiServer {
public:
  ApiServer(Project& doc, Engine& eng, int port = 18080) : doc_(doc), eng_(eng), port_(port) {}

  bool start(std::string& err);   // привязка на 127.0.0.1:port; поток accept
  void stop();
  int  port() const { return port_; }
  bool running() const { return running_.load(); }

private:
  Project& doc_;
  Engine& eng_;
  int port_;
  int listenFd_ = -1;
  std::atomic<bool> running_{false};

  void serveClient(int fd);
  std::string handle(const std::string& path, const std::string& query);
};

} // namespace mp
