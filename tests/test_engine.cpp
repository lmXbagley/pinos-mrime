#include "test_util.hpp"

#include "mp/dsl.hpp"
#include "mp/engine.hpp"

using namespace mp;

static OptF OF(double v) { return OptF(v); }
static OptI OI(long long v) { return OptI(v); }

static Neuron base(NType t, const std::string& zone) {
  Neuron n;
  n.type = OI((long long)t); n.zone = zone; n.pos = {0, 0, 0};
  n.model = OI((long long)NModel::LIF);
  n.thr = OF(1.0); n.leak = OF(0.0); n.refrMax = OI(2);
  n.noise = OF(0.0); n.trace = OI(0);
  return n;
}

// Формула (docs/SIMULATION.md, заморожено): u' = u + leak*(uRest-u) + I; spike iff u >= thr
MPT_TEST(engine_lif_spike_formula) {
  Project doc;
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  std::string id = n->id;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Synapse sy;
  sy.pre = s->id; sy.post = id;
  sy.weight = OF(0.5); sy.sign = OI((long long)SSign::Exc);
  sy.plast = OI((long long)Plasticity::Rigid); sy.lr = OF(0.01);
  sy.pw = OI(10); sy.wMin = OF(0); sy.wMax = OF(2); sy.decay = OF(0); sy.delay = OI(1);
  sy.id = doc.nextSynId();
  doc.synapses.push_back(sy);
  Engine eng(doc);
  eng.setSensorValue(s->id, 10.0);         // rate-кодирование: hz=10000 → период 1 тик
  eng.tick();                              // T1: сенсор спайкнул, доставка — на T2
  CHECK(mpt::near(doc.neuron(id)->u, 0.0, 1e-9), "no input on pre-spike tick (delay=1)");
  eng.tick();                              // T2: доставка w=0.5
  CHECK(mpt::near(doc.neuron(id)->u, 0.5, 1e-9), "input delivered at t+delay: u = w*1 (leak=0)");
  eng.tick();                              // T3: вторая доставка — u=1.0>=1 → спайк
  Neuron* after = doc.neuron(id);
  CHECK(after->spiked, "u>=thr => spike");
  CHECK(mpt::near(after->u, doc.uRest, 1e-9), "spike resets u to uRest");
  CHECK(after->refr == 2, "refractory counter set from refrMax");
}

// рефрактерность блокирует спайки; delay (2.9) откладывает доставку
MPT_TEST(engine_refractory_and_delay) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  Synapse sy;
  sy.pre = s->id; sy.post = n->id;
  sy.weight = OF(5.0); sy.sign = OI((long long)SSign::Exc);
  sy.plast = OI((long long)Plasticity::Rigid); sy.lr = OF(0.01);
  sy.pw = OI(10); sy.wMin = OF(0); sy.wMax = OF(5); sy.decay = OF(0); sy.delay = OI(2);
  sy.id = doc.nextSynId();
  doc.synapses.push_back(sy);
  Engine eng(doc);
  eng.setSensorValue(s->id, 10);
  eng.tick();                                    // T1: pre спайк
  CHECK(doc.neuron(n->id)->lastInSum == 0, "delay=2: no delivery on t+1");
  eng.tick();
  CHECK(doc.neuron(n->id)->lastInSum == 0, "delay=2: no delivery before t+delay");
  eng.tick();                                    // T3: доставка на t+delay
  const Neuron* nn = doc.neuron(n->id);
  CHECK(nn->spiked, "delayed spike delivered at t+delay (2.9)");
  CHECK(nn->refr == 2, "refractory counter set");
  eng.tick(); eng.tick();                        // T4: рефрактерность глушит спайки
  CHECK(!doc.neuron(n->id)->spiked, "refractory blocks spike");
  eng.tick();                                    // T5: новая доставка после рефрактерности
  CHECK(doc.neuron(n->id)->spiked, "after refractory: spiking resumes");
}

// ALIF: адаптация порога (1.5) + релаксация homeoRate (1.11)
MPT_TEST(engine_alif_threshold_adaptation) {
  Project doc;
  Neuron n = base(NType::Hidden, "");
  n.model = OI((long long)NModel::ALIF);
  Neuron* np = doc.addNeuron(n);
  std::string id = np->id;
  Engine eng(doc);
  eng.fireOnce(id);                       // 5.9: ручное возбуждение
  eng.tick();
  Neuron* after = doc.neuron(id);
  CHECK(after->spiked, "manual spike");
  CHECK(after->uThrAdapt > 0.0, "ALIF: uThrAdapt += gammaConst");
  CHECK(after->refr > 0, "refractory active");
}

// сенсор: rate-кодирование с нормализацией (1.16, 1.17)
MPT_TEST(engine_sensor_rate_encoding) {
  Project doc;
  Neuron n = base(NType::Sensor, "");
  n.enc = OI((long long)SensorEncoding::Rate);
  n.fireThr = OF(0.5); n.hyst = OF(0.0);
  n.normMin = OF(0); n.normMax = OF(100);
  n.smoothMs = OI(1);
  n.thr = OF(0.001);                      // сработает почти сразу
  Neuron* np = doc.addNeuron(n);
  Engine eng(doc);
  eng.setSensorValue(np->id, 100);        // нормализованный max
  eng.tick();
  const Neuron* after = doc.neuron(np->id);
  CHECK(after->spiked, "rate sensor at max input spikes");
}

// пластичность Hebb (2.4): в окне pw вес растёт на lr; decay ослабляет простаивающие
MPT_TEST(engine_hebb_plasticity_and_decay) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  Synapse sy;
  sy.pre = s->id; sy.post = n->id;
  sy.weight = OF(1.0); sy.sign = OI((long long)SSign::Exc);
  sy.plast = OI((long long)Plasticity::Hebb); sy.lr = OF(0.1);
  sy.pw = OI(5); sy.wMin = OF(0); sy.wMax = OF(2); sy.decay = OF(0.0); sy.delay = OI(0);
  sy.id = doc.nextSynId();
  doc.synapses.push_back(sy);
  Engine eng(doc);
  eng.setSensorValue(s->id, 10);
  eng.tick();        // pre спайк (сенсор)
  double w0 = doc.synapse(sy.id)->w;
  CHECK(mpt::near(w0, 1.0, 1e-9), "initial w == weight");
  eng.setSensorValue(n->id, 0);           // (n не сенсор — не влияет)
  (void)n;
  eng.fireOnce(n->id);                    // post спайк в окне pw
  eng.tick();
  Synapse* sy2 = doc.synapse(sy.id);
  CHECK(mpt::near(sy2->w, 1.0 + 0.1, 1e-9), "Hebb: w += lr when |t_pre - t_post| <= pw");
  // clamp до wMax
  eng.resetDyn();
  sy2->w = 1.97;
  eng.setSensorValue(s->id, 10);
  eng.tick(); eng.fireOnce(n->id); eng.tick();
  CHECK(mpt::near(doc.synapse(sy.id)->w, 2.0, 1e-9), "clamped to wMax");
}

// RewardGated: без активного модулятора пластичности нет; с ним (1.24, 1.25) — есть
MPT_TEST(engine_reward_gated_needs_modulator) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  std::string sid = s->id, nid = n->id;         // пойнтеры инвалидируются push_back'ом в neurons
  Synapse sy;
  sy.pre = sid; sy.post = nid;
  sy.weight = OF(1.0); sy.sign = OI((long long)SSign::Exc);
  sy.plast = OI((long long)Plasticity::RewardGated); sy.lr = OF(0.1);
  sy.pw = OI(5); sy.wMin = OF(0); sy.wMax = OF(2); sy.decay = OF(0); sy.delay = OI(0);
  sy.id = doc.nextSynId();
  doc.synapses.push_back(sy);
  Engine eng(doc);
  eng.setSensorValue(sid, 10);
  eng.tick();
  eng.fireOnce(nid);
  eng.tick();
  CHECK(mpt::near(doc.synapse(sy.id)->w, 1.0, 1e-9), "no modulator => no plastic change");

  // добавляем модулятор с rate и окном (1.22–1.25)
  Neuron mod = base(NType::Modulator, "");
  mod.mkind = OI((long long)ModKind::Dopamine);
  mod.mscope = OI((long long)ModScope::Global);
  mod.windowSec = OF(1.0);
  mod.rate = OF(0.5);
  Neuron* mp = doc.addNeuron(mod);
  eng.setSensorValue(sid, 10);
  eng.fireOnce(mp->id);                    // спайк модулятора открывает окно (1.24)
  eng.tick();
  eng.fireOnce(nid);
  eng.tick();                              // ещё в окне
  CHECK(mpt::near(doc.synapse(sy.id)->w, 1.0 + 0.1 * 0.5, 1e-9),
        "reward-gated: w += lr * sum(active modulator rates)");
}

// homeostat (1.2, 1.26–1.28): усреднённая Гц смещает пороги и масштабирует пластичность
MPT_TEST(engine_homeostat_adjusts) {
  Project doc;
  Neuron h = base(NType::Homeostat, "");
  h.target = OF(0.0);                       // целевой уровень 0
  h.integSec = OF(0.01);
  h.kThr = OF(1.0);
  h.kPlast = OF(1.0);
  doc.addNeuron(h);
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  Synapse sy;
  sy.pre = s->id; sy.post = n->id;
  sy.weight = OF(1.0); sy.sign = OI((long long)SSign::Exc);
  sy.plast = OI((long long)Plasticity::Hebb); sy.lr = OF(0.1);
  sy.pw = OI(5); sy.wMin = OF(0); sy.wMax = OF(2); sy.decay = OF(0); sy.delay = OI(0);
  sy.id = doc.nextSynId();
  doc.synapses.push_back(sy);
  Engine eng(doc);
  eng.setSensorValue(s->id, 10);
  eng.setSensorValue(h.id, 10);             // homeostat активен
  for (int t = 0; t < 20; ++t) eng.tick();
  const Neuron* nn = doc.neuron(n->id);
  CHECK(nn->homeoAdj != 0.0, "homeoAdj applied (1.28)");
  CHECK(eng.modulatorLevel() >= 0, "modulatorLevel readable");
}

// поток (5.9): setFlow доставляет спайки с частотой
MPT_TEST(engine_flow_slider) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Engine eng(doc);
  eng.setFlow(s->id, 1000.0);               // каждый тик
  eng.tick();
  const Neuron* after = doc.neuron(s->id);
  CHECK(after->spiked, "flow=1000Hz spikes every tick");
  long long c1 = after->spikeCount;
  eng.setFlow(s->id, 0.0);
  eng.tick();
  const Neuron* after2 = doc.neuron(s->id);
  CHECK(after2->spikeCount == c1, "flow=0 stops input");
}

// K: детерминизм — один и тот же прогон даёт ту же траекторию
MPT_TEST(engine_deterministic) {
  auto make = []() {
    Project doc;
    Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
    Neuron* h = doc.addNeuron(base(NType::Hidden, ""));
    h->noise = OF(0.2);                      // шум из детерминированного LCG (1.9)
    h->homeoOn = false;
    Synapse sy;
    sy.pre = s->id; sy.post = h->id;
    sy.weight = OF(0.9); sy.sign = OI((long long)SSign::Exc);
    sy.plast = OI((long long)Plasticity::Hebb); sy.lr = OF(0.01);
    sy.pw = OI(5); sy.wMin = OF(0); sy.wMax = OF(2); sy.decay = OF(0); sy.delay = OI(1);
    sy.id = doc.nextSynId();
    doc.synapses.push_back(sy);
    return doc;
  };
  Project a = make(), b = make();
  Engine ea(a), eb(b);
  ea.setSensorValue(a.neurons[0].id, 10);
  eb.setSensorValue(b.neurons[0].id, 10);
  for (int t = 0; t < 50; ++t) { ea.tick(); eb.tick(); }
  CHECK(a.neurons[1].spikeCount == b.neurons[1].spikeCount, "same ticks => same spikes");
  CHECK(mpt::near(a.neurons[1].u, b.neurons[1].u, 1e-12), "same u trajectory");
}
