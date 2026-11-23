#include "test_util.hpp"

#include "mp/dsl.hpp"
#include "mp/model.hpp"

using namespace mp;

static Project makeDoc() {
  Project doc;
  doc.name = "t";
  Zone z; z.code = "V1"; z.color = "#112233";
  doc.zones.push_back(z);
  return doc;
}

static Neuron mk(NType t, const std::string& zone) {
  Neuron n;
  n.type = OptI((long long)t);
  n.zone = zone;
  n.pos = {1, 2, 3};
  n.model = OptI((long long)NModel::LIF);
  n.thr = OptF(1.0); n.leak = OptF(0.8); n.refrMax = OptI(3);
  n.noise = OptF(0.0); n.trace = OptI(0);
  return n;
}

// K1 + R4 (главный баг v0.1): удаление нейрона чистит ВСЕ индексы
MPT_TEST(model_remove_neuron_cleans_everything) {
  Project doc = makeDoc();
  Neuron* a = doc.addNeuron(mk(NType::Hidden, "V1"));
  Neuron* b = doc.addNeuron(mk(NType::Motor, "V1"));
  CHECK(a && b, "two neurons added");
  Synapse s;
  s.pre = a->id; s.post = b->id;
  s.weight = OptF(2.0); s.sign = OptI((long long)SSign::Exc);
  s.plast = OptI((long long)Plasticity::Rigid); s.lr = OptF(0.01);
  s.pw = OptI(5); s.wMin = OptF(0); s.wMax = OptF(4); s.decay = OptF(0);
  s.delay = OptI(1);
  s.id = doc.nextSynId();
  doc.synapses.push_back(s);
  // удалить оба — R4
  std::string idA = a->id, idB = b->id;
  CHECK(doc.removeNeuron(idA), "remove a");
  CHECK(doc.removeNeuron(idB), "remove b");
  CHECK(doc.synapses.empty(), "R4: synapses of removed neuron are gone");
  CHECK(doc.synCounter >= 1, "R4: syn counter not corrupted");
  doc.recomputeIdCounters();
  CHECK(!doc.neuron(idA) && !doc.neuron(idB), "id2n index cleans");
}

// R4: удаление зоны не удаляет юниты, а снимает ссылку (1.3) и чистит рост/кластеры
MPT_TEST(model_remove_zone) {
  Project doc = makeDoc();
  Neuron* a = doc.addNeuron(mk(NType::Hidden, "V1"));
  a->growth.push_back("V1");
  Cluster c; c.id = doc.nextClusterId(); c.name = "cl"; c.zone = "V1";
  c.growZones.push_back("V1");
  doc.clusters.push_back(c);
  CHECK(doc.removeZone("V1"), "zone removed");
  CHECK(doc.neurons.size() == 1, "neuron survives zone deletion (ref link only)");
  CHECK(doc.neurons[0].zone.empty(), "zone link cleared to no-zone");
  CHECK(doc.neurons[0].growth.empty(), "growth mask cleared");
  CHECK(doc.clusters[0].zone.empty(), "cluster zone cleared");
  CHECK(doc.clusters[0].growZones.empty(), "cluster growZones cleared");
}

// R1: только unset-параметр делает юнит красным
MPT_TEST(model_missing_param_r1) {
  Project doc = makeDoc();
  Neuron n = mk(NType::Hidden, "V1");
  std::string idFull = doc.addNeuron(n)->id;
  CHECK(!doc.neuron(idFull)->missingParam(), "fully set unit is not red");
  Neuron n2 = mk(NType::Hidden, "V1");
  n2.thr = OptF{}; n2.thr.set = false;   // сняли параметр
  std::string idMiss = doc.addNeuron(n2)->id;
  CHECK(doc.neuron(idMiss)->missingParam(), "unset thr => red (R1)");
  Synapse s;
  s.pre = idFull; s.post = idMiss;
  doc.synapses.push_back(s);
  CHECK(doc.synapses[0].missingParam(), "synapse without params is red");
}

// ID-схема 1.1: кодзона.тип.ннн, счётчики по типам и зонам
MPT_TEST(model_id_scheme) {
  Project doc = makeDoc();
  std::string id1 = doc.addNeuron(mk(NType::Hidden, "V1"))->id;
  std::string id2 = doc.addNeuron(mk(NType::Hidden, "V1"))->id;
  std::string id3 = doc.addNeuron(mk(NType::Sensor, "V1"))->id;
  std::string id4 = doc.addNeuron(mk(NType::Hidden, "W1"))->id;
  CHECK(id1 != id2 && id3 != id1 && id4 != id1, "unique ids");
  CHECK(id1.find("V1.") == 0, "id prefixed by zone code");
  CHECK(id3.find(".S.") != std::string::npos, "sensor type code in id");
  CHECK(id4.find("W1.") == 0, "zone part of id");
  // R5: recomputeIdCounters восстанавливает счётчики как MAX существующих
  doc.neuron(id2)->id = "V1.H.099";
  doc.recomputeIdCounters();
  std::string id5 = doc.nextNeuronId("V1", NType::Hidden, "H");
  CHECK(id5 == "V1.H.100", "counter recomputed as max-of-existing");
}

// schemaHash (9.3): структурный, без динамики
MPT_TEST(model_schema_hash) {
  Project doc = makeDoc();
  Neuron* a = doc.addNeuron(mk(NType::Hidden, "V1"));
  std::string h1 = schemaHash(doc);
  doc.neuron(a->id)->u = 555;              // динамика не меняет хеш
  doc.neuron(a->id)->spiked = true;
  CHECK(schemaHash(doc) == h1, "dynamics not in schema hash");
  doc.neuron(a->id)->thr = OptF(2.0);      // параметр меняет хеш
  CHECK(schemaHash(doc) != h1, "param change changes hash");
}
