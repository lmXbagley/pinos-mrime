#include "mp/serialize.hpp"
// mp/serialize.cpp — Save/Load ВСЕХ полей (R5): включая refrMax, disabled/byCode,
// счётчики ID (1.1), проектные параметры, стенды. После load: recomputeIdCounters().
#include "mp/json.hpp"

#include <cstdlib>

namespace mp {

template <class T>
static Json optJson(const Opt<T>& o) {
  Json j = Json::object();
  j.set("set", Json::boolean(o.set));
  if (o.set) j.set("v", Json::number((double)o.v));
  return j;
}
template <class T>
static void optFromJson(const Json& j, Opt<T>& o) {
  o.set = j.has("set") && j.at("set").asBool();
  if (o.set) {
    if constexpr (std::is_integral_v<T>) o.v = (T)j.at("v").asInt();
    else o.v = j.at("v").asNum();
  }
}

static const char* tname(NType t) { return ntypeName(t); }
static NType tfrom(const std::string& s) {
  if (s == "sensor") return NType::Sensor;
  if (s == "motor") return NType::Motor;
  if (s == "modulator") return NType::Modulator;
  if (s == "homeostat") return NType::Homeostat;
  return NType::Hidden;
}
static const char* mname(NModel m) { return m == NModel::ALIF ? "alif" : "lif"; }
static const char* srname(SensorSource s) {
  switch (s) {
    case SensorSource::API: return "api";
    case SensorSource::Pin: return "pin";
    case SensorSource::Camera: return "camera";
    case SensorSource::Mic: return "mic";
    case SensorSource::Touch: return "touch";
    default: return "manual";
  }
}
static const char* ename(SensorEncoding e) {
  return e == SensorEncoding::EventDiff ? "event_diff"
         : e == SensorEncoding::EventOnOff ? "event_onoff" : "rate";
}
static const char* aname(Actuator a) {
  return a == Actuator::ServoR ? "servo_R" : a == Actuator::Voice ? "voice" : "servo_L";
}
static const char* cname(Curve c) { return c == Curve::Gamma ? "gamma" : "linear"; }
static const char* mkname(ModKind k) {
  return k == ModKind::Punishment ? "punishment" : k == ModKind::Novelty ? "novelty" : "dopamine";
}
static const char* msname(ModScope s) {
  return s == ModScope::Zone ? "zone" : s == ModScope::Zones ? "zones" : "global";
}
static const char* signName(SSign s) { return s == SSign::Inh ? "inh" : "exc"; }
static const char* shname(Shape s) {
  return s == Shape::Grid ? "grid" : s == Shape::Box ? "box" : "sphere";
}
static const char* topname(Topo t) {
  switch (t) {
    case Topo::Grid: return "grid";
    case Topo::Random: return "random";
    case Topo::KNN: return "knn";
    case Topo::Full: return "full";
  }
  return "knn";
}

std::string saveProjectJson(const Project& p) {
  Json root = Json::object();
  root.set("magic", Json::str("minosprime"));
  root.set("v", Json::number(2));

  Json pj = Json::object();
  pj.set("name", Json::str(p.name));
  pj.set("tickBudget", Json::number((double)p.tickBudget));
  pj.set("excNormLimit", Json::number(p.excNormLimit));
  pj.set("tickHz", Json::number(p.tickHz));
  pj.set("gammaConst", Json::number(p.gammaConst));
  pj.set("uRest", Json::number(p.uRest));
  root.set("project", pj);

  Json zones = Json::array();
  for (const auto& z : p.zones) {
    Json j = Json::object();
    j.set("code", Json::str(z.code));
    j.set("color", Json::str(z.color));
    j.set("note", Json::str(z.note));
    j.set("byCode", Json::boolean(z.byCode));
    zones.push_back(j);
  }
  root.set("zones", zones);

  Json neurons = Json::array();
  for (const auto& n : p.neurons) {
    Json j = Json::object();
    j.set("id", Json::str(n.id));
    j.set("label", Json::str(n.label));
    j.set("type", Json::str(n.type.set ? tname((NType)n.type.v) : ""));
    j.set("type_set", Json::boolean(n.type.set));
    j.set("zone", Json::str(n.zone));
    Json pos = Json::array();
    pos.push_back(Json::number(n.pos.x));
    pos.push_back(Json::number(n.pos.y));
    pos.push_back(Json::number(n.pos.z));
    j.set("pos", pos);
    j.set("model", Json::str(n.model.set ? mname((NModel)n.model.v) : ""));
    j.set("model_set", Json::boolean(n.model.set));
    j.set("thr", optJson(n.thr));
    j.set("leak", optJson(n.leak));
    j.set("refrMax", optJson(n.refrMax)); // R5
    j.set("noise", optJson(n.noise));
    j.set("trace", optJson(n.trace));
    j.set("homeoOn", Json::boolean(n.homeoOn));
    j.set("homeoRate", optJson(n.homeoRate));
    Json growth = Json::array();
    for (const auto& g : n.growth) growth.push_back(Json::str(g));
    j.set("growth", growth);
    j.set("color", Json::str(n.color));
    j.set("note", Json::str(n.note));
    j.set("tag", Json::str(n.tag));
    j.set("src", (n.src.set ? Json::str(srname((SensorSource)n.src.v)) : Json::nul()));
    j.set("enc", (n.enc.set ? Json::str(ename((SensorEncoding)n.enc.v)) : Json::nul()));
    j.set("fireThr", optJson(n.fireThr));
    j.set("hyst", optJson(n.hyst));
    j.set("normMin", optJson(n.normMin));
    j.set("normMax", optJson(n.normMax));
    j.set("smoothMs", optJson(n.smoothMs));
    j.set("act", (n.act.set ? Json::str(aname((Actuator)n.act.v)) : Json::nul()));
    j.set("mapMin", optJson(n.mapMin));
    j.set("mapMax", optJson(n.mapMax));
    j.set("mapSpeed", optJson(n.mapSpeed));
    j.set("inv", Json::boolean(n.inv));
    j.set("curve", (n.curve.set ? Json::str(cname((Curve)n.curve.v)) : Json::nul()));
    j.set("gamma", optJson(n.gamma));
    j.set("mkind", (n.mkind.set ? Json::str(mkname((ModKind)n.mkind.v)) : Json::nul()));
    j.set("mscope", (n.mscope.set ? Json::str(msname((ModScope)n.mscope.v)) : Json::nul()));
    Json mzl = Json::array();
    for (const auto& z : n.mzonelist) mzl.push_back(Json::str(z));
    j.set("mzonelist", mzl);
    j.set("windowSec", optJson(n.windowSec));
    j.set("rate", optJson(n.rate));
    j.set("target", optJson(n.target));
    j.set("integSec", optJson(n.integSec));
    j.set("kThr", optJson(n.kThr));
    j.set("kPlast", optJson(n.kPlast));
    j.set("disabled", Json::boolean(n.disabled)); // R5
    j.set("byCode", Json::boolean(n.byCode));     // R2
    j.set("srcCluster", Json::str(n.srcCluster));
    neurons.push_back(j);
  }
  root.set("neurons", neurons);

  Json syns = Json::array();
  for (const auto& s : p.synapses) {
    Json j = Json::object();
    j.set("id", Json::str(s.id));
    j.set("pre", Json::str(s.pre));   // (2.1)
    j.set("post", Json::str(s.post));
    j.set("weight", optJson(s.weight));
    j.set("sign", Json::str(s.sign.set ? signName((SSign)s.sign.v) : ""));
    j.set("plast", Json::str(s.plast.set ? plasticityName((Plasticity)s.plast.v) : ""));
    j.set("lr", optJson(s.lr));
    j.set("pw", optJson(s.pw));
    j.set("wMin", optJson(s.wMin));
    j.set("wMax", optJson(s.wMax));
    j.set("decay", optJson(s.decay));
    j.set("delay", optJson(s.delay));
    j.set("enabled", Json::boolean(s.enabled)); // (2.10), R5
    j.set("color", Json::str(s.color));         // (2.11)
    j.set("note", Json::str(s.note));
    j.set("byCode", Json::boolean(s.byCode));
    syns.push_back(j);
  }
  root.set("synapses", syns);

  Json clus = Json::array();
  for (const auto& c : p.clusters) {
    Json j = Json::object();
    j.set("id", Json::str(c.id));
    j.set("name", Json::str(c.name));
    j.set("zone", Json::str(c.zone));
    j.set("shape", Json::str(c.shape.set ? shname((Shape)c.shape.v) : ""));
    Json cn = Json::array();
    cn.push_back(Json::number(c.center.x));
    cn.push_back(Json::number(c.center.y));
    cn.push_back(Json::number(c.center.z));
    j.set("center", cn);
    j.set("radius", optJson(c.radius));
    j.set("count", optJson(c.count));
    j.set("topo", Json::str(c.topo.set ? topname((Topo)c.topo.v) : ""));
    j.set("knnK", optJson(c.knnK));
    j.set("density", optJson(c.density));
    j.set("thrVar", optJson(c.thrVar));
    j.set("leakVar", optJson(c.leakVar));
    j.set("wVar", optJson(c.wVar));
    j.set("seed", optJson(c.seed));
    j.set("neuroProb", optJson(c.neuroProb));
    j.set("pruneProb", optJson(c.pruneProb));
    j.set("growAll", Json::boolean(c.growAll));
    Json gz = Json::array();
    for (const auto& z : c.growZones) gz.push_back(Json::str(z));
    j.set("growZones", gz);
    j.set("byCode", Json::boolean(c.byCode));
    Json mem = Json::array();
    for (const auto& m : c.members) mem.push_back(Json::str(m));
    j.set("members", mem);
    clus.push_back(j);
  }
  root.set("clusters", clus);

  Json stands = Json::array();
  for (const auto& s : p.stands) {
    Json j = Json::object();
    j.set("name", Json::str(s.name));
    j.set("schemaHash", Json::str(s.schemaHash));
    j.set("resultHash", Json::str(s.resultHash));
    j.set("recSpikesPerSec", Json::number(s.recSpikesPerSec));
    j.set("recSpikesTotal", Json::number((double)s.recSpikesTotal));
    j.set("durationSec", Json::number(s.durationSec));
    j.set("speed", Json::number(s.speed));
    Json ev = Json::array();
    for (const auto& e : s.events) {
      Json x = Json::object();
      x.set("t", Json::number(e.t));
      x.set("api", Json::boolean(e.api));
      x.set("path", Json::str(e.path));
      x.set("neuronId", Json::str(e.neuronId));
      x.set("v", Json::number(e.v));
      ev.push_back(x);
    }
    j.set("events", ev);
    stands.push_back(j);
  }
  root.set("stands", stands);

  Json counters = Json::object(); // R5: счётчики ID сохраняются
  for (const auto& kv : p.idCounter)
    counters.set(kv.first, Json::number((double)kv.second));
  root.set("idCounter", counters);
  root.set("synCounter", Json::number((double)p.synCounter));
  root.set("clustCounter", Json::number((double)p.clustCounter));
  root.set("dirty", Json::boolean(p.dirty));
  return root.dump(2);
}

// ───────────── загрузка ─────────────
static void optNumFrom(const Json& j, OptF& o) { optFromJson(j, o); }
static void optLLFrom(const Json& j, OptI& o) { optFromJson(j, o); }

static NType typeFrom(const std::string& s) { return tfrom(s); }
static NModel modelFrom(const std::string& s) { return s == "alif" ? NModel::ALIF : NModel::LIF; }
static SensorSource srcFrom(const std::string& s) {
  if (s == "api") return SensorSource::API;
  if (s == "pin") return SensorSource::Pin;
  if (s == "camera") return SensorSource::Camera;
  if (s == "mic") return SensorSource::Mic;
  if (s == "touch") return SensorSource::Touch;
  return SensorSource::Manual;
}
static SensorEncoding encFrom(const std::string& s) {
  return s == "event_diff" ? SensorEncoding::EventDiff : s == "event_onoff" ? SensorEncoding::EventOnOff : SensorEncoding::Rate;
}
static Actuator actFrom(const std::string& s) {
  return s == "servo_R" ? Actuator::ServoR : s == "voice" ? Actuator::Voice : Actuator::ServoL;
}
static Curve curveFrom(const std::string& s) { return s == "gamma" ? Curve::Gamma : Curve::Linear; }
static ModKind mkFrom(const std::string& s) {
  return s == "punishment" ? ModKind::Punishment : s == "novelty" ? ModKind::Novelty : ModKind::Dopamine;
}
static ModScope msFrom(const std::string& s) {
  return s == "zone" ? ModScope::Zone : s == "zones" ? ModScope::Zones : ModScope::Global;
}
static SSign signFrom(const std::string& s) { return s == "inh" ? SSign::Inh : SSign::Exc; }
static Plasticity plFrom(const std::string& s) {
  return s == "hebb" ? Plasticity::Hebb : s == "reward" ? Plasticity::RewardGated
         : s == "antihebb" ? Plasticity::AntiHebb : Plasticity::Rigid;
}
static Shape shFrom(const std::string& s) {
  return s == "grid" ? Shape::Grid : s == "box" ? Shape::Box : Shape::Sphere;
}
static Topo topFrom(const std::string& s) {
  return s == "grid" ? Topo::Grid : s == "random" ? Topo::Random : s == "full" ? Topo::Full : Topo::KNN;
}

bool loadProjectJson(const std::string& text, Project& out, std::string& err) {
  Json root;
  int line;
  if (!Json::parse(text, root, err, line)) {
    err = "JSON: " + err;
    return false;
  }
  if (!root.isObj() || !root.at("magic").isStr() || root.at("magic").asStr() != "minosprime") {
    err = "не MINOSPRIME-файл";
    return false;
  }
  Project p;
  const Json& pj = root.at("project");
  p.name = pj.at("name").asStr();
  p.tickBudget = pj.at("tickBudget").asInt(200000);
  p.excNormLimit = pj.at("excNormLimit").asNum(8.0);
  p.tickHz = pj.at("tickHz").asNum(1000);
  p.gammaConst = pj.at("gammaConst").asNum(0.05);
  p.uRest = pj.at("uRest").asNum(0);

  for (size_t i = 0; i < root.at("zones").size(); ++i) {
    const Json& j = root.at("zones").at(i);
    Zone z;
    z.code = j.at("code").asStr();
    z.color = j.at("color").asStr();
    z.note = j.at("note").asStr();
    z.byCode = j.at("byCode").asBool();
    p.zones.push_back(z);
  }
  for (size_t i = 0; i < root.at("neurons").size(); ++i) {
    const Json& j = root.at("neurons").at(i);
    Neuron n;
    n.id = j.at("id").asStr();
    n.label = j.at("label").asStr();
    n.type.set = j.at("type_set").asBool();
    if (n.type.set) n.type.v = (long long)typeFrom(j.at("type").asStr());
    n.zone = j.at("zone").asStr();
    const Json& pos = j.at("pos");
    if (pos.size() == 3) {
      n.pos.x = pos.at(0).asNum();
      n.pos.y = pos.at(1).asNum();
      n.pos.z = pos.at(2).asNum();
    }
    n.model.set = j.at("model_set").asBool();
    if (n.model.set) n.model.v = (long long)modelFrom(j.at("model").asStr());
    optNumFrom(j.at("thr"), n.thr);
    optNumFrom(j.at("leak"), n.leak);
    optLLFrom(j.at("refrMax"), n.refrMax); // R5
    optNumFrom(j.at("noise"), n.noise);
    optLLFrom(j.at("trace"), n.trace);
    n.homeoOn = j.at("homeoOn").asBool();
    optNumFrom(j.at("homeoRate"), n.homeoRate);
    for (size_t g = 0; g < j.at("growth").size(); ++g)
      n.growth.push_back(j.at("growth").at(g).asStr());
    n.color = j.at("color").asStr();
    n.note = j.at("note").asStr();
    n.tag = j.at("tag").asStr();
    if (j.at("src").isStr()) { n.src.set = true; n.src.v = (long long)srcFrom(j.at("src").asStr()); }
    if (j.at("enc").isStr()) { n.enc.set = true; n.enc.v = (long long)encFrom(j.at("enc").asStr()); }
    optNumFrom(j.at("fireThr"), n.fireThr);
    optNumFrom(j.at("hyst"), n.hyst);
    optNumFrom(j.at("normMin"), n.normMin);
    optNumFrom(j.at("normMax"), n.normMax);
    optLLFrom(j.at("smoothMs"), n.smoothMs);
    if (j.at("act").isStr()) { n.act.set = true; n.act.v = (long long)actFrom(j.at("act").asStr()); }
    optNumFrom(j.at("mapMin"), n.mapMin);
    optNumFrom(j.at("mapMax"), n.mapMax);
    optNumFrom(j.at("mapSpeed"), n.mapSpeed);
    n.inv = j.at("inv").asBool();
    if (j.at("curve").isStr()) { n.curve.set = true; n.curve.v = (long long)curveFrom(j.at("curve").asStr()); }
    optNumFrom(j.at("gamma"), n.gamma);
    if (j.at("mkind").isStr()) { n.mkind.set = true; n.mkind.v = (long long)mkFrom(j.at("mkind").asStr()); }
    if (j.at("mscope").isStr()) { n.mscope.set = true; n.mscope.v = (long long)msFrom(j.at("mscope").asStr()); }
    for (size_t g = 0; g < j.at("mzonelist").size(); ++g)
      n.mzonelist.push_back(j.at("mzonelist").at(g).asStr());
    optNumFrom(j.at("windowSec"), n.windowSec);
    optNumFrom(j.at("rate"), n.rate);
    optNumFrom(j.at("target"), n.target);
    optNumFrom(j.at("integSec"), n.integSec);
    optNumFrom(j.at("kThr"), n.kThr);
    optNumFrom(j.at("kPlast"), n.kPlast);
    n.disabled = j.at("disabled").asBool(); // R5
    n.byCode = j.at("byCode").asBool();
    n.srcCluster = j.at("srcCluster").asStr();
    p.neurons.push_back(n);
  }
  for (size_t i = 0; i < root.at("synapses").size(); ++i) {
    const Json& j = root.at("synapses").at(i);
    Synapse s;
    s.id = j.at("id").asStr();
    s.pre = j.at("pre").asStr();
    s.post = j.at("post").asStr();
    optNumFrom(j.at("weight"), s.weight);
    if (j.at("sign").isStr()) { s.sign.set = true; s.sign.v = (long long)signFrom(j.at("sign").asStr()); }
    if (j.at("plast").isStr()) { s.plast.set = true; s.plast.v = (long long)plFrom(j.at("plast").asStr()); }
    optNumFrom(j.at("lr"), s.lr);
    optLLFrom(j.at("pw"), s.pw);
    optNumFrom(j.at("wMin"), s.wMin);
    optNumFrom(j.at("wMax"), s.wMax);
    optNumFrom(j.at("decay"), s.decay);
    optLLFrom(j.at("delay"), s.delay);
    s.enabled = j.at("enabled").asBool(true);
    s.color = j.at("color").asStr();
    s.note = j.at("note").asStr();
    s.byCode = j.at("byCode").asBool();
    p.synapses.push_back(s);
  }
  for (size_t i = 0; i < root.at("clusters").size(); ++i) {
    const Json& j = root.at("clusters").at(i);
    Cluster c;
    c.id = j.at("id").asStr();
    c.name = j.at("name").asStr();
    c.zone = j.at("zone").asStr();
    if (j.at("shape").isStr()) { c.shape.set = true; c.shape.v = (long long)shFrom(j.at("shape").asStr()); }
    const Json& cn = j.at("center");
    if (cn.size() == 3) {
      c.center.x = cn.at(0).asNum();
      c.center.y = cn.at(1).asNum();
      c.center.z = cn.at(2).asNum();
    }
    optNumFrom(j.at("radius"), c.radius);
    optLLFrom(j.at("count"), c.count);
    if (j.at("topo").isStr()) { c.topo.set = true; c.topo.v = (long long)topFrom(j.at("topo").asStr()); }
    optLLFrom(j.at("knnK"), c.knnK);
    optNumFrom(j.at("density"), c.density);
    optNumFrom(j.at("thrVar"), c.thrVar);
    optNumFrom(j.at("leakVar"), c.leakVar);
    optNumFrom(j.at("wVar"), c.wVar);
    optLLFrom(j.at("seed"), c.seed);
    optNumFrom(j.at("neuroProb"), c.neuroProb);
    optNumFrom(j.at("pruneProb"), c.pruneProb);
    c.growAll = j.at("growAll").asBool(true);
    for (size_t g = 0; g < j.at("growZones").size(); ++g)
      c.growZones.push_back(j.at("growZones").at(g).asStr());
    c.byCode = j.at("byCode").asBool();
    for (size_t g = 0; g < j.at("members").size(); ++g)
      c.members.push_back(j.at("members").at(g).asStr());
    p.clusters.push_back(c);
  }
  for (size_t i = 0; i < root.at("stands").size(); ++i) {
    const Json& j = root.at("stands").at(i);
    Stand s;
    s.name = j.at("name").asStr();
    s.schemaHash = j.at("schemaHash").asStr();
    s.resultHash = j.at("resultHash").asStr();
    s.recSpikesPerSec = j.at("recSpikesPerSec").asNum();
    s.recSpikesTotal = j.at("recSpikesTotal").asInt();
    s.durationSec = j.at("durationSec").asNum(5.0);
    s.speed = j.at("speed").asNum(1.0);
    for (size_t g = 0; g < j.at("events").size(); ++g) {
      const Json& e = j.at("events").at(g);
      StandEvent ev;
      ev.t = e.at("t").asNum();
      ev.api = e.at("api").asBool();
      ev.path = e.at("path").asStr();
      ev.neuronId = e.at("neuronId").asStr();
      ev.v = e.at("v").asNum();
      s.events.push_back(ev);
    }
    p.stands.push_back(s);
  }
  const Json& counters = root.at("idCounter"); // R5
  if (counters.isObj())
    for (const auto& k : counters.keys())
      p.idCounter[k] = counters.at(k).asInt(0);
  p.synCounter = root.at("synCounter").asInt(0);
  p.clustCounter = root.at("clustCounter").asInt(0);
  p.dirty = root.at("dirty").asBool();

  out = std::move(p);
  out.recomputeIdCounters(); // R4/R5: подтвердить счётчики фактическим MAX
  return true;
}

} // namespace mp
