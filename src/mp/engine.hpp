#pragma once
// mp/engine.hpp — такт симуляции. Формулы зафиксированы (docs/SIMULATION.md), v0.2 их не меняет.
#include "mp/model.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mp {

struct EngineStats {   // HUD 6.6
  long long neurons = 0, synapses = 0, impulses = 0, spikes = 0;
  double spikesPerSec = 0;
  double timeSec = 0;
};

class Engine {
public:
  explicit Engine(Project& doc) : doc_(doc) {}

  // один тик (1 tick = 1 мс, см. docs/SIMULATION.md)
  void tick();

  // ручное воздействие (5.9)
  void fireOnce(const std::string& neuronId);
  void setFlow(const std::string& neuronId, double hz);            // 0 = стоп
  void fillCluster(const std::string& clusterId, double fraction); // доля нейронов кластера

  // сенсорный вход: ручной/API (1.14), нормализация (1.17) и сглаживание (1.18) внутри
  void setSensorValue(const std::string& neuronId, double raw);

  // активный уровень модуляторов (для reward-gated пластичности), 0 если нет
  double modulatorLevel() const;

  EngineStats stats() const;
  long long ticks() const { return tickNow_; } // пройдено тактов (1 tick = 1 мс)
  void resetDyn();

private:
  Project& doc_;
  EngineStats st_{};
  std::set<std::string> manualSpikes_{};             // одиночные спайки (5.9)
  std::map<std::string, double> flowHz_{};           // поток частоты (5.9)
  std::map<std::string, double> sensorRaw_{};        // сырой вход сенсора
  std::map<std::string, double> sensorPrevRaw_{};    // предыдущий сырой вход (event-кодирование)
  std::map<std::string, double> sensorSmooth_{};     // сглаженный вход (1.18)
  // отложенные доставки (2.9)
  struct Pend { long long at; size_t post; double w; };
  std::vector<Pend> pending_{};
  struct ModActive { std::string nid; long long until; double rate; };
  std::vector<ModActive> mods_{};                    // активные окна модуляторов (1.24)
  std::vector<std::pair<long long, int>> spikeHist_{}; // (тик, спайки) — спайки/с для HUD
  long long tickNow_ = 0;
  double plastScale_ = 1.0;                          // множитель пластичности от гомеостата (1.28)
  double rng_ = 0.5;                                 // детерминированный LCG (1.9)

  double gauss();
  double uni();
  void scheduleOutputs();
};

} // namespace mp
