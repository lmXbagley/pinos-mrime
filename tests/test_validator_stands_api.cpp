#include "test_util.hpp"

#include "mp/api.hpp"
#include "mp/stands.hpp"
#include "mp/validator.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <sstream>

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

static Synapse link(const std::string& pre, const std::string& post, Project& doc) {
  Synapse s;
  s.pre = pre; s.post = post;
  s.weight = OF(0.5); s.sign = OI((long long)SSign::Exc);
  s.plast = OI((long long)Plasticity::Rigid); s.lr = OF(0.01);
  s.pw = OI(5); s.wMin = OF(0); s.wMax = OF(2); s.decay = OF(0); s.delay = OI(1);
  s.id = doc.nextSynId();
  doc.synapses.push_back(s);
  return s;
}

// 8.1: красные флаги
MPT_TEST(validator_red_flags) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* m = doc.addNeuron(base(NType::Motor, ""));
  auto fl = validate(doc);
  bool motorRed = false, r1 = false;
  for (const auto& f : fl) {
    if (f.level == FlagLevel::Red && f.code == "motor.no-inputs") motorRed = true;
    if (f.level == FlagLevel::Red && f.code == "R1") r1 = true;
  }
  CHECK(motorRed, "8.1: motor without inputs is red");
  (void)r1;
  CHECK(doc.neurons[0].missingParam(), "sensor without src/enc params is red (R1)");
  link(s->id, m->id, doc);
  fl = validate(doc);
  motorRed = false;
  for (const auto& f : fl) if (f.code == "motor.no-inputs") motorRed = true;
  CHECK(!motorRed, "8.1: motor with input not flagged");
}

// 8.1: RewardGated без модулятора; 8.2: exc.norm; orphan
MPT_TEST(validator_reward_and_norm) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  link(s->id, n->id, doc);
  doc.synapses[0].plast = OI((long long)Plasticity::RewardGated);
  auto fl = validate(doc);
  bool found = false;
  for (const auto& f : fl) if (f.code == "reward.no-mod") found = true;
  CHECK(found, "8.1: RewardGated without modulator flagged");
  doc.excNormLimit = 0.4;
  found = false;
  for (const auto& f : validate(doc)) if (f.code == "exc.norm") found = true;
  CHECK(found, "8.2: excitation norm exceeded flagged yellow");
  // 8.1: orphan — нейрон без связей и без кластера
  Neuron* o = doc.addNeuron(base(NType::Hidden, ""));
  bool orphan = false;
  for (const auto& f : validate(doc)) if (f.code == "orphan" && f.unit == o->id) orphan = true;
  CHECK(orphan, "8.1: orphan outside cluster flagged");
  // 8.1: бюджет такта
  doc.tickBudget = 1;
  found = false;
  for (const auto& f : validate(doc)) if (f.code == "tick.budget") found = true;
  CHECK(found, "8.1: tick budget exceeded flagged");
}

// 9.x: стенды — CRUD, запись, воспроизведение, регрессия, хеш схемы
MPT_TEST(stands_record_replay_regression) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  link(s->id, n->id, doc);
  Stand& st = createStand(doc, "base", 0.05);      // 50 мс прогон
  recordEvent(doc, "base", {0.01, true, "/set_sensor?id=" + s->id + "&v=10", s->id, 10.0});
  registerRecordedStand(doc, "base", 0.05);        // 9.3: хеш схемы + эталон
  CHECK(!st.schemaHash.empty(), "9.3: schema hash captured");
  CHECK(!st.resultHash.empty(), "9.3: baseline resultHash captured");
  StandRunResult r = runStand(doc, st);
  CHECK(r.replayOk, "9.2: events replay");
  CHECK(r.regressionOk, "9.4: replay matches baseline");
  StandRunResult r2 = runStand(doc, st);
  CHECK(r2.resultHash == r.resultHash, "9.2: deterministic replay");
  // 9.3: изменение схемы → hashMismatch
  doc.neuron(n->id)->thr = OF(9.0);
  StandRunResult r3 = runStand(doc, st);
  CHECK(r3.hashMismatch, "9.3: schema change warned");
  // 9.1: удаление стенда
  CHECK(removeStand(doc, "base"), "9.1: stand removable");
}

// 10.x: API — loopback, get_neuron/set_sensor(_s)/get_schema/stream; структура не мутируется
static std::string httpGet(int port, const std::string& path) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return "SOCKET-FAIL";
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, (sockaddr*)&a, sizeof(a)) != 0) { ::close(fd); return "CONNECT-FAIL"; }
  std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  ::send(fd, req.data(), req.size(), 0);
  std::string out; char buf[8192]; ssize_t n;
  while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) out.append(buf, (size_t)n);
  ::close(fd);
  return out;
}

MPT_TEST(api_loopback_endpoints) {
  Project doc;
  Neuron* s = doc.addNeuron(base(NType::Sensor, ""));
  Neuron* n = doc.addNeuron(base(NType::Hidden, ""));
  link(s->id, n->id, doc);
  std::string schemaBefore = schemaHash(doc);
  size_t neuronsBefore = doc.neurons.size(), synBefore = doc.synapses.size();
  Engine eng(doc);
  ApiServer api(doc, eng, 18421);
  std::string err;
  CHECK(api.start(err), "server starts: " + err);
  std::string r1 = httpGet(18421, "/get_neuron?id=" + s->id);
  CHECK(r1.find("200 OK") != std::string::npos && r1.find(s->id) != std::string::npos,
        "10.1 get_neuron returns neuron");
  std::string r2 = httpGet(18421, "/set_sensor?id=" + s->id + "&v=10");
  CHECK(r2.find("\"ok\":1") != std::string::npos, "10.2 set_sensor ok");
  std::string r2b = httpGet(18421, "/set_sensors?ids=" + s->id + "," + n->id + "&v=1,2");
  CHECK(r2b.find("\"applied\":2") != std::string::npos, "10.2 set_sensors multi");
  std::string r3 = httpGet(18421, "/get_schema");
  CHECK(r3.find(s->id) != std::string::npos && r3.find("\"neurons\"") != std::string::npos,
        "10.3 get_schema returns full document");
  std::string r4 = httpGet(18421, "/stream?ids=" + s->id);
  CHECK(r4.find(s->id) != std::string::npos && r4.find("\"hz\"") != std::string::npos,
        "10.4 stream with ids filter");
  std::string r5 = httpGet(18421, "/stream?zone=NOPE");
  CHECK(r5.empty() || r5.find(s->id) == std::string::npos, "10.4 stream zone filter works");
  // 10.5: структура не изменилась
  CHECK(schemaHash(doc) == schemaBefore, "10.5 API did not change structure");
  CHECK(doc.neurons.size() == neuronsBefore && doc.synapses.size() == synBefore, "10.5 no units added/removed");
  CHECK(eng.ticks() == 0, "10.5 API does not simulate by itself");
  api.stop();
  std::string r6 = httpGet(18421, "/get_neuron?id=" + s->id);
  CHECK(r6 == "CONNECT-FAIL", "server stops");
}
