#include "mp/stands.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <sstream>

namespace mp {

// FNV-1a для хешей стендов
static uint64_t fnv64(uint64_t h, const void* data, size_t n) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
  return h;
}

static std::string trajectoryHash(const Project& doc, long long ticks,
                                  const std::vector<long long>& spikesPerTick) {
  uint64_t h = 1469598103934665603ULL;
  h = fnv64(h, &ticks, sizeof(ticks));
  for (const auto& n : doc.neurons) {
    uint8_t b = n.spiked ? 1 : 0;
    h = fnv64(h, &b, 1);
  }
  for (long long s : spikesPerTick) h = fnv64(h, &s, sizeof(s));
  std::ostringstream o;
  o << std::hex << h;
  return o.str();
}

Stand* findStand(Project& doc, const std::string& name) {
  for (auto& s : doc.stands) if (s.name == name) return &s;
  return nullptr;
}
const Stand* findStand(const Project& doc, const std::string& name) {
  for (const auto& s : doc.stands) if (s.name == name) return &s;
  return nullptr;
}

Stand& createStand(Project& doc, const std::string& name, double durationSec) {
  Stand s;
  s.name = name;
  s.durationSec = durationSec;
  doc.stands.push_back(s);
  doc.dirty = true;
  return doc.stands.back();
}

bool removeStand(Project& doc, const std::string& name) {
  auto it = std::remove_if(doc.stands.begin(), doc.stands.end(),
                           [&](const Stand& s) { return s.name == name; });
  bool removed = it != doc.stands.end();
  doc.stands.erase(it, doc.stands.end());
  doc.dirty = doc.dirty || removed;
  return removed;
}

void recordEvent(Project& doc, const std::string& standName, const StandEvent& ev) {
  for (auto& s : doc.stands)
    if (s.name == standName) { s.events.push_back(ev); doc.dirty = true; return; }
  Stand& s = createStand(doc, standName);
  s.events.push_back(ev);
}

void registerRecordedStand(Project& doc, const std::string& standName, double durationSec) {
  Stand* s = findStand(doc, standName);
  if (!s) return;
  s->durationSec = durationSec;
  s->schemaHash = schemaHash(doc);                         // 9.3
  double dur = std::max(1e-6, s->durationSec);
  Engine eng(doc);                                         // эталонный прогон
  long long ticks = (long long)std::llround(dur * doc.tickHz);
  std::vector<long long> perTick; perTick.reserve(ticks);
  long long t = 0, prevSpikes = 0;
  for (const StandEvent& ev : s->events) {
    long long atTick = (long long)std::llround(ev.t * doc.tickHz);
    while (t < atTick && t < ticks) {
      eng.tick(); perTick.push_back(eng.stats().spikes - prevSpikes);
      prevSpikes = eng.stats().spikes; ++t;
    }
    eng.setSensorValue(ev.neuronId, ev.v);                 // api и мышь действуют одинаково
  }
  while (t < ticks) {
    eng.tick(); perTick.push_back(eng.stats().spikes - prevSpikes);
    prevSpikes = eng.stats().spikes; ++t;
  }
  long long total = 0; for (long long x : perTick) total += x;
  s->recSpikesTotal = total;
  s->recSpikesPerSec = total / dur;
  s->resultHash = trajectoryHash(doc, ticks, perTick);
  doc.dirty = true;
}

StandRunResult runStand(const Project& doc, const Stand& stand, double speed) {
  StandRunResult r;
  r.name = stand.name;
  r.hashMismatch = (stand.schemaHash != schemaHash(doc));   // 9.3: предупреждение
  if (!stand.schemaHash.empty() && r.hashMismatch) return r; // схема изменилась — прогон требует подтверждения

  Project sim;
  deepCopy(doc, sim);                                       // R2/R5: прогон на копии документа
  Engine eng(sim);
  long long ticks = (long long)std::llround(std::max(0.0, stand.durationSec) * speed * doc.tickHz);
  ticks = std::min<long long>(ticks, 10'000'000);
  std::vector<long long> perTick; perTick.reserve((size_t)std::min<long long>(ticks, 1000000));
  std::vector<const StandEvent*> evs;
  for (const auto& e : stand.events) evs.push_back(&e);
  std::sort(evs.begin(), evs.end(),
            [](const StandEvent* a, const StandEvent* b) { return a->t < b->t; });
  size_t ei = 0; r.replayOk = true; long long prevSpikes = 0;
  for (long long t = 0; t < ticks; ++t) {
    double sec = t / doc.tickHz;
    while (ei < evs.size() && evs[ei]->t <= sec) {
      const StandEvent* ev = evs[ei++];
      Neuron* n = sim.neuron(ev->neuronId);
      if (!n) { r.replayOk = false; continue; }             // 9.2: честный отчёт о невоспроизведённых событиях
      eng.setSensorValue(ev->neuronId, ev->v);
    }
    eng.tick();
    if (perTick.size() < 1000000) {
      perTick.push_back(eng.stats().spikes - prevSpikes);
      prevSpikes = eng.stats().spikes;
    }
  }
  for (long long x : perTick) r.spikesTotal += x;
  double dur = std::max(1e-6, std::max(0.0, stand.durationSec) * speed);
  r.spikesPerSec = r.spikesTotal / dur;
  r.resultHash = trajectoryHash(sim, ticks, perTick);   // траектория КОПИИ, не исходного doc
  r.regressionOk = stand.resultHash.empty() ? false : (r.resultHash == stand.resultHash);
  return r;
}

std::vector<StandRunResult> runRegression(const Project& doc) {   // 9.4
  std::vector<StandRunResult> out;
  for (const auto& s : doc.stands) out.push_back(runStand(doc, s));
  return out;
}

} // namespace mp
