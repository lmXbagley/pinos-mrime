// gui/inspector.cpp — инспектор: все поля 1.1–1.28 (нейрон), 2.1–2.11 (связь),
// 3.1–3.4 (кластер). Группировка секциями; числа и степперы (2.x), Opt-set/unset (R1).
#include "inspector.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace mpui {

// подписи перечислений (1.2, 1.5, 1.14–1.25, 2.3–2.4, 3.1)
static const std::vector<std::string> kNType = {"сенсор", "скрытый", "мотор", "модулятор", "гомеостат"};
static const std::vector<std::string> kModel = {"LIF", "ALIF"};
static const std::vector<std::string> kSSign = {"возб.", "тормоз"};
static const std::vector<std::string> kPlast = {"жёсткая", "хебб", "награда", "анти-хебб"};
static const std::vector<std::string> kSensorSrc = {"мышь/API", "API", "пин", "камера", "микрофон", "тач"};
static const std::vector<std::string> kEnc = {"частота", "дельта", "on/off"};
static const std::vector<std::string> kAct = {"серво L", "серво R", "голос"};
static const std::vector<std::string> kCurve = {"линейная", "гамма"};
static const std::vector<std::string> kModKind = {"дофамин", "наказание", "новизна"};
static const std::vector<std::string> kModScope = {"глобально", "зона", "список зон"};
static const std::vector<std::string> kShape = {"сфера", "сетка", "бокс"};
static const std::vector<std::string> kTopo = {"сетка", "случайно", "kNN", "полный"};

unsigned gid2(const char* a, const std::string& b) { return hashStr((std::string(a) + "|" + b).c_str()); }

static float numRow(UI& ui, unsigned id, const char* name, Rect r, double& v, double step = 0.05) {
  mp::OptF tmp;
  tmp.set = true;
  tmp.v = v;
  widgetLabel(ui, name, {r.x + 4, r.y + 3, 118, 0}, C_DIM);
  widgetOptF(ui, id, r, tmp, step, -1e9, 1e9);
  v = tmp.v;
  return 23;
}

static float enumRow(UI& ui, unsigned id, const char* name, Rect r, int& idx, const std::vector<std::string>& names) {
  widgetLabel(ui, name, {r.x + 4, r.y + 2, 118, 0}, C_DIM);
  Rect cr{r.x + 124, r.y, r.w - 128, 22};
  widgetEnum(ui, id, cr, idx, names);
  return 25;
}

static float textRow(UI& ui, unsigned id, const char* name, Rect r, std::string& v) {
  widgetLabel(ui, name, {r.x + 4, r.y + 2, 118, 0}, C_DIM);
  Rect cr{r.x + 124, r.y, r.w - 128, 22};
  widgetTextField(ui, id, cr, v, false);
  return 25;
}

static float boolRow(UI& ui, unsigned id, const char* name, Rect r, bool& v) {
  widgetCheckbox(ui, id, {r.x + 4, r.y, r.w - 8, 22}, v, name);
  return 25;
}

static float roRow(UI& ui, const char* name, const std::string& val, Rect r) {
  widgetLabel(ui, name, {r.x + 4, r.y + 3, 118, 0}, C_DIM);
  widgetLabel(ui, val, {r.x + 126, r.y + 3, r.w - 130, 0}, 0xb9c3d0ff);
  return 22;
}

template <class F>
static std::string fmt(F f) {
  char b[48];
  b[0] = 0;
  f(b, sizeof b);
  return std::string(b);
}

static std::string csv(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) s += ",";
    s += v[i];
  }
  return s;
}

static void csvSet(const std::string& gz, std::vector<std::string>& out) {
  out.clear();
  std::string cur;
  for (char c : gz) {
    if (c == ',' || c == ' ') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
}

// ---------- нейрон ----------
static void drawNeuronInspector(App& app, UI& ui, mp::Neuron& nr, Rect r) {
  float y = r.y;
  int t = (int)(nr.type.set ? nr.type.v : 1);
  std::vector<std::string> tn = kNType;
  int t2 = t;
  enumRow(ui, gid2("nt", nr.id), "тип (1.2)", {r.x, y, r.w, 0}, t2, tn);
  if (t2 != t || !nr.type.set) nr.type = t2;
  y += 22;
  y += textRow(ui, gid2("lb", nr.id), "метка (1.1)", {r.x, y, r.w, 0}, nr.label);
  y += textRow(ui, gid2("zn", nr.id), "зона (1.3)", {r.x, y, r.w, 0}, nr.zone);
  double px = nr.pos.x, py = nr.pos.y, pz = nr.pos.z;
  y += numRow(ui, gid2("px", nr.id), "X (1.4)", {r.x, y, r.w, 0}, px, 0.5);
  y += numRow(ui, gid2("py", nr.id), "Y (1.4)", {r.x, y, r.w, 0}, py, 0.5);
  y += numRow(ui, gid2("pz", nr.id), "Z (1.4)", {r.x, y, r.w, 0}, pz, 0.5);
  nr.pos = {px, py, pz};
  y += boolRow(ui, gid2("ds", nr.id), "повреждён (R5/5.10)", {r.x, y, r.w, 0}, nr.disabled);

  y += 8;
  widgetSection(ui, hashStr("sec dyn"), "Динамика (1.5–1.10)", app.sectOpen["dyn"], {r.x, y, r.w, 22});
  y += 22;
  if (app.sectOpen["dyn"]) {
    int m = (int)(nr.model.set ? nr.model.v : 0);
    enumRow(ui, gid2("md", nr.id), "модель (1.5)", {r.x, y, r.w, 0}, m, kModel);
    nr.model = m;
    y += 22;
    widgetLabel(ui, "порог (1.6)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("thr", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.thr, 0.05);
    y += 20;
    widgetLabel(ui, "утечка (1.7)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("lk", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.leak, 0.01, 0, 1);
    y += 20;
    widgetLabel(ui, "рефрак (1.8)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptI(ui, gid2("rf", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.refrMax, 1, 0, 1e6);
    y += 20;
    widgetLabel(ui, "шум (1.9)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("ns", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.noise, 0.01, 0, 1e3);
    y += 20;
    widgetLabel(ui, "след (1.10)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptI(ui, gid2("tr", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.trace, 1, 0, 1e6);
    y += 20;
  }

  y += 8;
  widgetSection(ui, hashStr("sec homeo"), "Гомеостат (1.11, 1.26–1.28)", app.sectOpen["homeo"], {r.x, y, r.w, 22});
  y += 22;
  if (app.sectOpen["homeo"]) {
    y += boolRow(ui, gid2("ho", nr.id), "включён (1.11)", {r.x, y, r.w, 0}, nr.homeoOn);
    widgetLabel(ui, "ставка (1.11)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("hr", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.homeoRate, 0.01, 0, 100);
    y += 20;
    widgetLabel(ui, "цель (1.26)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("tg", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.target, 0.5, 0, 1e6);
    y += 20;
    widgetLabel(ui, "окно сек (1.27)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("is", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.integSec, 0.1, 0, 1e6);
    y += 20;
    widgetLabel(ui, "k порога (1.28)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("kt", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.kThr, 0.01);
    y += 20;
    widgetLabel(ui, "k пласт (1.28)", {r.x + 4, y + 2, 118, 0}, C_DIM);
    widgetOptF(ui, gid2("kp", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.kPlast, 0.01);
    y += 20;
  }

  y += 8;
  widgetSection(ui, hashStr("sec grow"), "Рост (1.12)", app.sectOpen["grow"], {r.x, y, r.w, 22});
  y += 22;
  if (app.sectOpen["grow"]) {
    std::string gz = csv(nr.growth);
    if (textRow(ui, gid2("gz", nr.id), "зоны (csv)", {r.x, y, r.w, 0}, gz)) csvSet(gz, nr.growth);
    y += 22;
  }

  if (nr.type.v == 0) { // сенсор 1.14–1.18
    y += 8;
    widgetSection(ui, hashStr("sec sens"), "Сенсор (1.14–1.18)", app.sectOpen["sens"], {r.x, y, r.w, 22});
    y += 22;
    if (app.sectOpen["sens"]) {
      int src = (int)(nr.src.set ? nr.src.v : 1);
      int src2 = src;
      enumRow(ui, gid2("src", nr.id), "источник (1.14)", {r.x, y, r.w, 0}, src2, kSensorSrc);
      nr.src = src2;
      y += 22;
      int enc = (int)(nr.enc.set ? nr.enc.v : 0);
      int enc2 = enc;
      enumRow(ui, gid2("enc", nr.id), "кодировка (1.15)", {r.x, y, r.w, 0}, enc2, kEnc);
      nr.enc = enc2;
      y += 22;
      widgetLabel(ui, "порог вход (1.16)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("ft", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.fireThr, 0.05);
      y += 20;
      widgetLabel(ui, "гистерезис (1.16)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("hy", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.hyst, 0.01, 0, 1e6);
      y += 20;
      widgetLabel(ui, "норм мин (1.17)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("nmn", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.normMin, 0.1);
      y += 20;
      widgetLabel(ui, "норм макс (1.17)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("nmx", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.normMax, 0.1);
      y += 20;
      widgetLabel(ui, "сглаж мс (1.18)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptI(ui, gid2("sm", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.smoothMs, 10, 0, 1e6);
      y += 20;
    }
  }
  if (nr.type.v == 2) { // мотор 1.19–1.21
    y += 8;
    widgetSection(ui, hashStr("sec motor"), "Мотор (1.19–1.21)", app.sectOpen["motor"], {r.x, y, r.w, 22});
    y += 22;
    if (app.sectOpen["motor"]) {
      int act = (int)(nr.act.set ? nr.act.v : 0);
      int act2 = act;
      enumRow(ui, gid2("ac", nr.id), "актюатор (1.19)", {r.x, y, r.w, 0}, act2, kAct);
      nr.act = act2;
      y += 22;
      widgetLabel(ui, "мапп мин (1.20)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("mmn", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.mapMin, 0.1);
      y += 20;
      widgetLabel(ui, "мапп макс (1.20)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("mmx", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.mapMax, 0.1);
      y += 20;
      widgetLabel(ui, "скорость (1.20)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("msp", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.mapSpeed, 0.1);
      y += 20;
      y += boolRow(ui, gid2("inv", nr.id), "инверсия (1.20)", {r.x, y, r.w, 0}, nr.inv);
      int cv = (int)(nr.curve.set ? nr.curve.v : 0);
      int cv2 = cv;
      enumRow(ui, gid2("cv", nr.id), "кривая (1.21)", {r.x, y, r.w, 0}, cv2, kCurve);
      nr.curve = cv2;
      y += 22;
      widgetLabel(ui, "гамма (1.21)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("gm", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.gamma, 0.01, 0, 1e6);
      y += 20;
    }
  }
  if (nr.type.v == 3) { // модулятор 1.22–1.25
    y += 8;
    widgetSection(ui, hashStr("sec mod"), "Модулятор (1.22–1.25)", app.sectOpen["mod"], {r.x, y, r.w, 22});
    y += 22;
    if (app.sectOpen["mod"]) {
      int mk = (int)(nr.mkind.set ? nr.mkind.v : 0);
      int mk2 = mk;
      enumRow(ui, gid2("mk", nr.id), "тип (1.22)", {r.x, y, r.w, 0}, mk2, kModKind);
      nr.mkind = mk2;
      y += 22;
      int ms = (int)(nr.mscope.set ? nr.mscope.v : 0);
      int ms2 = ms;
      enumRow(ui, gid2("msc", nr.id), "охват (1.23)", {r.x, y, r.w, 0}, ms2, kModScope);
      nr.mscope = ms2;
      y += 22;
      std::string zl = csv(nr.mzonelist);
      if (textRow(ui, gid2("mzl", nr.id), "зоны (csv)", {r.x, y, r.w, 0}, zl)) csvSet(zl, nr.mzonelist);
      y += 22;
      widgetLabel(ui, "окно сек (1.24)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("ws", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.windowSec, 0.1, 0, 1e6);
      y += 20;
      widgetLabel(ui, "ставка (1.25)", {r.x + 4, y + 2, 118, 0}, C_DIM);
      widgetOptF(ui, gid2("rt", nr.id), {r.x + 124, y, r.w - 128, 20}, nr.rate, 0.1, 0, 1e6);
      y += 20;
    }
  }

  y += 8;
  widgetSection(ui, hashStr("sec misc"), "Прочее (1.13)", app.sectOpen["misc"], {r.x, y, r.w, 22});
  y += 22;
  if (app.sectOpen["misc"]) {
    y += textRow(ui, gid2("col", nr.id), "цвет", {r.x, y, r.w, 0}, nr.color);
    y += textRow(ui, gid2("tag", nr.id), "тег", {r.x, y, r.w, 0}, nr.tag);
    y += textRow(ui, gid2("not", nr.id), "заметка", {r.x, y, r.w, 0}, nr.note);
  }

  y += 10;
  y += roRow(ui, "u", fmt([&](char* b, size_t n) { snprintf(b, n, "%.3f", nr.u); }), {r.x, y, r.w, 0});
  y += roRow(ui, "рефрак", std::to_string(nr.refr), {r.x, y, r.w, 0});
  y += roRow(ui, "спайков", std::to_string(nr.spikeCount), {r.x, y, r.w, 0});
  y += roRow(ui, "вх. весов", fmt([&](char* b, size_t n) { snprintf(b, n, "%.2f", nr.inWeightSum); }), {r.x, y, r.w, 0});
  y += 8;
  float bw = (r.w - 12) / 3;
  if (widgetButton(ui, gid2("fire", nr.id), "Зажечь", {r.x, y, bw, 24})) {
    app.fireOnceEvt(nr.id);
    app.flash[nr.id] = 1.0;
  }
  if (widgetButton(ui, gid2("flow", nr.id), "Поток", {r.x + bw + 6, y, bw, 24})) {
    app.flowOpen = true;
    app.flowId = nr.id;
    app.flowHz = 0;
  }
  if (widgetButton(ui, gid2("dup", nr.id), "Дубль", {r.x + (bw + 6) * 2, y, bw, 24})) {
    app.undoPush("мышь: дублировать нейрон");
    mp::Neuron c = nr;
    c.id.clear();
    c.pos.x += 3;
    c.pos.z += 3;
    c.byCode = false;
    c.srcCluster.clear();
    c.u = 0;
    c.refr = 0;
    c.spikeCount = 0;
    c.uThrAdapt = 0;
    c.homeoAdj = 0;
    mp::Neuron* nn = app.doc.addNeuron(c);
    app.selN.clear();
    app.selN.insert(nn->id);
    app.structuralChanged();
  }
  y += 30;
  if (widgetButton(ui, gid2("del", nr.id), "Удалить (R4)", {r.x, y, r.w, 24}, true, C_ERR)) {
    app.confirmWhat = 1;
    app.confirmArg = nr.id;
    app.confirmTxt = "Удалить " + nr.id + " (R4: связи/кластеры чистятся цепочкой)?";
    app.confirmOpen = true;
  }
}

// ---------- связь ----------
static void drawSynapseInspector(App& app, UI& ui, mp::Synapse& sy, Rect r) {
  float y = r.y;
  y += textRow(ui, gid2("pre", sy.id), "пресинапс (2.1)", {r.x, y, r.w, 0}, sy.pre);
  y += textRow(ui, gid2("post", sy.id), "постсинапс (2.1)", {r.x, y, r.w, 0}, sy.post);
  widgetLabel(ui, "вес (2.2)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("w", sy.id), {r.x + 124, y, r.w - 128, 20}, sy.weight, 0.05, 0, 1e6);
  y += 20;
  int sg = (int)(sy.sign.set ? sy.sign.v : 0);
  int sg2 = sg;
  enumRow(ui, gid2("sg", sy.id), "знак (2.3)", {r.x, y, r.w, 0}, sg2, kSSign);
  sy.sign = sg2;
  y += 22;
  int pl = (int)(sy.plast.set ? sy.plast.v : 0);
  int pl2 = pl;
  enumRow(ui, gid2("pl", sy.id), "пласт-ть (2.4)", {r.x, y, r.w, 0}, pl2, kPlast);
  sy.plast = pl2;
  y += 22;
  widgetLabel(ui, "lr (2.5)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("lr", sy.id), {r.x + 124, y, r.w - 128, 20}, sy.lr, 0.005, 0, 1e6);
  y += 20;
  widgetLabel(ui, "окно тиков (2.6)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptI(ui, gid2("pw", sy.id), {r.x + 124, y, r.w - 128, 20}, sy.pw, 1, 0, 1e6);
  y += 20;
  widgetLabel(ui, "w мин (2.7)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("wmn", sy.id), {r.x + 124, y, r.w - 128, 20}, sy.wMin, 0.05);
  y += 20;
  widgetLabel(ui, "w макс (2.7)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("wmx", sy.id), {r.x + 124, y, r.w - 128, 20}, sy.wMax, 0.05);
  y += 20;
  widgetLabel(ui, "распад (2.8)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("dc", sy.id), {r.x + 124, y, r.w - 128, 20}, sy.decay, 0.005, 0, 1e6);
  y += 20;
  widgetLabel(ui, "задержка (2.9)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptI(ui, gid2("dl", sy.id), {r.x + 124, y, r.w - 128, 20}, sy.delay, 1, 0, 1e6);
  y += 20;
  y += boolRow(ui, gid2("en", sy.id), "включена (2.10, 5.10)", {r.x, y, r.w, 0}, sy.enabled);
  y += textRow(ui, gid2("col", sy.id), "цвет (2.11)", {r.x, y, r.w, 0}, sy.color);
  y += textRow(ui, gid2("not", sy.id), "заметка (2.11)", {r.x, y, r.w, 0}, sy.note);
  y += 8;
  y += roRow(ui, "w факт", fmt([&](char* b, size_t n) { snprintf(b, n, "%.3f", sy.w); }), {r.x, y, r.w, 0});
  y += roRow(ui, "бездействие", std::to_string(sy.idle), {r.x, y, r.w, 0});
  y += 8;
  float bw = (r.w - 12) / 2;
  if (widgetButton(ui, gid2("tgl", sy.id), sy.enabled ? "Отключить" : "Включить", {r.x, y, bw, 24})) {
    app.undoPush("мышь: повреждение связи");
    sy.enabled = !sy.enabled;
    app.revalidate();
  }
  if (widgetButton(ui, gid2("del", sy.id), "Удалить", {r.x + bw + 12, y, bw, 24}, true, C_ERR)) {
    app.confirmWhat = 2;
    app.confirmArg = sy.id;
    app.confirmTxt = "Перерезать связь " + sy.id + "?";
    app.confirmOpen = true;
  }
}

// ---------- кластер ----------
static std::string clusterCodeOf(const mp::Cluster& c) {
  char b[1024];
  const char* shape = c.shape.set ? (c.shape.v == 1 ? "grid" : c.shape.v == 2 ? "box" : "sphere") : "sphere";
  const char* topo = c.topo.set ? (c.topo.v == 1 ? "random" : c.topo.v == 2 ? "knn" : c.topo.v == 3 ? "full" : "grid") : "knn";
  std::string name = c.name.empty() ? c.id : c.name;
  snprintf(b, sizeof(b),
           "cluster \"%s\" { zone=\"%s\"; shape=\"%s\"; center=[%g,%g,%g]; radius=%g; count=%lld; "
           "topo=\"%s\"; knnK=%lld; density=%g; thrVar=%g; leakVar=%g; wVar=%g; seed=%lld; "
           "neuroProb=%g; pruneProb=%g; growAll=%s; growZones=[",
           name.c_str(), c.zone.c_str(), shape, c.center.x, c.center.y, c.center.z,
           c.radius.set ? c.radius.v : 8.0, (long long)(c.count.set ? c.count.v : 16), topo,
           (long long)(c.knnK.set ? c.knnK.v : 6), c.density.set ? c.density.v : 0.2,
           c.thrVar.set ? c.thrVar.v : 0.0, c.leakVar.set ? c.leakVar.v : 0.0,
           c.wVar.set ? c.wVar.v : 0.0, (long long)(c.seed.set ? c.seed.v : 1),
           c.neuroProb.set ? c.neuroProb.v : 0.0, c.pruneProb.set ? c.pruneProb.v : 0.0,
           c.growAll ? "true" : "false");
  std::string code = b;
  for (size_t i = 0; i < c.growZones.size(); ++i) {
    if (i) code += ",";
    code += "\"" + c.growZones[i] + "\"";
  }
  code += "]; }";
  return code;
}

static void drawClusterInspector(App& app, UI& ui, mp::Cluster& cl, Rect r) {
  float y = r.y;
  y += textRow(ui, gid2("nm", cl.id), "имя (3.2)", {r.x, y, r.w, 0}, cl.name);
  y += textRow(ui, gid2("zn", cl.id), "зона (3.2)", {r.x, y, r.w, 0}, cl.zone);
  int sh = (int)(cl.shape.set ? cl.shape.v : 0);
  int sh2 = sh;
  enumRow(ui, gid2("sh", cl.id), "форма (3.1)", {r.x, y, r.w, 0}, sh2, kShape);
  cl.shape = sh2;
  y += 22;
  double cx = cl.center.x, cy = cl.center.y, cz = cl.center.z;
  y += numRow(ui, gid2("cx", cl.id), "центр X", {r.x, y, r.w, 0}, cx, 0.5);
  y += numRow(ui, gid2("cy", cl.id), "центр Y", {r.x, y, r.w, 0}, cy, 0.5);
  y += numRow(ui, gid2("cz", cl.id), "центр Z", {r.x, y, r.w, 0}, cz, 0.5);
  cl.center = {cx, cy, cz};
  widgetLabel(ui, "радиус", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("rd", cl.id), {r.x + 124, y, r.w - 128, 20}, cl.radius, 0.5, 0, 1e6);
  y += 20;
  widgetLabel(ui, "число (3.1)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptI(ui, gid2("ct", cl.id), {r.x + 124, y, r.w - 128, 20}, cl.count, 1, 1, 100000);
  y += 20;
  int tp = (int)(cl.topo.set ? cl.topo.v : 2);
  int tp2 = tp;
  enumRow(ui, gid2("tp", cl.id), "топо (3.1)", {r.x, y, r.w, 0}, tp2, kTopo);
  cl.topo = tp2;
  y += 22;
  widgetLabel(ui, "kNN k", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptI(ui, gid2("kk", cl.id), {r.x + 124, y, r.w - 128, 20}, cl.knnK, 1, 1, 1000);
  y += 20;
  widgetLabel(ui, "плотность", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("dn", cl.id), {r.x + 124, y, r.w - 128, 20}, cl.density, 0.01, 0, 1);
  y += 20;
  widgetLabel(ui, "вариации (3.2)", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("tv", cl.id), {r.x + 124, y, r.w - 128, 20}, cl.thrVar, 0.01, 0, 1e6);
  widgetOptF(ui, gid2("lv", cl.id), {r.x + 124, y + 20, r.w - 128, 20}, cl.leakVar, 0.01, 0, 1e6);
  widgetOptF(ui, gid2("wv", cl.id), {r.x + 124, y + 40, r.w - 128, 20}, cl.wVar, 0.01, 0, 1e6);
  widgetLabel(ui, "(порог / утечка / вес)", {r.x + 124, y + 20, 0, 0}, 0x00000000);
  y += 60;
  widgetLabel(ui, "сид", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptI(ui, gid2("sd", cl.id), {r.x + 124, y, r.w - 128, 20}, cl.seed, 1, 0, 1e9);
  y += 20;
  widgetLabel(ui, "нейрогенез", {r.x + 4, y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, gid2("np", cl.id), {r.x + 124, y, r.w - 128, 20}, cl.neuroProb, 0.005, 0, 1);
  widgetOptF(ui, gid2("pp", cl.id), {r.x + 124, y + 20, r.w - 128, 20}, cl.pruneProb, 0.005, 0, 1);
  y += 44;
  y += boolRow(ui, gid2("ga", cl.id), "рост во все зоны", {r.x, y, r.w, 0}, cl.growAll);
  std::string gz = csv(cl.growZones);
  if (textRow(ui, gid2("gz", cl.id), "зоны роста", {r.x, y, r.w, 0}, gz)) csvSet(gz, cl.growZones);
  y += 26;
  y += roRow(ui, "юнитов", std::to_string(cl.members.size()), {r.x, y, r.w, 0});
  y += 8;
  if (widgetButton(ui, gid2("unfold", cl.id), "Развернуть/пересобрать (3.3)", {r.x, y, r.w, 24})) {
    app.undoPush("мышь: развернуть кластер");
    std::string code = clusterCodeOf(cl);
    app.lastBuild = mp::dslBuild(app.doc, code);
    app.structuralChanged();
    app.statusMsg = "кластер: +" + std::to_string(app.lastBuild.created) + " -" + std::to_string(app.lastBuild.removed);
  }
  y += 28;
  float bw = (r.w - 12) / 2;
  if (widgetButton(ui, gid2("fill", cl.id), "Заполнить (5.9)", {r.x, y, bw, 24})) {
    app.fillOpen = true;
    app.fillId = cl.id;
    app.fillFrac = 0.5;
  }
  if (widgetButton(ui, gid2("del", cl.id), "Удалить", {r.x + bw + 12, y, bw, 24}, true, C_ERR)) {
    app.undoPush("мышь: удалить кластер");
    app.doc.removeClusterNeurons(cl.id);
    for (size_t i = 0; i < app.doc.clusters.size(); ++i)
      if (app.doc.clusters[i].id == cl.id) {
        app.doc.clusters.erase(app.doc.clusters.begin() + (long)i);
        break;
      }
    app.structuralChanged();
  }
}

// ---------- общее окно (реализация в panels.cpp) ----------
bool uiWindowShared(App& app, UI& ui, unsigned id, const char* title, float& x, float& y, float w, float h,
                    bool closeBtn = true);

void inspectorDraw(App& app) {
  UI& ui = app.ui;
  float w = 300;
  float x = (float)ui.winW - w - 8;
  float y = 40;
  float h = (float)ui.winH - y - 120;
  std::string ttl = "Инспектор";
  if (app.selN.size() == 1)
    ttl = "Нейрон " + *app.selN.begin();
  else if (app.selN.size() > 1)
    ttl = "Выбрано: " + std::to_string(app.selN.size()) + " нейронов";
  else if (app.selS.size() == 1)
    ttl = "Связь " + *app.selS.begin();
  else if (app.selS.size() > 1)
    ttl = "Выбрано связей: " + std::to_string(app.selS.size());
  else if (!app.selCluster.empty())
    ttl = "Кластер " + app.selCluster;
  if (uiWindowShared(app, ui, hashStr("insp"), ttl.c_str(), x, y, w, h, true)) app.winInsp = false;
  pushClip(x + 1, y + 24, w - 2, h - 24);
  Rect r{x + 6, y + 28, w - 12, 20};
  if (app.selN.size() == 1) {
    if (mp::Neuron* n = app.doc.neuron(*app.selN.begin())) drawNeuronInspector(app, ui, *n, r);
  } else if (app.selS.size() == 1) {
    if (mp::Synapse* s = app.doc.synapse(*app.selS.begin())) drawSynapseInspector(app, ui, *s, r);
  } else if (!app.selCluster.empty()) {
    if (mp::Cluster* c = app.doc.cluster(app.selCluster)) drawClusterInspector(app, ui, *c, r);
  } else if (app.selN.size() > 1) {
    widgetLabel(ui, "группа: тащи мышью, Delete — удалить,", {r.x, r.y, r.w, 0}, C_DIM);
    widgetLabel(ui, "Ctrl+C/V — копировать/вставить (5.4)", {r.x, r.y + 18, r.w, 0}, C_DIM);
  } else {
    widgetLabel(ui, "ничего не выбрано", {r.x, r.y, r.w, 0}, C_DIM);
    widgetLabel(ui, "S — выбор/зажигание, N — нейрон,", {r.x, r.y + 18, r.w, 0}, C_DIM);
    widgetLabel(ui, "C — связь, X — резать, D — повреждение", {r.x, r.y + 36, r.w, 0}, C_DIM);
  }
  popClip();
  if (x <= ui.mx && ui.mx < x + w && y <= ui.my && ui.my < y + h) app.overAny = true;
}

} // namespace mpui
