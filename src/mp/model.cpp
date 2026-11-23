#include "mp/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mp {

const char* ntypeName(NType t) {
  switch (t) {
    case NType::Sensor: return "sensor";
    case NType::Hidden: return "hidden";
    case NType::Motor: return "motor";
    case NType::Modulator: return "modulator";
    case NType::Homeostat: return "homeostat";
  }
  return "?";
}
const char* plasticityName(Plasticity p) {
  switch (p) {
    case Plasticity::Rigid: return "rigid";
    case Plasticity::Hebb: return "hebb";
    case Plasticity::RewardGated: return "reward";
    case Plasticity::AntiHebb: return "antihebb";
  }
  return "?";
}

// ------------------------------------------------------------------
// R1: обязательные параметры по типам. Всё, что Opt и не set → красный.
// ------------------------------------------------------------------
bool Neuron::missingParam() const {
  if (!type.set) return true;
  NType t = (NType)type.v;
  if (!thr.set || !leak.set || !refrMax.set || !noise.set || !trace.set) return true;
  if (homeoOn && !homeoRate.set) return true;
  switch (t) {
    case NType::Sensor:
      if (!src.set || !enc.set || !fireThr.set || !hyst.set || !normMin.set ||
          !normMax.set || !smoothMs.set) return true;
      break;
    case NType::Motor:
      if (!act.set || !mapMin.set || !mapMax.set || !mapSpeed.set || !curve.set ||
          ((Curve)curve.v == Curve::Gamma && !gamma.set)) return true;
      break;
    case NType::Modulator:
      if (!mkind.set || !mscope.set || !windowSec.set || !rate.set) return true;
      if ((ModScope)mscope.v == ModScope::Zones && mzonelist.empty()) return true;
      break;
    case NType::Homeostat:
      if (!target.set || !integSec.set || !kThr.set || !kPlast.set) return true;
      break;
    case NType::Hidden:
      break;
  }
  return false;
}

bool Synapse::missingParam() const {
  return !weight.set || !sign.set || !plast.set || !lr.set || !pw.set || !wMin.set ||
         !wMax.set || !decay.set || !delay.set;
}

double Synapse::effW() const { return w * ((sign.set && (SSign)sign.v == SSign::Inh) ? -1.0 : 1.0); }

bool Cluster::missingParam() const {
  return !shape.set || !radius.set || !count.set || !topo.set ||
         !thrVar.set || !leakVar.set || !wVar.set || !seed.set ||
         !neuroProb.set || !pruneProb.set ||
         ((Topo)topo.v == Topo::KNN && !knnK.set);
}

// ------------------------------------------------------------------
// поиск/индексы
// ------------------------------------------------------------------
Neuron* Project::neuron(const std::string& id) {
  for (auto& n : neurons) if (n.id == id) return &n;
  return nullptr;
}
const Neuron* Project::neuron(const std::string& id) const {
  for (const auto& n : neurons) if (n.id == id) return &n;
  return nullptr;
}
Synapse* Project::synapse(const std::string& id) {
  for (auto& s : synapses) if (s.id == id) return &s;
  return nullptr;
}
Zone* Project::zone(const std::string& code) {
  for (auto& z : zones) if (z.code == code) return &z;
  return nullptr;
}
Cluster* Project::cluster(const std::string& id) {
  for (auto& c : clusters) if (c.id == id) return &c;
  return nullptr;
}

// ------------------------------------------------------------------
// ID (1.1): канонический "кодзоны.тип.ннн"; счётчики правятся только здесь
// ------------------------------------------------------------------
char Project::typeCode(NType t) {
  switch (t) {
    case NType::Sensor: return 'S';
    case NType::Hidden: return 'H';
    case NType::Motor: return 'M';
    case NType::Modulator: return 'D';
    case NType::Homeostat: return 'G';
  }
  return 'H';
}

std::string Project::nextNeuronId(const std::string& zoneCode, NType t, const std::string& labelHint) {
  std::string key = zoneCode + std::string(1, '.') + std::string(1, typeCode(t));
  (void)labelHint; // метка — отдельное поле Neuron::label (1.1); в ID всегда номер
  long long c = idCounter[key] + 1;
  std::string id = key + "." + std::to_string(c);
  // подбор свободного номера (счётчик остаётся монотонным)
  while (neuron(id)) {
    idCounter[key] = c;
    ++c;
    id = key + "." + std::to_string(c);
  }
  idCounter[key] = c;
  return id;
}

static bool trailingNumber(const std::string& id, std::string* keyOut, long long* numOut) {
  size_t p = id.find_last_of('.');
  if (p == std::string::npos || p + 1 >= id.size()) return false;
  for (size_t i = p + 1; i < id.size(); ++i)
    if (id[i] < '0' || id[i] > '9') return false;
  if (keyOut) *keyOut = id.substr(0, p);
  if (numOut) *numOut = std::atoll(id.c_str() + p + 1);
  return true;
}

std::string Project::nextSynId() { return "syn." + std::to_string(++synCounter); }
std::string Project::nextClusterId() { return "clust." + std::to_string(++clustCounter); }

void Project::recomputeIdCounters() {
  // R4/R5: после load/удаления счётчики гарантрованно ≥ MAX существующих номеров.
  // Монотонно-максимальный пересчёт делаем только по запросу загрузки;
  // при удалении счётчики НЕ уменьшаем (ID не переиспользуются).
  idCounter.clear();
  for (const auto& n : neurons) {
    std::string key; long long num;
    if (!trailingNumber(n.id, &key, &num)) continue;
    long long& c = idCounter[key];
    if (num > c) c = num;
  }
  synCounter = 0;
  for (const auto& s : synapses)
    if (s.id.rfind("syn.", 0) == 0) {
      long long num = std::atoll(s.id.c_str() + 4);
      if (num > synCounter) synCounter = num;
    }
  clustCounter = 0;
  for (const auto& c : clusters)
    if (c.id.rfind("clust.", 0) == 0) {
      long long num = std::atoll(c.id.c_str() + 6);
      if (num > clustCounter) clustCounter = num;
    }
}

// ------------------------------------------------------------------
// мутации (R4)
// ------------------------------------------------------------------
Neuron* Project::addNeuron(Neuron n) {
  if (n.id.empty()) {
    std::string zoneCode = n.zone;
    NType t = n.type.set ? (NType)n.type.v : NType::Hidden;
    n.id = nextNeuronId(zoneCode, t, "");
  }
  if (n.label.empty()) {
    char cb[2] = {typeCode(n.type.set ? (NType)n.type.v : NType::Hidden), 0};
    n.label = std::string(cb) + n.id.substr(n.id.find_last_of('.') + 1);
  }
  neurons.push_back(std::move(n));
  dirty = true;
  return &neurons.back();
}

bool Project::removeNeuron(const std::string& id) {
  // R4/K1: связи на нейрон удаляются целиком; из кластера убирается членство;
  // id2n (динамический индекс) чистится за счёт удаления самого вектора; счётчики не откатываются.
  bool found = neuron(id) != nullptr;
  if (!found) return false;
  synapses.erase(std::remove_if(synapses.begin(), synapses.end(),
                                [&](const Synapse& s) { return s.pre == id || s.post == id; }),
                 synapses.end());
  for (auto& c : clusters) {
    c.members.erase(std::remove(c.members.begin(), c.members.end(), id), c.members.end());
  }
  neurons.erase(std::remove_if(neurons.begin(), neurons.end(),
                               [&](const Neuron& n) { return n.id == id; }),
                neurons.end());
  dirty = true;
  return true;
}

bool Project::removeSynapse(const std::string& id) {
  auto it = std::remove_if(synapses.begin(), synapses.end(),
                           [&](const Synapse& s) { return s.id == id; });
  if (it == synapses.end()) return false;
  synapses.erase(it, synapses.end());
  dirty = true;
  return true;
}

bool Project::removeZone(const std::string& code) {
  auto it = std::remove_if(zones.begin(), zones.end(),
                           [&](const Zone& z) { return z.code == code; });
  if (it == zones.end()) return false;
  zones.erase(it, zones.end());
  for (auto& n : neurons)
    if (n.zone == code) n.zone = ""; // (1.3) → «без зоны»
  for (auto& n : neurons) {
    n.growth.erase(std::remove(n.growth.begin(), n.growth.end(), code), n.growth.end());
    n.mzonelist.erase(std::remove(n.mzonelist.begin(), n.mzonelist.end(), code), n.mzonelist.end());
  }
  for (auto& c : clusters) {
    if (c.zone == code) c.zone = ""; // R4: ссылка зоны у кластера тоже снимается
    c.growZones.erase(std::remove(c.growZones.begin(), c.growZones.end(), code), c.growZones.end());
  }
  dirty = true;
  return true;
}

bool Project::removeClusterNeurons(const std::string& clusterId) {
  Cluster* c = cluster(clusterId);
  if (!c) return false;
  std::vector<std::string> members = c->members;
  for (const auto& m : members) removeNeuron(m);
  c->members.clear();
  dirty = true;
  return true;
}

int Project::synapsesOf(const std::string& neuronId, bool in) const {
  int c = 0;
  for (const auto& s : synapses)
    if ((in ? s.post : s.pre) == neuronId) ++c;
  return c;
}

// ------------------------------------------------------------------
// копия/сравнение
// ------------------------------------------------------------------
void deepCopy(const Project& from, Project& to) {
  to = from; // POD-поля и векторы копируются po значению целиком
}

static uint64_t hmix(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}
static uint64_t hstr(uint64_t h, const std::string& s) {
  for (unsigned char c : s) h = hmix(h, c);
  h = hmix(h, 0xffULL);
  return h;
}

std::string schemaHash(const Project& p) {
  // 9.3: хеш СТРУКТУРЫ (не динамики): сроки, зоны, юниты с ключевыми полями, связи.
  uint64_t h = 0x12345678;
  h = hmix(h, (uint64_t)p.neurons.size());
  h = hmix(h, (uint64_t)p.synapses.size());
  for (const auto& z : p.zones) h = hstr(h, z.code);
  for (const auto& n : p.neurons) {
    h = hstr(h, n.id);
    h = hmix(h, (uint64_t)n.type.v);
    h = hstr(h, n.zone);
    h = hmix(h, (uint64_t)std::llround(n.pos.x * 1000));
    h = hmix(h, (uint64_t)std::llround(n.pos.y * 1000));
    h = hmix(h, (uint64_t)std::llround(n.pos.z * 1000));
    h = hmix(h, (uint64_t)n.refrMax.v);
    h = hmix(h, n.disabled ? 1 : 0);
    h = hmix(h, n.model.set ? (uint64_t)n.model.v : 0);
    h = hmix(h, n.thr.set ? (uint64_t)std::llround(n.thr.v * 1000) : (uint64_t)-1);
    h = hmix(h, n.leak.set ? (uint64_t)std::llround(n.leak.v * 1000) : (uint64_t)-1);
  }
  for (const auto& s : p.synapses) {
    h = hstr(h, s.pre);
    h = hstr(h, s.post);
    h = hmix(h, (uint64_t)std::llround(s.weight.v * 1000));
    h = hmix(h, (uint64_t)s.sign.v);
    h = hmix(h, s.enabled ? 1 : 0);
    h = hmix(h, s.delay.set ? (uint64_t)s.delay.v : 0);
    h = hmix(h, s.plast.set ? (uint64_t)s.plast.v : 0);
  }
  char buf[32];
  std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
  return std::string(buf);
}

} // namespace mp
