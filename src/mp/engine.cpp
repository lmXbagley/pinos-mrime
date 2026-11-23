#include "mp/engine.hpp"

#include <algorithm>
#include <cmath>

namespace mp {

// Формулы (docs/SIMULATION.md, зафиксированы; v0.2 их не меняет — R3/12):
//   I_n(t)  = Σ_{s: post==n, enabled} effW(s) * pre_spiked(t - delay_s)
//   u'      = u + leak*(uRest - u) + I + noise*gauss()      (1 tick = 1 мс)
//   спайк   : refr == 0 && u >= thr_eff;  thr_eff = thr + uThrAdapt + homeoAdj
//   ALIF    : при спайке uThrAdapt += PROJ.gammaConst;
//             иначе uThrAdapt *= exp(-relax/1000), relax = homeoRate (если homeoOn) иначе 0
//   после спайка: u = uRest, refr = refrMax, traceV = trace (1.10); traceV-- каждый тик
//   доставка   : спайк pre в тик T приходит на фазе I тика T + max(delay,1)
//   сенсор rate      : гармоника частоты: hz = 1000*smooth, период round(1000/hz), детерминированно
//   сенсор EventDiff : спайк при |raw - raw_prev| > fireThr
//   сенсор EventOnOff: on при пересечении fireThr вверх; off при падении ниже fireThr-hyst
//   пластичность (при |t_pre - t_post| <= pw тиков):
//     Хебб w+=lr; анти-Хебб w-=lr; reward-gated w+=lr*M; жёсткая — без изменений
//     M = Σ ставок активных модуляторов (1.25)
//   распад (2.8): тик без пары спайков → w *= (1 - decay); клампы |w| в [wMin, wMax]
//   модулятор (1.22–1.25): спайк модулятора активирует окно windowSec с уровнем rate
//   гомеостат (1.26–1.28): avgHz scope-нейронов за окно integSec (EMA 1мс);
//     homeoAdj = kThr*(avgHz - target); множитель пластичности = 1 + kPlast*(avgHz - target)
//   клеточный гомеостаз (1.11): релаксация порога со скоростью homeoRate для ALIF/membrane

static const double kAdaptRelaxScale = 1000.0; // мс в сек для экспонент

double Engine::uni() {
  rng_ = std::fmod(rng_ * 1103515245.0 + 12345.0, 2147483648.0);
  if (rng_ < 0) rng_ += 2147483648.0;
  return rng_ / 2147483648.0;
}
double Engine::gauss() {
  double u1 = uni(), u2 = uni();
  if (u1 < 1e-12) u1 = 1e-12;
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

void Engine::setSensorValue(const std::string& neuronId, double raw) {
  sensorRaw_[neuronId] = raw;
}

void Engine::fireOnce(const std::string& neuronId) {
  manualSpikes_.insert(neuronId); // исполнится в ближайшем tick
}

void Engine::setFlow(const std::string& neuronId, double hz) {
  if (hz <= 0) flowHz_.erase(neuronId);
  else flowHz_[neuronId] = hz;
}

void Engine::fillCluster(const std::string& clusterId, double fraction) {
  std::vector<std::string> members;
  for (const auto& n : doc_.neurons)
    if (n.srcCluster == clusterId && !n.disabled) members.push_back(n.id);
  if (members.empty()) return;
  long long k = (long long)std::llround(std::min(1.0, std::max(0.0, std::fabs(fraction))) *
                                        (double)members.size());
  k = std::max<long long>(1, std::min<long long>(k, (long long)members.size()));
  for (long long i = 0; i < k; ++i) {
    size_t idx = (size_t)((double)i * (double)members.size() / (double)members.size());
    idx = (size_t)(((double)i / (double)k) * (double)members.size());
    fireOnce(members[idx % members.size()]);
  }
}

double Engine::modulatorLevel() const {
  double m = 0;
  for (const auto& a : mods_) if (a.until > tickNow_) m += a.rate;
  return m;
}

void Engine::resetDyn() {
  for (auto& n : doc_.neurons) {
    n.u = 0; n.uThrAdapt = 0; n.homeoAdj = 0;
    n.lastInSum = 0; n.inWeightSum = 0;
    n.refr = 0; n.traceV = 0; n.spikeCount = 0; n.spiked = false;
  }
  for (auto& s : doc_.synapses) {
    s.w = s.weight.set ? s.weight.v : 0.0;
    s.idle = 0; s.lastPreTick = -1; s.lastPostTick = -1; s.spikedNow = false;
  }
  sensorRaw_.clear(); sensorPrevRaw_.clear(); sensorSmooth_.clear();
  pending_.clear(); mods_.clear(); spikeHist_.clear();
  manualSpikes_.clear(); flowHz_.clear();
  doc_.homeoState_.clear();
  tickNow_ = 0; rng_ = 0.5;
  st_ = EngineStats{};
}

EngineStats Engine::stats() const {
  EngineStats s = st_;
  s.neurons = (long long)doc_.neurons.size();
  s.synapses = (long long)doc_.synapses.size();
  s.timeSec = (double)tickNow_ / 1000.0;
  long long win = 0;
  for (const auto& kv : spikeHist_)
    if (tickNow_ - kv.first <= 1000) win += (long long)kv.second;
  s.spikesPerSec = (double)win; // окно истории ровно 1000 тиков = 1 с
  return s;
}

void Engine::scheduleOutputs() {
  for (auto& s : doc_.synapses) {
    if (!s.enabled || !s.alive) continue;
    const Neuron* pre = doc_.neuron(s.pre);
    if (!pre || !pre->spiked) continue;
    s.spikedNow = true; // для пластичности: pre активен на этом тике
    s.lastPreTick = tickNow_;
    long long d = s.delay.set ? std::max(0LL, s.delay.v) : 0;
    auto it = doc_.neuronIndexCache_.find(s.post);
    if (it == doc_.neuronIndexCache_.end()) continue;
    pending_.push_back({tickNow_ + std::max(1LL, d), it->second, s.effW()});
    st_.impulses++;
  }
}

void Engine::tick() {
  ++tickNow_;
  st_.impulses = 0;
  int spikesThisTick = 0;

  doc_.neuronIndexCache_.clear();
  for (size_t i = 0; i < doc_.neurons.size(); ++i)
    doc_.neuronIndexCache_[doc_.neurons[i].id] = i;

  // ── фаза 0: суммарный вход I (доставки из очереди задержек 2.9) ──
  std::vector<double> I(doc_.neurons.size(), 0.0);
  for (auto& n : doc_.neurons) { n.lastInSum = 0; n.spiked = false; }
  for (const auto& p : pending_) {
    if (p.at <= tickNow_ && p.post < I.size()) {
      I[p.post] += p.w;
      doc_.neurons[p.post].lastInSum += p.w;
      st_.impulses++;
    }
  }
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                [this](const Pend& p) { return p.at <= tickNow_; }),
                 pending_.end());

  // ── фаза 1: сенсоры (rate / event) и потоки; ручные спайки ниже в фазе 2 ──
  for (size_t i = 0; i < doc_.neurons.size(); ++i) {
    Neuron& n = doc_.neurons[i];
    if (!n.type.set || (NType)n.type.v != NType::Sensor || n.disabled) continue;
    if (!sensorRaw_.count(n.id)) continue; // R1: без явного входа сенсор молчит
    double raw = sensorRaw_[n.id];
    double mn = n.normMin.set ? n.normMin.v : 0.0;
    double mx = n.normMax.set ? n.normMax.v : 1.0;
    double v = (mx - mn) > 1e-12 ? (raw - mn) / (mx - mn) : raw; // (1.17)
    double alpha = 1.0;
    if (n.smoothMs.set && n.smoothMs.v > 0) alpha = 1000.0 / (1000.0 + n.smoothMs.v);
    double sm = (sensorSmooth_.count(n.id) ? (1.0 - alpha) * sensorSmooth_[n.id] : 0.0) + alpha * v;
    sensorSmooth_[n.id] = sm;
    SensorEncoding enc = n.enc.set ? (SensorEncoding)n.enc.v : SensorEncoding::Rate;
    bool spikeNow = false;
    if (enc == SensorEncoding::Rate) {
      double hz = std::max(0.0, sm) * 1000.0;
      long long period = hz > 0 ? std::max(1LL, (long long)std::llround(1000.0 / hz)) : 0;
      spikeNow = period > 0 && (tickNow_ % period == 0);   // детерминированный рате-ритм
    } else {
      double prev = sensorPrevRaw_.count(n.id) ? sensorPrevRaw_[n.id] : raw;
      double d = raw - prev;
      double thrF = n.fireThr.set ? n.fireThr.v : 0.0;
      double hyst = n.hyst.set ? n.hyst.v : 0.0;
      if (enc == SensorEncoding::EventDiff)
        spikeNow = std::fabs(d) > thrF;
      else {
        if (d > 0 && raw > thrF && prev <= thrF) spikeNow = true;
        else if (d < 0 && raw < thrF - hyst && prev >= thrF - hyst) spikeNow = true;
      }
      sensorPrevRaw_[n.id] = raw;
    }
    if (spikeNow) manualSpikes_.insert(n.id);
  }
  for (const auto& kv : flowHz_) {
    if (kv.second <= 0) continue;
    if (uni() < (1.0 - std::exp(-kv.second / 1000.0))) manualSpikes_.insert(kv.first);
  }

  // ── фаза 2: мембраны, спайки, модуляторные окна ──
  for (size_t i = 0; i < doc_.neurons.size(); ++i) {
    Neuron& n = doc_.neurons[i];
    if (!n.alive) continue;
    double leak = n.leak.set ? n.leak.v : 0.0;
    double noise = n.noise.set ? n.noise.v : 0.0;
    if (leak > 0) n.u += leak * (doc_.uRest - n.u);
    n.u += I[i];
    if (noise > 0) n.u += noise * gauss();

    double thr = n.thr.set ? n.thr.v : 0.0;
    long long refrMax = n.refrMax.set ? n.refrMax.v : 0;

    if (n.refr > 0) {
      --n.refr;
      n.spiked = false;
      if (n.traceV > 0) --n.traceV;
    } else if (n.u >= thr + n.uThrAdapt + n.homeoAdj) {
      n.spiked = true;
      ++spikesThisTick;
      ++n.spikeCount;
      n.u = doc_.uRest;
      n.refr = std::max(0LL, refrMax);
      n.traceV = n.trace.set ? n.trace.v : 0; // (1.10)
      if (n.model.set && (NModel)n.model.v == NModel::ALIF)
        n.uThrAdapt += doc_.gammaConst; // адаптация порога (1.5)
      if (n.type.set && (NType)n.type.v == NType::Modulator && n.rate.set) {
        // окно действия ЭТОГО модулятора (1.24), не глобальный MOD_TTL
        double winSec = n.windowSec.set ? n.windowSec.v : 0.0;
        long long until = tickNow_ + (long long)std::max(1LL, (long long)std::llround(winSec * 1000.0));
        mods_.push_back({n.id, until, n.rate.v});
      }
    } else {
      n.spiked = false;
      if (n.traceV > 0) --n.traceV;
    }
    if (manualSpikes_.count(n.id)) {
      if (!n.spiked) {
        n.spiked = true; ++spikesThisTick; ++n.spikeCount; n.u = doc_.uRest;
        if (n.model.set && (NModel)n.model.v == NModel::ALIF)
          n.uThrAdapt += doc_.gammaConst;                  // адаптация на любом спайке (1.5)
      }
      n.refr = refrMax;
      n.traceV = n.trace.set ? n.trace.v : 0;
      manualSpikes_.erase(n.id);
    }
    // релаксация пороговой адаптации (1.11): homeoRate, если клеточный гомеостаз включён
    if (n.homeoOn && n.homeoRate.set && n.homeoRate.v > 0)
      n.uThrAdapt *= std::exp(-n.homeoRate.v / kAdaptRelaxScale);
  }

  // ── фаза 3: гомеостаты (1.26–1.28) ──
  for (const auto& h : doc_.neurons) {
    if (!h.type.set || (NType)h.type.v != NType::Homeostat || h.disabled) continue;
    double integSec = h.integSec.set ? std::max(0.001, h.integSec.v) : 1.0;
    double avgHz = 0;
    for (size_t i = 0; i < doc_.neurons.size(); ++i) {
      const Neuron& n = doc_.neurons[i];
      if (n.id == h.id || !n.type.set) continue;
      if ((NType)n.type.v == NType::Homeostat) continue;
      if (!h.zone.empty() && n.zone != h.zone) continue; // scope: его зона (или все)
      if (n.spiked && !n.disabled) avgHz += 1000.0;      // спайк в этом тике — фрагмент окна интеграции
    }
    // скользящее среднее масштаба «спайки за integSec»: EMA с шагом 1 мс
    double a = 1.0 - std::exp(-1.0 / (integSec * 1000.0));
    Project::HomeoState& hs = doc_.homeoState_[h.id];
    hs.avgHz = (1.0 - a) * hs.avgHz + a * avgHz;
    hs.kThr = h.kThr.set ? h.kThr.v : 0.0;
    hs.kPlast = h.kPlast.set ? h.kPlast.v : 0.0;
  }
  for (const auto& kv : doc_.homeoState_) {
    const Neuron* h = doc_.neuron(kv.first);
    if (!h) continue;
    double target = h->target.set ? h->target.v : 0.0;
    double derr = kv.second.avgHz - target;
    for (auto& n : doc_.neurons) {
      if (n.id == kv.first) continue;
      if (!h->zone.empty() && n.zone != h->zone) continue;
      n.homeoAdj = kv.second.kThr * derr; // (1.28) влияние на пороги
    }
    plastScale_ = 1.0 + kv.second.kPlast * derr; // (1.28) влияние на пластичность
  }

  // ── фаза 4: пластичность (2.4–2.8) ──
  double M = modulatorLevel();
  for (auto& s : doc_.synapses) {
    if (!s.enabled || !s.alive) { s.spikedNow = false; continue; }
    const Neuron* post = doc_.neuron(s.post);
    bool postSpiked = post && post->spiked;
    if (postSpiked) s.lastPostTick = tickNow_;
    long long pw = s.pw.set ? s.pw.v : 0;
    bool pairInWindow = s.lastPreTick >= 0 && s.lastPostTick >= 0 &&
                        std::llabs(s.lastPreTick - s.lastPostTick) <= pw;
    if (pairInWindow && s.plast.set) {
      Plasticity pl = (Plasticity)s.plast.v;
      double lr = (s.lr.set ? s.lr.v : 0.0) * std::max(0.0, plastScale_);
      switch (pl) {
        case Plasticity::Rigid: break;
        case Plasticity::Hebb: s.w += lr; break;
        case Plasticity::AntiHebb: s.w -= lr; break;
        case Plasticity::RewardGated: s.w += lr * M; break;
      }
      s.idle = 0;
    }
    if (!s.spikedNow) ++s.idle;
    if (s.decay.set && s.idle > 0 && s.decay.v > 0)
      s.w *= (1.0 - s.decay.v); // (2.8) распад при неиспользовании
    double wmin = s.wMin.set ? s.wMin.v : 0.0;
    double wmax = s.wMax.set ? s.wMax.v : 1e9;
    double aw = std::fabs(s.w);
    if (aw < wmin) s.w = (s.w < 0 ? -wmin : wmin);
    if (aw > wmax) s.w = (s.w < 0 ? -wmax : wmax);
    s.spikedNow = false;
  }

  // ── фаза 5: карта весов входа (6.3) и постановка выходов в очередь ──
  for (auto& n : doc_.neurons) n.inWeightSum = 0;
  for (const auto& s : doc_.synapses) {
    if (!s.enabled || !s.alive) continue;
    auto it = doc_.neuronIndexCache_.find(s.post);
    if (it != doc_.neuronIndexCache_.end())
      doc_.neurons[it->second].inWeightSum += s.effW();
  }
  scheduleOutputs();

  // ── фаза 6: окна модуляторов (1.24) и статистика ──
  mods_.erase(std::remove_if(mods_.begin(), mods_.end(),
                             [&](const ModActive& a) { return a.until <= tickNow_; }),
              mods_.end());
  spikeHist_.emplace_back(tickNow_, spikesThisTick);
  while (!spikeHist_.empty() && tickNow_ - spikeHist_.front().first > 1000)
    spikeHist_.erase(spikeHist_.begin());
  st_.spikes += spikesThisTick;
}

} // namespace mp
