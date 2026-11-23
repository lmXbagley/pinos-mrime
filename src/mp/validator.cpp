#include "mp/validator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace mp {

// ── 8.1 красные (не валидируется, юнит/схема помечаются) ─────────────────
//  R1            — незаданные обязательные параметры (никаких молчаливых умолчаний)
//  motor.no-inputs   — мотор без входных связей
//  reward.no-mod     — RewardGated-связь при отсутствии модулятора в схеме
//  exc.cycle         — чисто возбуждающий цикл
//  tick.budget       — превышение бюджета такта проекта
//  orphan            — юнит вне кластера без наследования и без связей
// ── 8.2 жёлтые (предупреждения) ──────────────────────────────────────────
//  dead.input        — нейрон без входов
//  cluster.uniform   — все веса внутри кластера одинаковы
//  exc.norm          — суммарное возбуждение в норме превышено

static void pushFlag(std::vector<Flag>& out, FlagLevel lvl, const std::string& code,
                     const std::string& txt, const std::string& unit) {
  out.push_back({lvl, code, txt, unit});
}

std::vector<Flag> validate(const Project& doc) {
  std::vector<Flag> out;

  // --- R1: незаданные параметры (8.1) ---
  for (const auto& n : doc.neurons)
    if (n.missingParam())
      pushFlag(out, FlagLevel::Red, "R1", "нейрон " + n.id + ": незаданные обязательные параметры", n.id);
  for (const auto& s : doc.synapses)
    if (s.missingParam())
      pushFlag(out, FlagLevel::Red, "R1", "связь " + s.id + ": незаданные обязательные параметры", s.pre);
  for (const auto& c : doc.clusters)
    if (c.missingParam())
      pushFlag(out, FlagLevel::Red, "R1", "кластер " + c.name + ": незаданные параметры", c.id);

  // --- индексы входящих/исходящих ---
  std::map<std::string, std::vector<const Synapse*>> in, outm;
  for (const auto& s : doc.synapses) {
    if (!s.alive) continue;
    outm[s.pre].push_back(&s);
    in[s.post].push_back(&s);
  }
  int modulatorCount = 0;
  for (const auto& n : doc.neurons)
    if (n.type.set && (NType)n.type.v == NType::Modulator && !n.disabled) ++modulatorCount;

  // --- мотор без входов (8.1) ---
  for (const auto& n : doc.neurons) {
    if (!n.type.set || (NType)n.type.v != NType::Motor || n.disabled) continue;
    if (in[n.id].empty())
      pushFlag(out, FlagLevel::Red, "motor.no-inputs",
               "мотор " + n.id + " без входных связей", n.id);
  }

  // --- reward-gated без модулятора (8.1) ---
  for (const auto& s : doc.synapses) {
    if (!s.alive) continue;
    if (s.plast.set && (Plasticity)s.plast.v == Plasticity::RewardGated && modulatorCount == 0)
      pushFlag(out, FlagLevel::Red, "reward.no-mod",
               "связь " + s.id + ": RewardGated без модулятора в схеме", s.pre);
  }

  // --- чисто возбуждающий цикл (8.1): DFS только по возбуждающим связям ---
  {
    std::set<std::string> visited, inStack;
    // лёгкая DFS с раскраской по каждому старту
    for (const auto& n : doc.neurons) {
      if (inStack.count(n.id)) continue;
      std::vector<std::pair<std::string, size_t>> stack{{n.id, 0}};
      std::set<std::string> onPath{n.id};
      while (!stack.empty()) {
        auto& [cur, idx] = stack.back();
        if (idx >= outm[cur].size()) { onPath.erase(cur); stack.pop_back(); continue; }
        const Synapse* s = outm[cur][idx++];
        bool exc = s->sign.set ? (SSign)s->sign.v == SSign::Exc : true;
        if (!exc || !s->enabled) continue;
        if (s->post == n.id) {
          pushFlag(out, FlagLevel::Red, "exc.cycle",
                   "чисто возбуждающий цикл через " + n.id, n.id);
          while (!stack.empty()) stack.pop_back();
          break;
        }
        if (!onPath.count(s->post)) { onPath.insert(s->post); stack.push_back({s->post, 0}); }
      }
    }
  }

  // --- бюджет такта (8.1): оценка работы одного тика ≤ tickBudget ---
  {
    long long load = (long long)doc.neurons.size() + (long long)doc.synapses.size();
    if (load > doc.tickBudget)
      pushFlag(out, FlagLevel::Red, "tick.budget",
               "работа такта " + std::to_string(load) + " > бюджет " +
               std::to_string(doc.tickBudget), doc.neurons.empty() ? "" : doc.neurons[0].id);
  }

  // --- сироты вне кластера без наследования (8.1) ---
  for (const auto& n : doc.neurons) {
    if (!n.srcCluster.empty() || !in[n.id].empty() || !outm[n.id].empty()) continue;
    pushFlag(out, FlagLevel::Red, "orphan",
             "юнит " + n.id + " вне кластера без наследования и связей", n.id);
  }

  // --- жёлтый: нейрон без входов (8.2), не сенсор и не модулятор ---
  for (const auto& n : doc.neurons) {
    if (!n.type.set || (NType)n.type.v == NType::Modulator || n.disabled) continue;
    if ((NType)n.type.v == NType::Sensor && n.src.set) continue; // у сенсора есть источник
    if (in[n.id].empty())
      pushFlag(out, FlagLevel::Yellow, "dead.input",
               "нейрон " + n.id + " без входов", n.id);
  }

  // --- жёлтый: все связи кластера одного веса (8.2) ---
  for (const auto& c : doc.clusters) {
    if (c.members.size() < 2) continue;
    std::set<std::string> mem(c.members.begin(), c.members.end());
    std::set<double> ws; bool any = false;
    for (const auto& s : doc.synapses) {
      if (!s.alive || !mem.count(s.pre) || !mem.count(s.post)) continue;
      ws.insert(s.weight.set ? s.weight.v : s.w);
      any = true;
    }
    if (any && ws.size() == 1)
      pushFlag(out, FlagLevel::Yellow, "cluster.uniform",
               "все связи кластера " + c.name + " одного веса", c.id);
  }

  // --- жёлтый: норма возбуждения (8.2) ---
  for (const auto& n : doc.neurons) {
    double sum = 0;
    for (const Synapse* s : in[n.id]) {
      bool exc = s->sign.set ? (SSign)s->sign.v == SSign::Exc : true;
      if (exc && s->enabled && s->weight.set) sum += s->weight.v;
    }
    if (sum > doc.excNormLimit)
      pushFlag(out, FlagLevel::Yellow, "exc.norm",
               "суммарное возбуждение " + n.id + " = " + std::to_string(sum) +
               " > " + std::to_string(doc.excNormLimit), n.id);
  }

  return out;
}

bool hasRedFlags(const std::vector<Flag>& flags) {
  return std::any_of(flags.begin(), flags.end(),
                     [](const Flag& f) { return f.level == FlagLevel::Red; });
}
bool hasYellowFlags(const std::vector<Flag>& flags) {
  return std::any_of(flags.begin(), flags.end(),
                     [](const Flag& f) { return f.level == FlagLevel::Yellow; });
}

} // namespace mp
