// gui/app.cpp — логика приложения (7.x файлы, 9.x стенды, 10.x API, 4.x сборка).
#include "app.hpp"

#include "../mp/serialize.hpp"

#include "panels.hpp"
#include "scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>

namespace mpui {

static std::string homeDir() {
  const char* h = getenv("HOME");
  return h ? std::string(h) : std::string(".");
}

static bool fileExists(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string dirOf(const std::string& p) {
  size_t i = p.find_last_of('/');
  if (i == std::string::npos) return std::string(".");
  return p.substr(0, i);
}

static void loadRecent(std::vector<std::string>& out) {
  out.clear();
  std::ifstream f(homeDir() + "/.minosprime/recent.txt");
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty()) out.push_back(line);
    if (out.size() >= 8) break;
  }
}

static void saveRecent(const std::vector<std::string>& v) {
  std::string d = homeDir() + "/.minosprime";
  ::mkdir(d.c_str(), 0755);
  std::ofstream f(d + "/recent.txt");
  for (const auto& s : v) f << s << "\n";
}

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Select: return "выбор";
    case Mode::Neuron: return "нейрон";
    case Mode::Wire:   return "связь";
    case Mode::Cut:    return "вы cut";
    case Mode::Damage: return "повреждение";
  }
  return "?";
}

// ------------------------------------------------------------------

void App::startup(const char* openArg) {
  loadRecent(recent);
  if (openArg && *openArg && fileExists(openArg)) {
    openFile(openArg);
  } else {
    newDocPrep();
    loadRecent(recent);
    statusMsg = "новый проект";
  }
  fitView();
}

void App::fitView() {
  if (doc.neurons.empty()) {
    cam.target = {0, 0, 0};
    cam.dist = 60;
    cam.yaw = 0.8f;
    cam.pitch = 0.5f;
    return;
  }
  double mnx = 1e30, mny = 1e30, mnz = 1e30, mxx = -1e30, mxy = -1e30, mxz = -1e30;
  for (const auto& n : doc.neurons) {
    mnx = std::min(mnx, n.pos.x); mxx = std::max(mxx, n.pos.x);
    mny = std::min(mny, n.pos.y); mxy = std::max(mxy, n.pos.y);
    mnz = std::min(mnz, n.pos.z); mxz = std::max(mxz, n.pos.z);
  }
  for (const auto& c : doc.clusters) {
    double r = c.radius.set ? c.radius.v : 8.0;
    mnx = std::min(mnx, c.center.x - r); mxx = std::max(mxx, c.center.x + r);
    mny = std::min(mny, c.center.y - r); mxy = std::max(mxy, c.center.y + r);
    mnz = std::min(mnz, c.center.z - r); mxz = std::max(mxz, c.center.z + r);
  }
  cam.target = {(mnx + mxx) / 2, (mny + mxy) / 2, (mnz + mxz) / 2};
  double ex = mxx - mnx, ey = mxy - mny, ez = mxz - mnz;
  double rad = std::sqrt(ex * ex + ey * ey + ez * ez) / 2;
  cam.dist = (float)std::max(20.0, rad * 2.4 + 14.0);
  cam.yaw = 0.8f;
  cam.pitch = 0.5f;
  cam.flying = false;
}

void App::update(double dt) {
  now += dt;
  if (dt > 0) fps = fps * 0.9 + (1.0 / dt) * 0.1;
  frameDt = dt;

  // симуляция (8.1): budget по суммарным тактам, ограничение на кадр от фриза UI
  if (running) {
    if (eng.ticks() >= (long long)doc.tickBudget) {
      running = false;
      statusMsg = "достигнут бюджет тактов (8.1)";
    } else {
      tickAcc += dt * speed * doc.tickHz;
      long long want = (long long)std::floor(tickAcc);
      long long cap = 20000;
      long long run = std::min(want, cap);
      if (run > 0) {
        tickAcc -= (double)run;
        long long left = (long long)doc.tickBudget - eng.ticks();
        for (long long i = 0; i < run && i < left; ++i) eng.tick();
      }
    }
  }

  // 6.3: импульсы — view-layer по rising edge spiked
  for (const auto& n : doc.neurons) {
    bool sp = n.spiked && n.alive && !n.disabled;
    bool was = false;
    auto it = lastSpiked.find(n.id);
    if (it != lastSpiked.end()) was = it->second;
    if (sp && !was) {
      flash[n.id] = 1.0;
      for (const auto& s : doc.synapses) {
        if (s.pre == n.id && s.enabled)
          impulses.push_back(Imp{s.id, s.pre, s.post, 0.0, s.w});
      }
    }
  }
  for (const auto& n : doc.neurons) lastSpiked[n.id] = n.spiked && n.alive && !n.disabled;
  double decay = std::exp(-dt * 2.5);
  for (auto& kv : flash) kv.second *= decay;
  for (auto& im : impulses) im.t += dt * 3.0;
  impulses.erase(std::remove_if(impulses.begin(), impulses.end(),
                                [](const Imp& i) { return i.t >= 1.0; }),
                 impulses.end());

  // автосохранение (7.5): каждые 30 c, если есть изменения
  if (doc.dirty) {
    autosaveT += dt;
    if (autosaveT >= 30.0) {
      autosaveT = 0;
      std::string p = filePath.empty() ? "autosave.mnp.json" : dirOf(filePath) + "/autosave.mnp.json";
      std::ofstream f(p);
      if (f) {
        f << mp::saveProjectJson(doc);
        statusMsg = "автосохранение: " + p;
        lastAutosaveW = now;
      }
    }
  } else {
    autosaveT = 0;
  }

  cam.tickFly(dt);
}

void App::drawUI() { panelsDraw(*this); }

bool App::undoPush(const char* label) {
  undo.push(doc, label);
  return true;
}

void App::revalidate() { flags = mp::validate(doc); }

void App::structuralChanged() {
  eng.resetDyn();
  flash.clear();
  lastSpiked.clear();
  impulses.clear();
  revalidate();
  doc.dirty = true;
}

void App::openFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    statusMsg = "не открыть: " + path;
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  mp::Project p;
  std::string err;
  if (!mp::loadProjectJson(ss.str(), p, err)) {
    statusMsg = "ошибка загрузки: " + err;
    return;
  }
  doc = std::move(p);
  filePath = path;
  filePathEdit = path;
  eng.resetDyn();
  flash.clear();
  lastSpiked.clear();
  impulses.clear();
  selN.clear();
  selS.clear();
  selCluster.clear();
  focusUnit.clear();
  running = false;
  revalidate();
  addRecent(path);
  undo.push(doc, "открыт файл");
  fitView();
  statusMsg = "открыт: " + path;
}

void App::saveFile(const std::string& path) {
  std::ofstream f(path);
  if (!f) {
    statusMsg = "не записать: " + path;
    return;
  }
  f << mp::saveProjectJson(doc);
  filePath = path;
  filePathEdit = path;
  doc.dirty = false;
  addRecent(path);
  statusMsg = "сохранено: " + path;
}

void App::newDoc() {
  doc = mp::Project();
  structuralChanged();
}

void App::newDocPrep() {
  filePath.clear();
  filePathEdit.clear();
  selN.clear();
  selS.clear();
  selCluster.clear();
  focusUnit.clear();
  hoverN.clear();
  hoverS.clear();
  running = false;
  doc.name = "новый проект";
  newDoc();
}

void App::applyBuild() {
  bool hadUndoSame = undo.canUndo();
  lastBuild = mp::dslBuild(doc, codeText);
  codeErrLine = lastBuild.errors.empty() ? -1 : lastBuild.errors.front().line;
  codeTabErrors = !lastBuild.ok;
  if (lastBuild.ok) {
    undo.push(doc, "код: Сборка");
    structuralChanged();
    char b[96];
    snprintf(b, sizeof b, "код: +%d ~%d -%d", lastBuild.created, lastBuild.updated, lastBuild.removed);
    statusMsg = b;
    fitView();
  } else if (hadUndoSame) {
    revalidate();
  }
}

void App::exportCode() {
  codeText = mp::dslExport(doc);
  codeCursor = (int)codeText.size();
  codeTabErrors = false;
  codeErrLine = -1;
  statusMsg = "код экспортирован из схемы (4.4)";
}

void App::deleteSelection() {
  std::vector<std::string> ns(selN.begin(), selN.end());
  std::vector<std::string> ss(selS.begin(), selS.end());
  if (ns.empty() && ss.empty()) return;
  undoPush("мышь: удалить выделенное");
  for (const auto& id : ns) doc.removeNeuron(id);
  for (const auto& id : ss) doc.removeSynapse(id);
  selN.clear();
  selS.clear();
  structuralChanged();
  statusMsg = "удалено";
}

void App::copySel() {
  clipN.clear();
  clipS.clear();
  for (const auto& n : doc.neurons)
    if (n.alive && selN.count(n.id)) clipN.push_back(n);
  for (const auto& s : doc.synapses)
    if (s.alive && selN.count(s.pre) && selN.count(s.post)) clipS.push_back(s);
  statusMsg = clipN.empty() ? "буфер пуст" : "скопировано: " + std::to_string(clipN.size());
}

void App::pasteClip() {
  if (clipN.empty()) return;
  undoPush("мышь: вставить (5.4)");
  std::map<std::string, std::string> mapping;
  for (const auto& n : clipN) {
    mp::Neuron c = n;
    c.byCode = false;
    c.srcCluster.clear();
    c.pos.x += 3;
    c.pos.z += 3;
    std::string label = n.label.empty() ? std::string("H") : n.label;
    if (!label.empty() && label.back() >= '0' && label.back() <= '9') label.back() = '0';
    c.id.clear();
    std::string zone = c.zone;
    long long tt = 1;
    if (c.type.set && c.type.v >= 0 && c.type.v <= 4) tt = c.type.v;
    c.id = doc.nextNeuronId(zone, (mp::NType)tt, label);
    doc.addNeuron(c);
    mapping[n.id] = c.id;
  }
  for (const auto& s : clipS) {
    mp::Synapse c = s;
    c.byCode = false;
    auto ip = mapping.find(s.pre);
    auto iq = mapping.find(s.post);
    if (ip == mapping.end() || iq == mapping.end()) continue;
    c.pre = ip->second;
    c.post = iq->second;
    c.id = doc.nextSynId();
    doc.synapses.push_back(c);
  }
  selN.clear();
  selS.clear();
  for (const auto& kv : mapping) selN.insert(kv.second);
  structuralChanged();
  statusMsg = "вставлено";
}

void App::toggleDamage(const std::string& id, bool isSyn) {
  undoPush("мышь: повреждение (5.10)");
  if (isSyn) {
    mp::Synapse* s = doc.synapse(id);
    if (s) s->enabled = !s->enabled;
  } else {
    mp::Neuron* n = doc.neuron(id);
    if (n) n->disabled = !n->disabled;
  }
  eng.resetDyn();
  revalidate();
  doc.dirty = true;
  statusMsg = "повреждение переключено (5.10)";
}

void App::addRecent(const std::string& p) {
  if (p.empty()) return;
  std::string abs = p;
  if (!abs.empty() && abs[0] != '/') abs = homeDir() + "/" + abs;
  recent.erase(std::remove(recent.begin(), recent.end(), abs), recent.end());
  recent.insert(recent.begin(), abs);
  if (recent.size() > 8) recent.resize(8);
  saveRecent(recent);
}

void App::fireOnceEvt(const std::string& id) {
  eng.fireOnce(id);
  flash[id] = 1.0;
  if (recording)
    mp::recordEvent(doc, recName, mp::StandEvent{recT, false, std::string(), id, 1.0});
}

void App::setFlowEvt(const std::string& id, double hz) {
  eng.setFlow(id, hz);
  flowHz = hz;
  if (recording)
    mp::recordEvent(doc, recName, mp::StandEvent{recT, false, std::string(), id, hz});
}

void App::fillEvt(const std::string& clusterId, double frac) {
  eng.fillCluster(clusterId, frac);
  if (recording)
    mp::recordEvent(doc, recName, mp::StandEvent{recT, false, std::string(), clusterId, frac});
}

void App::startRecording() {
  recording = true;
  recT = 0;
  if (appStSelValid()) {
    recName = doc.stands[(size_t)stSel].name;
  } else {
    std::vector<bool> used;
    std::set<int> nums;
    for (const auto& s : doc.stands)
      if (s.name.rfind("запись ", 0) == 0) nums.insert(atoi(s.name.c_str() + 7));
    int k = 1;
    while (nums.count(k)) ++k;
    recName = "запись " + std::to_string(k);
    mp::createStand(doc, recName, 5.0);
    stSel = (int)doc.stands.size() - 1;
    undoPush("стенд: начать запись (9.2)");
  }
  statusMsg = "ЗАПИСЬ стенда: " + recName;
}

bool App::appStSelValid() const {
  return stSel >= 0 && stSel < (int)doc.stands.size();
}

void App::stopRecording() {
  recording = false;
  mp::registerRecordedStand(doc, recName, recT);
  statusMsg = "стенд зафиксирован: " + recName;
  doc.dirty = true;
}

void App::newClusterOnPlane() {
  char blk[320];
  snprintf(blk, sizeof blk,
           "zone(\"K\");\n"
           "cluster \"кл\" { zone=\"K\"; shape=\"sphere\"; center=[%.1f, %.1f, %.1f]; "
           "radius=8; count=16; topo=\"knn\"; knnK=6; seed=1; }\n",
           lastPos.x, lastPos.y, lastPos.z);
  std::set<std::string> before;
  for (const auto& c : doc.clusters) before.insert(c.id);
  undoPush("мышь: новый кластер (3.x)");
  codeText += blk;
  applyBuild();
  if (lastBuild.ok) {
    for (const auto& c : doc.clusters)
      if (!before.count(c.id)) selCluster = c.id;
    statusMsg = "кластер создан: " + selCluster;
  }
}

} // namespace mpui
