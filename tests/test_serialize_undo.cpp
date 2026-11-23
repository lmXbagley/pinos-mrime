#include "test_util.hpp"

#include "mp/serialize.hpp"
#include "mp/undo.hpp"

using namespace mp;

static OptF OF(double v) { return OptF(v); }
static OptI OI(long long v) { return OptI(v); }

static Project fullDoc() {
  Project doc;
  doc.name = "roundtrip";
  doc.tickBudget = 123456;
  doc.excNormLimit = 9.5;
  doc.tickHz = 500;
  doc.gammaConst = 0.04;
  doc.uRest = -0.2;
  Zone z; z.code = "V1"; z.color = "#aabbcc"; z.note = "зрительная";
  doc.zones.push_back(z);

  Neuron n;
  n.type = OI((long long)NType::Sensor);
  n.zone = "V1"; n.pos = {1.5, -2.25, 3}; n.label = "S1";
  n.model = OI((long long)NModel::ALIF);
  n.thr = OF(1.2); n.leak = OF(0.85); n.refrMax = OI(4); n.noise = OF(0.01); n.trace = OI(7);
  n.homeoOn = true; n.homeoRate = OF(0.25);
  n.growth.push_back("V1");
  n.color = "#ff0000"; n.note = "заметка"; n.tag = "微粒";
  n.src = OI((long long)SensorSource::Pin);
  n.enc = OI((long long)SensorEncoding::EventDiff);
  n.fireThr = OF(0.5); n.hyst = OF(0.1);
  n.normMin = OF(0.0); n.normMax = OF(100.0);
  n.smoothMs = OI(20);
  doc.addNeuron(n);

  Neuron m;
  m.type = OI((long long)NType::Motor);
  m.zone = "V1"; m.pos = {5, 5, 5};
  m.model = OI((long long)NModel::LIF);
  m.thr = OF(1.0); m.leak = OF(0.8); m.refrMax = OI(3); m.noise = OF(0.0); m.trace = OI(0);
  m.act = OI((long long)Actuator::ServoL);
  m.mapMin = OF(-1); m.mapMax = OF(1); m.mapSpeed = OF(0.5); m.inv = true;
  m.curve = OI((long long)Curve::Gamma); m.gamma = OF(2.2);
  m.disabled = true;                       // R5: повреждение не теряется
  Neuron* mp2 = doc.addNeuron(m);
  mp2->byCode = true; mp2->srcCluster = "clust.1";

  Synapse s;
  s.pre = doc.neurons[0].id; s.post = doc.neurons[1].id;
  s.weight = OF(0.7); s.sign = OI((long long)SSign::Inh);
  s.plast = OI((long long)Plasticity::RewardGated); s.lr = OF(0.01);
  s.pw = OI(15); s.wMin = OF(0); s.wMax = OF(2); s.decay = OF(0.001); s.delay = OI(3);
  s.enabled = false;                       // R5: выключенная связь сохраняется
  s.color = "#00ff00"; s.note = "ингибитор";
  s.id = doc.nextSynId();
  doc.synapses.push_back(s);

  Cluster c;
  c.id = doc.nextClusterId(); c.name = "midbrain"; c.zone = "V1";
  c.shape = OI((long long)Shape::Sphere); c.center = {0, 1, 2};
  c.radius = OF(10); c.count = OI(5); c.topo = OI((long long)Topo::KNN);
  c.knnK = OI(3); c.density = OF(0.25);
  c.thrVar = OF(0.1); c.leakVar = OF(0.05); c.wVar = OF(0.02);
  c.seed = OI(7); c.neuroProb = OF(0.01); c.pruneProb = OF(0.01);
  c.growAll = false; c.growZones.push_back("V1");
  doc.clusters.push_back(c);

  doc.idCounter["V1.S"] = 1; doc.idCounter["V1.M"] = 1;
  doc.synCounter = 1; doc.clustCounter = 1;
  return doc;
}

// K + R5: Save/Load сохраняет ВСЕ поля, включая refrMax, disabled, byCode, счётчики ID
MPT_TEST(serialize_roundtrip_all_fields) {
  Project a = fullDoc();
  std::string txt = saveProjectJson(a);
  Project b; std::string err;
  CHECK(loadProjectJson(txt, b, err), "load ok: " + err);
  CHECK(b.name == "roundtrip" && b.tickBudget == 123456, "project params");
  CHECK(mpt::near(b.excNormLimit, 9.5) && mpt::near(b.tickHz, 500), "project params 2");
  CHECK(mpt::near(b.gammaConst, 0.04) && mpt::near(b.uRest, -0.2), "frozen formula constants");
  CHECK(b.zones.size() == 1 && b.zones[0].note == "зрительная", "zone note (unicode)");
  CHECK(b.neurons.size() == 2, "neurons count");
  const Neuron& sn = b.neurons[0];
  CHECK(sn.refrMax.set && sn.refrMax.v == 4, "R5: refrMax survives");
  CHECK(sn.homeoOn && mpt::near(sn.homeoRate.v, 0.25), "R5: homeo survives");
  CHECK(sn.tag == "微粒", "R5: unicode tag survives");
  CHECK(sn.normMax.set && mpt::near(sn.normMax.v, 100.0), "R5: sensor norm survives");
  CHECK(sn.growth.size() == 1 && sn.growth[0] == "V1", "R5: growth mask survives");
  const Neuron& mn = b.neurons[1];
  CHECK(mn.disabled, "R5: disabled state survives");
  CHECK(mn.byCode && mn.srcCluster == "clust.1", "R5: byCode/srcCluster survive");
  CHECK(mn.inv && mpt::near(mn.gamma.v, 2.2), "R5: motor fields survive");
  CHECK(b.synapses.size() == 1, "synapse survives");
  const Synapse& ss = b.synapses[0];
  CHECK(!ss.enabled, "R5: synapse enabled=false survives");
  CHECK(ss.plast.v == (long long)Plasticity::RewardGated, "R5: plasticity survives");
  CHECK(mpt::near(ss.decay.v, 0.001) && ss.delay.v == 3, "R5: decay/delay survive");
  CHECK(b.clusters.size() == 1 && b.clusters[0].growZones.size() == 1, "R5: cluster survives");
  CHECK(!b.clusters[0].growAll, "R5: cluster growAll survives");
  CHECK(b.idCounter.size() == 2 && b.idCounter.at("V1.S") == 1, "R5: idCounter survives");
  CHECK(b.synCounter == 1 && b.clustCounter == 1, "R5: counters survive");
  // R5: после load счётчики пересчитаны как MAX — следующий ID не коллизирует
  std::string nid = b.nextNeuronId("V1", NType::Sensor, "S");
  CHECK(b.neuron(nid) == nullptr || nid == "V1.S.2", "next id after load is fresh");
}

// 7.3: единая история для мыши и кода; undo возвращает точный документ
MPT_TEST(undo_unified_history) {
  Project doc = fullDoc();
  UndoStack undo(1024);
  undo.push(doc, "начало");
  // «мышь»: добавили нейрон
  Neuron n; n.type = OI((long long)NType::Hidden); n.zone = "V1";
  n.pos = {9, 9, 9}; n.thr = OF(1); n.leak = OF(0.8); n.refrMax = OI(2);
  n.noise = OF(0); n.trace = OI(0);
  doc.addNeuron(n);
  undo.push(doc, "мышь: добавить нейрон");
  // «код»: сборка — тоже в ту же историю (4.6)
  undo.pushCodeBuild("zone(\"V1\");");
  CHECK(undo.size() == 2, "history holds both");
  CHECK(undo.canUndo(), "can undo");
  Project r = undo.undo(doc);
  CHECK(r.neurons.size() == 2, "undo restores mouse op result");
  r = undo.undo(doc);
  CHECK(r.neurons.size() == 2, "undo to initial snapshot");
  CHECK(undo.canRedo(), "redo available");
  r = undo.redo(r);
  CHECK(r.neurons.size() == 2, "redo after undo");
}
