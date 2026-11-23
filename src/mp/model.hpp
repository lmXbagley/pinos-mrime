#pragma once
// mp/model.hpp — документ MINOSPRIME v0.2.
// R1: ни одного молчаливого умолчания — все числовые/перечислимые параметры юнита
//     лежат в Opt<T>: либо set, либо валидатор красит юнит красным.
// R2: код и мышь правят ОДИН документ; юниты, созданные кодом, помечены byCode,
//     пересборка их заменяет, ручные юниты не трогает; конфликты возвращаются списком.
// R4: удаление нейрона/связи/зоны/кластера корректно чистит все индексы
//     (связи, кластеры, зоны, id2n, счётчики ID).
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mp {

// ---------- Opt<T>: явная незаданность (R1) ----------
template <class T>
struct Opt {
  bool set = false;
  T v{};
  Opt() = default;
  Opt(T x) : set(true), v(x) {}
  bool operator==(const Opt& o) const { return set == o.set && (!set || v == o.v); }
  Opt& operator=(T x) { set = true; v = x; return *this; }
};

using OptF = Opt<double>;
using OptI = Opt<long long>;

// ---------- Перечисления (1.2, 1.5, 1.14–1.25) ----------
enum class NType { Sensor = 0, Hidden, Motor, Modulator, Homeostat };
enum class NModel { LIF = 0, ALIF };
enum class SensorSource { Manual = 0, API, Pin, Camera, Mic, Touch };
enum class SensorEncoding { Rate = 0, EventDiff, EventOnOff };
enum class Actuator { ServoL = 0, ServoR, Voice };
enum class Curve { Linear = 0, Gamma };
enum class ModKind { Dopamine = 0, Punishment, Novelty };
enum class ModScope { Global = 0, Zone, Zones };
enum class SSign { Exc = 0, Inh };
enum class Plasticity { Rigid = 0, Hebb, RewardGated, AntiHebb };
enum class Shape { Sphere = 0, Grid, Box };
enum class Topo { Grid = 0, Random, KNN, Full };

const char* ntypeName(NType t);
const char* plasticityName(Plasticity p);

// ---------- Vec3 ----------
struct V3 {
  double x = 0, y = 0, z = 0;
};
inline bool operator==(const V3& a, const V3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// ---------- Нейрон (1.1–1.28) ----------
struct Neuron {
  std::string id;        // канонический "кодзоны.тип.ннн" (1.1)
  std::string label;     // короткая метка S1/H7/M3/G2/D1 (1.1), редактируемая
  OptI type;             // NType (1.2)
  std::string zone;      // код зоны или "" = без зоны (1.3)
  V3 pos;                // (1.4) — вектор, не Opt (позиция всегда есть)
  OptI model;            // NModel (1.5)
  OptF thr, leak;        // (1.6, 1.7)
  OptI refrMax;          // (1.8) тики
  OptF noise;            // (1.9)
  OptI trace;            // (1.10) тики eligibility trace
  bool  homeoOn = false; OptF homeoRate; // (1.11)
  std::vector<std::string> growth; // (1.12) маска роста: коды зон
  std::string color, note, tag;    // (1.13)
  // сенсор
  OptI src;              // SensorSource (1.14), Pin/Camera/Mic/Touch зарезервированы
  OptI enc;              // SensorEncoding (1.15)
  OptF fireThr, hyst;    // (1.16)
  OptF normMin, normMax; // (1.17)
  OptI smoothMs;         // (1.18)
  // мотор
  OptI act;              // Actuator (1.19) зарезервирован
  OptF mapMin, mapMax, mapSpeed; bool inv = false; // (1.20)
  OptI curve; OptF gamma;          // (1.21)
  // модулятор
  OptI mkind;            // ModKind (1.22)
  OptI mscope;           // ModScope (1.23)
  std::vector<std::string> mzonelist; // (1.23) список зон для ModScope::Zones
  OptF windowSec;        // (1.24) окно действия ЭТОГО модулятора (не глобальный MOD_TTL)
  OptF rate;             // (1.25) ставка
  // гомеостат (как тип клетки 1.2 + п.1.26–1.28)
  OptF target;           // (1.26) целевой уровень возбуждения
  OptF integSec;         // (1.27) окно усреднения
  OptF kThr, kPlast;     // (1.28) влияние на пороги/пластичность
  // служебное
  bool disabled = false; // повреждение (5.10), R5
  bool byCode = false;   // создан/ведётся кодом (R2)
  std::string srcCluster; // id кластера-генератора, если есть
  // динамика
  double u = 0, uThrAdapt = 0, homeoAdj = 0, lastInSum = 0, inWeightSum = 0;
  long long refr = 0, traceV = 0;
  long long spikeCount = 0; // всего спайков (для API /stream hz-фильтра и HUD)
  bool spiked = false;
  bool alive = true;

  bool missingParam() const; // R1: есть незаданный обязательный параметр → красный
};

// ---------- Связь (2.1–2.11) ----------
struct Synapse {
  std::string id;        // syn.ннн
  std::string pre, post; // (2.1)
  OptF weight;           // (2.2) модуль веса
  OptI sign;             // SSign (2.3) явный знак, не знак числа
  OptI plast;            // Plasticity (2.4)
  OptF lr;               // (2.5)
  OptI pw;               // (2.6) окно пластичности, тики
  OptF wMin, wMax;       // (2.7)
  OptF decay;            // (2.8) скорость распада при неиспользовании
  OptI delay;            // (2.9) тики
  bool enabled = true;   // (2.10) вкл/выкл (повреждение), R5
  std::string color, note; // (2.11)
  bool byCode = false;   // R2
  // динамика
  double w = 0;          // текущий (пластичный) вес: magnitude со знаком sign
  long long idle = 0, elapsed = 0;
  long long lastPreTick = -1, lastPostTick = -1;
  bool spikedNow = false; // pre спайкнул на этом тике
  bool alive = true;

  double effW() const;   // weight * (sign==Inh ? -1 : +1)
  bool missingParam() const; // R1
};

// ---------- Зона ----------
struct Zone {
  std::string code;   // короткий код (1.1), напр. "V1"
  std::string color = "#3a4a5a";
  std::string note;
  bool byCode = false;
};

// ---------- Кластер-генератор (3.1–3.4) ----------
struct Cluster {
  std::string id;        // clust.ннн
  std::string name;      // (3.2)
  std::string zone;      // код зоны (3.2)
  OptI shape;            // Shape
  V3 center;             // центр
  OptF radius;           // радиус/размер
  OptI count;            // число нейронов
  OptI topo;             // Topo (сетка/случайно/kNN/полный)
  OptI knnK;             // k для kNN
  OptF density;          // плотность внутренних связей (Topo::Random/full)
  // формулы вариации параметров с сидом (3.2)
  OptF thrVar, leakVar, wVar; // амплитуды вариации (±), применяются к базовым значениям
  OptI seed;
  // рост
  OptF neuroProb, pruneProb;  // вероятности нейрогенеза и отсева (3.2)
  bool growAll = true;        // разрешённые направления роста: если false — маска
  std::vector<std::string> growZones;
  bool byCode = false;
  // ---- развёртка ----
  // Кластер РАЗВОРАЧИВАЕТСЯ в нейроны при компиляции (3.3); нейроны держат srcCluster.
  std::vector<std::string> members; // id нейронов, порождённых этим кластером

  bool missingParam() const;
};

// ---------- Флаг валидатора (8.x) ----------
enum class FlagLevel { Red = 0, Yellow };
struct Flag {
  FlagLevel level;
  std::string code;    // R1 / motor.no-inputs / reward.no-mod / exc.cycle / tick.budget /
                       // orphan / dead.input / cluster.uniform / exc.norm
  std::string txt;
  std::string unit;    // id юнита для клика → камера (8.3, K10)
};

// ---------- Событие стенда (9.2) ----------
struct StandEvent {
  double t;              // сек от начала прогона
  bool api = false;      // воздействие API (иначе — мышь)
  std::string path;      // "/set_sensor?id=..&v=.." (api) или "" (мышь)
  std::string neuronId;  // цель мыши
  double v = 0;          // значение (v/fill для мыши)
};

// ---------- Стенд (9.1–9.4) ----------
struct Stand {
  std::string name;              // (9.1) именованный
  std::string schemaHash;        // (9.3) привязка к версии схемы
  std::vector<StandEvent> events;
  std::string resultHash;        // зафиксированный результат эталонного прогона
  double recSpikesPerSec = 0;    // эталонные метрики
  long long recSpikesTotal = 0;
  double durationSec = 5.0;      // длина прогона
  double speed = 1.0;            // скорость воспроизведения (9.2)
};

// ---------- Проект ----------
struct Project {
  std::string name;      // имя проекта (HUD 6.6)
  long long tickBudget = 200000; // бюджет тика (8.1) — редактируемый проектный параметр
  double excNormLimit = 8.0;     // норма возбуждения (8.2 жёлтая)
  double tickHz = 1000;          // базовая частота тика (для авто-прогонов)
  double gammaConst = 0.05;      // внутренний шаг адаптации порога (ALIF), документирован
  double uRest = 0.0;            // потенциал покоя, документирован в docs/SIMULATION.md

  std::vector<Zone> zones;
  std::deque<Neuron> neurons;  // deque: ссылки/указатели стабильны при добавлении (R2/R4)
  std::vector<Synapse> synapses;
  std::vector<Cluster> clusters;
  std::vector<Stand> stands;

  // --- счётчики ID (1.1, R4, R5) ---
  std::map<std::string, long long> idCounter; // ключ "кодзоны.тип" → счётчик
  long long synCounter = 0;
  long long clustCounter = 0;

  bool dirty = false; // для «Сохранить как…» и автосохранения

  // --- динамические индексы (пересобираются тиком; R4 чистит при удалении) ---
  struct HomeoState { double avgHz = 0; double kThr = 0; double kPlast = 0; };
  std::map<std::string, HomeoState> homeoState_;
  std::map<std::string, size_t> neuronIndexCache_;

  // --- поиск/индексы ---
  Neuron* neuron(const std::string& id);
  const Neuron* neuron(const std::string& id) const;
  Synapse* synapse(const std::string& id);
  Zone* zone(const std::string& code);
  Cluster* cluster(const std::string& id);
  std::string neuronZoneCodeOf(const Neuron& n) const { return n.zone; }

  // --- генерация ID (1.1): "зона.тип.ннн" ---
  static char typeCode(NType t);
  std::string nextNeuronId(const std::string& zoneCode, NType t, const std::string& labelHint);
  std::string nextSynId();
  std::string nextClusterId();

  // --- мутации с полной очисткой индексов (R4) ---
  Neuron* addNeuron(Neuron n);                 // проставляет id, карточку считает заполненной
  bool removeNeuron(const std::string& id);    // K1
  bool removeSynapse(const std::string& id);
  bool removeZone(const std::string& code);    // чистит zf у нейронов? нет: убирает ссылку (1.3 → «без зоны»)
  bool removeClusterNeurons(const std::string& clusterId);
  void recomputeIdCounters();                  // R4/R5: пересчёт подтверждённый MAX, вызывается только после load

  int synapsesOf(const std::string& neuronId, bool in) const;
};

// ---------- Копия/сравнение (undo, стенды) ----------
void deepCopy(const Project& from, Project& to);
std::string schemaHash(const Project& p);  // хеш структуры (9.3): только ids/структура связей, без динамики

} // namespace mp
