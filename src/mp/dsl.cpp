#include "mp/dsl.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>

namespace mp {

// ───────────────────────── лексер ─────────────────────────
namespace {

struct Tok {
  enum K { Ident, Num, Str, Punct, Arrow, End } k = End;
  std::string s;
  double num = 0;
  int line = 1;
};

// предобработка: комментарии → пробелы; русские ключевые слова → английские
std::string preprocess(const std::string& src) {
  std::string out;
  out.reserve(src.size() + 32);
  for (size_t i = 0; i < src.size();) {
    if (src.compare(i, 2, "//") == 0) {
      while (i < src.size() && src[i] != '\n') { out.push_back(' '); ++i; }
      continue;
    }
    if (src.compare(i, 2, "/*") == 0) {
      while (i + 1 < src.size() && src.compare(i, 2, "*/") != 0) {
        out.push_back(src[i] == '\n' ? '\n' : ' ');
        ++i;
      }
      if (i + 1 < src.size()) { out += "  "; i += 2; }
      continue;
    }
    // русские ключевые слова (UTF-8): «повтор — 12 байт, 4 буквы», «по сетке»
    auto eat = [&](const std::string& ru, const char* en) {
      if (src.compare(i, ru.size(), ru) == 0) { out += en; i += ru.size(); return true; }
      return false;
    };
    // границы идентификатора, чтобы не задеть хвост слова
    auto boundary = [&](size_t after) {
      return after >= src.size() ||
             !( (src[after] & 0xC0) == 0x80 || isalnum((unsigned char)src[after]) || src[after] == '_' );
    };
    // двигаем i на领袖 длину многобайтовой буквы при проверке границ
    if (eat("\xD0\xBF\xD0\xBE\xD0\xB2\xD1\x82\xD0\xBE\xD1\x80", " repeat")) continue;      // «повтор»
    if (eat("\xD0\xBF\xD0\xBE \xD1\x81\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB5", " bygrid")) continue; // «по сетке»
    if (eat("\xD0\xBF\xD0\xBE\xD0\xB2\xD1\x82\xD0\xBE\xD1\x80\xD0\xB8\xD1\x82\xD1\x8C", " repeat")) continue; // «повторить»
    out.push_back(src[i]);
    ++i;
    (void)boundary;
  }
  return out;
}

struct Lexer {
  std::string src;
  size_t p = 0;
  int line = 1;
  explicit Lexer(const std::string& s) : src(s) {}

  Tok next() {
    Tok t;
    while (p < src.size()) {
      char c = src[p];
      if (c == '\n') { ++line; ++p; continue; }
      if (isspace((unsigned char)c)) { ++p; continue; }
      if (c == '-' && p + 1 < src.size() && src[p + 1] == '>') {
        t.k = Tok::Arrow; t.s = "->"; t.line = line; p += 2; return t;
      }
      if (isalpha((unsigned char)c) || (unsigned char)c >= 0x80 || c == '_') {
        size_t st = p;
        while (p < src.size() && (isalnum((unsigned char)src[p]) || (unsigned char)src[p] >= 0x80 || src[p] == '_')) ++p;
        t.k = Tok::Ident; t.s = src.substr(st, p - st); t.line = line; return t;
      }
      if (isdigit((unsigned char)c) || (c == '.' && p + 1 < src.size() && isdigit((unsigned char)src[p + 1]))) {
        size_t st = p;
        while (p < src.size() && (isdigit((unsigned char)src[p]) || src[p] == '.')) ++p;
        if (p < src.size() && (src[p] == 'e' || src[p] == 'E')) {
          ++p;
          if (p < src.size() && (src[p] == '-' || src[p] == '+')) ++p;
          while (p < src.size() && isdigit((unsigned char)src[p])) ++p;
        }
        t.k = Tok::Num; t.num = std::atof(src.substr(st, p - st).c_str()); t.line = line; return t;
      }
      if (c == '"' || c == '\'') {
        char q = c; ++p;
        std::string v;
        while (p < src.size() && src[p] != q) {
          if (src[p] == '\\' && p + 1 < src.size()) { ++p; }
          v.push_back(src[p]);
          ++p;
          if (src[p - 1] == '\n') ++line;
        }
        ++p; // закрывающая кавычка
        t.k = Tok::Str; t.s = v; t.line = line; return t;
      }
      t.k = Tok::Punct; t.s = std::string(1, c); t.line = line; ++p; return t;
    }
    t.k = Tok::End; t.line = line; return t;
  }
};

// ───────────── выражения: числа, i, j, +-*/, функции uniform/gauss/abs/sqrt/exp/pow ─────────────
struct ExprCtx {
  double i = 0, j = 0;
  long long seed = 0;
  int pairIndex = 0;
  double uni() {
    seed = (long long)((seed * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
    long long s = seed ^ (pairIndex * 2654435761ULL);
    s &= 0x7fffffffULL;
    return (double)(s & 0xffffff) / 16777216.0;
  }
};

struct Eval { bool ok = false; double v = 0; };

// gcc-стайл: маленький рекурсивный спуск по строке токенов
class Parser;

struct DLexer {
  std::vector<Tok> toks;
  size_t p = 0;
  const Tok& cur() const { return toks[std::min(p, toks.size() - 1)]; }
  const Tok& peek(size_t k = 1) const { return toks[std::min(p + k, toks.size() - 1)]; }
  bool eof() const { return cur().k == Tok::End; }
  Tok eat() { return toks[std::min(p++, toks.size() - 1)]; }
  bool is(const char* s) const { return cur().k == Tok::Punct && cur().s == s; }
  bool isIdent(const char* s) const { return cur().k == Tok::Ident && cur().s == s; }
  void expect(char c, std::vector<DslError>& errs) {
    if (!(cur().k == Tok::Punct && cur().s.size() == 1 && cur().s[0] == c))
      errs.push_back({cur().line, std::string("ожидалось '") + c + "'"});
    else ++p;
  }
  bool accept(const char* word) {   // Punct или Ident слово
    if (is(word) || isIdent(word)) { ++p; return true; }
    if (cur().k == Tok::Arrow && std::string(word) == "->") { ++p; return true; }
    return false;
  }
};

int cur_line(DLexer& L);

double evalExpr(DLexer& L, ExprCtx& cx, std::vector<DslError>& errs, int depth = 0);
double evalPrimary(DLexer& L, ExprCtx& cx, std::vector<DslError>& errs, int depth) {
  if (depth > 64) { errs.push_back({cur_line(L), "слишком глубокое выражение"}); ++L.p; return 0; }
  const Tok t = L.cur();
  if (t.k == Tok::Num) { ++L.p; return t.num; }
  if (t.k == Tok::Ident) {
    ++L.p;
    if (t.s == "i") return cx.i;
    if (t.s == "j") return cx.j;
    if (t.s == "uniform") {
      // uniform(a,b[,seed])
      if (!L.is("(")) { errs.push_back({t.line, "uniform( … )"}); return 0; }
      ++L.p;
      double a = evalExpr(L, cx, errs, depth + 1);
      if (L.is(",")) ++L.p;
      double b = evalExpr(L, cx, errs, depth + 1);
      long long sd = cx.seed;
      if (L.is(",")) { ++L.p; sd = (long long)evalExpr(L, cx, errs, depth + 1); }
      if (!L.is(")")) errs.push_back({t.line, "ожидалась ')'"}); else ++L.p;
      double save = cx.seed; cx.seed = sd;
      double r = cx.uni();
      cx.seed = (long long)save;
      return a + (b - a) * r;
    }
    if (t.s == "gauss") {
      if (!L.is("(")) { errs.push_back({t.line, "gauss( … )"}); return 0; }
      ++L.p;
      double mu = evalExpr(L, cx, errs, depth + 1);
      if (L.is(",")) ++L.p;
      double sd = evalExpr(L, cx, errs, depth + 1);
      if (!L.is(")")) errs.push_back({t.line, "ожидалась ')'"}); else ++L.p;
      double u1 = cx.uni(), u2 = cx.uni();
      if (u1 < 1e-9) u1 = 1e-9;
      return mu + sd * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
    if (t.s == "abs" || t.s == "sqrt" || t.s == "exp" || t.s == "pow" || t.s == "min" || t.s == "max") {
      if (!L.is("(")) { errs.push_back({t.line, t.s + "( … )"}); return 0; }
      ++L.p;
      double a = evalExpr(L, cx, errs, depth + 1);
      double b = 0;
      if (L.is(",")) { ++L.p; b = evalExpr(L, cx, errs, depth + 1); }
      if (!L.is(")")) errs.push_back({t.line, "ожидалась ')'"}); else ++L.p;
      if (t.s == "abs") return std::fabs(a);
      if (t.s == "sqrt") return std::sqrt(std::max(0.0, a));
      if (t.s == "exp") return std::exp(a);
      if (t.s == "pow") return std::pow(a, b);
      if (t.s == "min") return std::min(a, b);
      return std::max(a, b);
    }
    errs.push_back({t.line, "неизвестный идентификатор '" + t.s + "'"});
    return 0;
  }
  if (t.k == Tok::Punct && t.s == "(") {
    ++L.p;
    double v = evalExpr(L, cx, errs, depth + 1);
    if (!L.is(")")) errs.push_back({t.line, "ожидалась ')'"}); else ++L.p;
    return v;
  }
  if (t.k == Tok::Punct && (t.s == "-" || t.s == "+")) {
    ++L.p;
    double v = evalPrimary(L, cx, errs, depth + 1);
    return t.s == "-" ? -v : v;
  }
  errs.push_back({t.line, "ожидалось число/идентификатор"});
  ++L.p;
  return 0;
}

double evalExpr(DLexer& L, ExprCtx& cx, std::vector<DslError>& errs, int depth) {
  double v = evalPrimary(L, cx, errs, depth);
  for (;;) {
    if (L.is("*")) { ++L.p; v *= evalPrimary(L, cx, errs, depth + 1); }
    else if (L.is("/")) { ++L.p; double d = evalPrimary(L, cx, errs, depth + 1); v = (std::fabs(d) < 1e-12) ? 0 : v / d; }
    else if (L.is("+")) { ++L.p; v += evalPrimary(L, cx, errs, depth + 1); }
    else if (L.is("-")) { ++L.p; v -= evalPrimary(L, cx, errs, depth + 1); }
    else break;
  }
  return v;
}

int cur_line(DLexer& L) { return L.cur().line; }

Plasticity plastFromString(const std::string& s) {
  if (s == "hebb") return Plasticity::Hebb;
  if (s == "reward") return Plasticity::RewardGated;
  if (s == "antihebb") return Plasticity::AntiHebb;
  return Plasticity::Rigid;
}


// ───────────── разбор ─────────────
struct UnitDecl {
  std::string kind;                    // sensor|hidden|motor|modulator|homeostat
  std::string name;                    // необязательная метка
  std::map<std::string, std::vector<Tok>> params;
  V3 at;
  bool hasAt = false;
  double step = 2.0;
  int line = 0;
};

struct Stmt {
  enum K { Zone, Unit, Repeat, Cluster, Connect, Modulator } k = Unit;
  int line = 0;
  UnitDecl unit;                        // Unit / база Repeat
  int count = 0, gw = 0, gh = 0;        // Repeat
  std::vector<UnitDecl> repeatUnits;
  // Zone
  std::string zname, zcolor;
  // Cluster
  std::string cname;
  std::map<std::string, std::vector<Tok>> cparams;
  // Connect
  std::string from, to;
  std::map<std::string, std::vector<Tok>> connParams;
};

void skipSemi(DLexer& L) { while (L.is(";")) ++L.p; }

std::map<std::string, std::vector<Tok>> parseParams(DLexer& L, std::vector<DslError>& errs) {
  std::map<std::string, std::vector<Tok>> params;
  while (!L.eof() && !L.is("}")) {
    if (L.cur().k != Tok::Ident) { errs.push_back({cur_line(L), "ожидался параметр"}); ++L.p; continue; }
    std::string key = L.eat().s;
    bool posKey = L.is("[");          // bare-форма: ключ [x,y,z] — позиция юнита
    if (!posKey && !L.is("=") && !L.is(":")) { errs.push_back({cur_line(L), "ожидался '='"}); continue; }
    if (!posKey) ++L.p;
    size_t start = L.p;
    int depth = 0;
    while (!L.eof()) {
      if (L.is("[") || L.is("(")) ++depth;
      if (L.is("]") || L.is(")")) --depth;
      if (depth == 0 && (L.is(";") || L.is("}") || L.is(","))) break;
      ++L.p;
    }
    if (L.is(",")) ++L.p;
    params[key] = std::vector<Tok>(L.toks.begin() + (long)start, L.toks.begin() + (long)L.p);
    skipSemi(L);
  }
  return params;
}

V3 parseVec(const std::vector<Tok>& toks, ExprCtx& cx, std::vector<DslError>& errs, int line) {
  V3 v;
  DLexer L; L.toks = toks;
  if (!L.is("[")) { errs.push_back({line, "ожидался вектор [x,y,z]"}); return v; }
  ++L.p;
  v.x = evalExpr(L, cx, errs);
  if (L.is(",")) ++L.p;
  v.y = evalExpr(L, cx, errs);
  if (L.is(",")) ++L.p;
  v.z = evalExpr(L, cx, errs);
  if (!L.is("]")) errs.push_back({line, "ожидалась ']'"}); else ++L.p;
  return v;
}

double evalParam(const std::vector<Tok>& toks, ExprCtx& cx, std::vector<DslError>& errs, int line) {
  (void)line;
  DLexer L; L.toks = toks;
  return evalExpr(L, cx, errs);
}

std::string evalStr(const std::vector<Tok>& toks, int line, std::vector<DslError>& errs) {
  if (toks.size() == 1 && toks[0].k == Tok::Str) return toks[0].s;
  if (toks.size() == 1 && toks[0].k == Tok::Num) {
    char b[32]; std::snprintf(b, sizeof b, "%g", toks[0].num);
    return b;
  }
  errs.push_back({line, "ожидалась строка"});
  return "";
}

std::vector<std::string> evalStrList(const std::vector<Tok>& toks, int line, std::vector<DslError>& errs) {
  std::vector<std::string> r;
  DLexer L; L.toks = toks;
  if (!L.is("[")) { errs.push_back({line, "ожидался список строк"}); return r; }
  ++L.p;
  while (!L.eof() && !L.is("]")) {
    if (L.cur().k == Tok::Str) r.push_back(L.eat().s);
    else if (L.is(",")) ++L.p;
    else { errs.push_back({cur_line(L), "ожидалась строка в списке"}); ++L.p; }
  }
  return r;
}

} // namespace

// ───────────────────────── парсер программы ─────────────────────────
BuildResult parseAndBuild(Project& doc, const std::string& code);

BuildResult dslBuild(Project& doc, const std::string& code) {
  return parseAndBuild(doc, code);
}

// ───────────────────────── сборка (4.2) ─────────────────────────
namespace {

struct BuiltUnit {
  std::string id;
  Neuron n;
};

void applyUnit(Project& doc, const UnitDecl& u, ExprCtx cx, int inst,
               std::vector<DslError>& errs, BuildResult& res,
               std::vector<std::string>& codeIds) {
  Neuron n;
  n.byCode = true;
  NType t = NType::Hidden;
  std::string tc = "H";
  if (u.kind == "sensor") { t = NType::Sensor; tc = "S"; }
  else if (u.kind == "motor") { t = NType::Motor; tc = "M"; }
  else if (u.kind == "Modulator" || u.kind == "modulator") { t = NType::Modulator; tc = "D"; }
  else if (u.kind == "homeostat") { t = NType::Homeostat; tc = "G"; }
  n.type = OptI((long long)t);
  if (t == NType::Modulator && !u.name.empty()) {
    // modulator("dopamine") { … } — имя задаёт вид модулятора (1.22)
    if (u.name == "punishment") n.mkind = OptI((long long)ModKind::Punishment);
    else if (u.name == "novelty") n.mkind = OptI((long long)ModKind::Novelty);
    else if (u.name == "dopamine") n.mkind = OptI((long long)ModKind::Dopamine);
  }

  cx.i = (double)inst;
  // позиция: at + step*grid (i → строка/колонка решётки кладётся вызывающим через cx.j)
  // параметры
  for (const auto& kv : u.params) {
    const std::string& key = kv.first;
    const auto& toks = kv.second;
    if (key == "zone") { n.zone = evalStr(toks, u.line, errs); continue; }
    if (key == "label") { n.label = evalStr(toks, u.line, errs); continue; }
    if (key == "color") { n.color = evalStr(toks, u.line, errs); continue; }
    if (key == "note") { n.note = evalStr(toks, u.line, errs); continue; }
    if (key == "tag") { n.tag = evalStr(toks, u.line, errs); continue; }
    if (key == "model") { n.model = OptI((long long)(evalStr(toks, u.line, errs) == "alif" ? NModel::ALIF : NModel::LIF)); continue; }
    if (key == "homeoOn") { n.homeoOn = evalParam(toks, cx, errs, u.line) != 0; continue; }
    if (key == "inv") { n.inv = evalParam(toks, cx, errs, u.line) != 0; continue; }
    if (key == "growth" || key == "growZones") { n.growth = evalStrList(toks, u.line, errs); continue; }
    if (key == "mzone") { n.mzonelist = evalStrList(toks, u.line, errs); continue; }
    if (key == "at" || key == "pos") {
      n.pos = parseVec(toks, cx, errs, u.line); continue; }
    if (key == "src") { n.src = OptI((long long)(evalStr(toks, u.line, errs) == "api" ? 1 : 0)); continue; }
    if (key == "enc") {
      std::string e = evalStr(toks, u.line, errs);
      n.enc = OptI((long long)(e == "event_diff" ? SensorEncoding::EventDiff : e == "event_onoff" ? SensorEncoding::EventOnOff : SensorEncoding::Rate));
      continue;
    }
    if (key == "act") {
      std::string a = evalStr(toks, u.line, errs);
      n.act = OptI((long long)(a == "servo_R" ? Actuator::ServoR : a == "voice" ? Actuator::Voice : Actuator::ServoL));
      continue;
    }
    if (key == "mkind") {
      std::string k = evalStr(toks, u.line, errs);
      n.mkind = OptI((long long)(k == "punishment" ? ModKind::Punishment : k == "novelty" ? ModKind::Novelty : ModKind::Dopamine));
      continue;
    }
    if (key == "mscope") {
      std::string k = evalStr(toks, u.line, errs);
      n.mscope = OptI((long long)(k == "zone" ? ModScope::Zone : k == "zones" ? ModScope::Zones : ModScope::Global));
      continue;
    }
    if (key == "curve") { n.curve = OptI((long long)(evalStr(toks, u.line, errs) == "gamma" ? Curve::Gamma : Curve::Linear)); continue; }
    // числовые
    double v = evalParam(toks, cx, errs, u.line);
    if (key == "thr") { n.thr = OptF(v); continue; }
    if (key == "leak") { n.leak = OptF(v); continue; }
    if (key == "refrMax") { n.refrMax = OptI((long long)v); continue; }
    if (key == "noise") { n.noise = OptF(v); continue; }
    if (key == "trace") { n.trace = OptI((long long)v); continue; }
    if (key == "homeoRate") { n.homeoRate = OptF(v); continue; }
    if (key == "fireThr") { n.fireThr = OptF(v); continue; }
    if (key == "hyst") { n.hyst = OptF(v); continue; }
    if (key == "normMin") { n.normMin = OptF(v); continue; }
    if (key == "normMax") { n.normMax = OptF(v); continue; }
    if (key == "smoothMs") { n.smoothMs = OptI((long long)v); continue; }
    if (key == "mapMin") { n.mapMin = OptF(v); continue; }
    if (key == "mapMax") { n.mapMax = OptF(v); continue; }
    if (key == "mapSpeed") { n.mapSpeed = OptF(v); continue; }
    if (key == "gamma") { n.gamma = OptF(v); continue; }
    if (key == "windowSec") { n.windowSec = OptF(v); continue; }
    if (key == "rate") { n.rate = OptF(v); continue; }
    if (key == "target") { n.target = OptF(v); continue; }
    if (key == "integSec") { n.integSec = OptF(v); continue; }
    if (key == "kThr") { n.kThr = OptF(v); continue; }
    if (key == "kPlast") { n.kPlast = OptF(v); continue; }
    // pos внутри тела юнита
        // вес/связочные параметры у юнита не валидны
    errs.push_back({u.line, "неизвестный параметр '" + key + "'"});
  }
  if (u.hasAt) {
    n.pos = u.at;
  } else if (u.params.count("pos") == 0 && u.params.count("at") == 0) {
    // без явной at/pos — позиция (0,0,0); так юнит виден у основания сцены
    n.pos = V3{0, 0, 0};
  }
  // канонический id: код-юниты нумеруются детерминированно по зоне и типу
  std::string zoneCode = n.zone.empty() ? "Z0" : n.zone;
  // R2: если желаемый id занят РУЧНЫМ юнитом или новым код-юнитом этой сборки —
  // берём следующий свободный номер; конфликт фиксируется в res.conflicts (показываем, не молчим).
  std::string id;
  long long nn = (long long)inst + 1;
  bool shifted = false;
  for (;; ++nn) {
    std::string cand = zoneCode + std::string(1, '.') + tc + std::string(1, '.') + std::to_string(nn);
    Neuron* exn = doc.neuron(cand);
    bool manualBlocked = exn && !exn->byCode;
    bool dupThisBuild = std::find(codeIds.begin(), codeIds.end(), cand) != codeIds.end();
    if (manualBlocked) {
      shifted = true;
      continue;
    }
    if (dupThisBuild) continue;
    id = cand;
    break;
  }
  if (shifted)
    res.conflicts.push_back({std::string("номер '") + tc + "." + std::to_string(inst + 1) +
                             "' зоны '" + zoneCode + "' занят ручным юнитом (R2), код-юнит получил следующий номер",
                             id});
  n.id = id;
  if (n.label.empty()) {
    n.label = tc + std::to_string(nn);
  }
  Neuron* ex = doc.neuron(id);
  if (ex) {
    n.lastInSum = ex->lastInSum;
    n.inWeightSum = ex->inWeightSum;
    *ex = n;
    res.updated++;
  } else {
    doc.addNeuron(n);
    res.created++;
  }
  codeIds.push_back(id);
}

void applyRepeat(Project& doc, const Stmt& st, std::vector<DslError>& errs, BuildResult& res,
                 std::vector<std::string>& codeIds) {
  int gw = std::max(1, st.gw);
  for (int k = 0; k < st.count; ++k) {
    int gx = k % gw, gy = k / gw;
    ExprCtx cx;
    cx.i = (double)k;                    // i — номер повторения
    cx.j = (double)gx + (double)gy * gw; // j — линейный индекс ячейки сетки
    cx.seed = st.line + k * 131;
    for (const UnitDecl& u0 : st.repeatUnits) {
      UnitDecl u = u0;
      if (st.unit.hasAt) {               // позиции по сетке: at + step*колонка/строка
        u.at = V3{st.unit.at.x + st.unit.step * gx,
                  st.unit.at.y + st.unit.step * gy,
                  st.unit.at.z};
        u.hasAt = true;
      }
      applyUnit(doc, u, cx, k, errs, res, codeIds);
    }
  }
}

} // namespace

BuildResult parseAndBuild(Project& doc, const std::string& codeText) {
  BuildResult res;
  std::string src = preprocess(codeText);
  Lexer lx(src);
  DLexer L;
  Tok lead;                       // стартовый sentinel: не End, иначе eof() сработает сразу
  lead.k = Tok::Punct; lead.s = ";";
  L.toks.push_back(lead);
  Tok t;
  while ((t = lx.next()).k != Tok::End) L.toks.push_back(t);
  L.toks.push_back(Tok{});

  std::vector<Stmt> stmts;
  std::vector<DslError> errs;

  // ── фаза парсинга ──
  while (!L.eof()) {
    if (L.is(";")) { ++L.p; continue; }
    if (L.cur().k != Tok::Ident) { errs.push_back({cur_line(L), "ожидался оператор"}); ++L.p; continue; }
    std::string kw = L.cur().s;
    int line = cur_line(L);
    if (kw == "zone") {
      ++L.p;
      if (!L.is("(")) { errs.push_back({line, "zone( … )"}); ++L.p; continue; }
      ++L.p;
      Stmt s; s.k = Stmt::Zone; s.line = line;
      if (L.cur().k == Tok::Str) s.zname = L.eat().s; else errs.push_back({line, "код зоны — строка"});
      if (L.is(",")) {
        ++L.p;
        if (L.isIdent("color")) { ++L.p; L.expect('=', errs); s.zcolor = evalStr(std::vector<Tok>{L.eat()}, line, errs); }
      }
      L.expect(')', errs);
      skipSemi(L);
      stmts.push_back(s);
      continue;
    }
    if (kw == "cluster") {
      ++L.p;
      Stmt s; s.k = Stmt::Cluster; s.line = line;
      if (L.cur().k == Tok::Str) s.cname = L.eat().s;
      if (L.cur().k == Tok::Ident) s.cname = L.eat().s;
      if (L.is("{")) { ++L.p; }
      s.cparams = parseParams(L, errs);
      if (L.is("}")) ++L.p; else errs.push_back({line, "ожидалась '}' кластера"});
      stmts.push_back(s);
      continue;
    }
    if (kw == "connect") {
      ++L.p;
      if (!L.is("(")) { errs.push_back({line, "connect( … )"}); continue; }
      ++L.p;
      Stmt s; s.k = Stmt::Connect; s.line = line;
      if (L.cur().k == Tok::Str) s.from = L.eat().s;
      else if (L.cur().k == Tok::Ident) s.from = L.eat().s;
      else { errs.push_back({line, "откуда — строка"}); }
      if (!L.accept("->")) { errs.push_back({line, "ожидалось ->"}); }
      if (L.cur().k == Tok::Str) s.to = L.eat().s;
      else if (L.cur().k == Tok::Ident) s.to = L.eat().s;
      else { errs.push_back({line, "куда — строка"}); }
      // параметры через запятую: key=value
      while (L.is(",")) {
        ++L.p;
        if (L.cur().k != Tok::Ident) break;
        std::string key = L.eat().s;
        if (!L.is("=") && !L.is(":")) { errs.push_back({line, "ожидался '='"}); break; }
        ++L.p;
        size_t start = L.p;
        while (!L.eof() && !L.is(",") && !L.is(")")) ++L.p;
        s.connParams[key] = std::vector<Tok>(L.toks.begin() + (long)start, L.toks.begin() + (long)L.p);
      }
      L.expect(')', errs);
      skipSemi(L);
      stmts.push_back(s);
      continue;
    }
    if (kw == "repeat") {
      ++L.p;
      Stmt s; s.k = Stmt::Repeat; s.line = line;
      // повтор N по сетке WxH at [..] step S { unitdecl... }
      if (L.cur().k == Tok::Num) s.count = (int)L.eat().num;
      else { errs.push_back({line, "повтор N …"}); s.count = 1; }
      if (!L.accept("bygrid")) { errs.push_back({cur_line(L), "ожеидалось «по сетке WxH»"}); }
      if (L.cur().k == Tok::Num) {
        double a = L.eat().num;
        // лексер склеивает «5x4» в Num(5)+Ident("x4"): поддерживаем оба разбиения
        bool gotH = false;
        if (L.cur().k == Tok::Ident) {
          const std::string& sv = L.cur().s;
          bool restDigits = sv.size() > 1 &&
                            std::all_of(sv.begin() + 1, sv.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
          if (sv[0] == 'x' && restDigits) { s.gw = (int)a; s.gh = std::atoi(sv.c_str() + 1); gotH = true; ++L.p; }
          else if (sv == "x") {
            ++L.p;
            if (L.cur().k == Tok::Num) { s.gw = (int)a; s.gh = (int)L.eat().num; gotH = true; }
          }
        }
        if (!gotH) { s.gw = (int)a; s.gh = 1; }
      }
      if (L.isIdent("at")) {
        ++L.p;
        if (L.is("[")) {
          std::vector<Tok> vec;
          // at [ ... ]: parseVec ждёт "["..."]", восстанавливаем скобки
          Tok lb; lb.k = Tok::Punct; lb.s = "["; lb.line = line;
          vec.push_back(lb);
          ++L.p;
          int depth = 1;
          while (!L.eof() && depth > 0) {
            if (L.is("[")) ++depth;
            if (L.is("]")) { --depth; if (depth == 0) { ++L.p; break; } }
            vec.push_back(L.eat());
          }
          Tok rb; rb.k = Tok::Punct; rb.s = "]"; rb.line = line;
          vec.push_back(rb);
          ExprCtx cx0;
          s.unit.at = parseVec(vec, cx0, errs, line);
          s.unit.hasAt = true;
        }
      }
      if (L.isIdent("step")) {
        ++L.p;
        if (L.cur().k == Tok::Num) s.unit.step = L.eat().num;
      }
      if (!L.is("{")) { errs.push_back({cur_line(L), "ожидался '{' цикла"}); }
      else ++L.p;
      // тела цикла: объявления юнитов
      while (!L.eof() && !L.is("}")) {
        if (L.cur().k != Tok::Ident) { ++L.p; continue; }
        std::string uk = L.cur().s;
        if (uk == "sensor" || uk == "hidden" || uk == "motor" || uk == "modulator" || uk == "homeostat") {
          ++L.p;
          UnitDecl u; u.kind = uk; u.line = cur_line(L);
          if (L.cur().k == Tok::Str) u.name = L.eat().s;
          if (L.is("{")) { ++L.p; u.params = parseParams(L, errs); if (L.is("}")) ++L.p; }
          // позиция экземпляров: из блока repeat (s.unit.at/step) или локально pos=
          if (s.unit.hasAt) { u.at = s.unit.at; u.hasAt = true; u.step = 0; }
          s.repeatUnits.push_back(u);
        } else { errs.push_back({cur_line(L), "в цикле ожидался юнит"}); ++L.p; }
      }
      if (L.is("}")) ++L.p;
      stmts.push_back(s);
      continue;
    }
    if (kw == "sensor" || kw == "hidden" || kw == "motor" || kw == "modulator" || kw == "homeostat") {
      ++L.p;
      Stmt s; s.k = Stmt::Unit; s.line = line;
      s.unit.kind = kw;
      if (L.cur().k == Tok::Str) s.unit.name = L.eat().s;
      if (L.is("{")) {
        ++L.p;
        s.unit.params = parseParams(L, errs);
        if (L.is("}")) ++L.p; else errs.push_back({line, "ожидалась '}' юнита"});
      } else { errs.push_back({line, "ожидался '{' юнита"}); }
      if (L.isIdent("at")) {
        ++L.p;
        if (L.is("[")) {
          std::vector<Tok> vec;
          Tok lb; lb.k = Tok::Punct; lb.s = "["; lb.line = line;
          vec.push_back(lb);
          ++L.p; int depth = 1;
          while (!L.eof() && depth > 0) {
            if (L.is("[")) ++depth;
            if (L.is("]")) { --depth; if (depth == 0) { ++L.p; break; } }
            vec.push_back(L.eat());
          }
          Tok rb; rb.k = Tok::Punct; rb.s = "]"; rb.line = line;
          vec.push_back(rb);
          ExprCtx cx0;
          s.unit.at = parseVec(vec, cx0, errs, line);
          s.unit.hasAt = true;
        }
      }
      stmts.push_back(s);
      continue;
    }
    errs.push_back({line, "неизвестный оператор '" + kw + "'"});
    ++L.p;
  }

  // ── фаза применения (R2: операция НАД документом) ──
  std::vector<std::string> codeIds;
  int inst = 0;
  for (const auto& st : stmts) {
    if (st.k == Stmt::Zone) {
      Zone z; z.code = st.zname; z.color = st.zcolor.empty() ? "#3a4a5a" : st.zcolor; z.byCode = true;
      Zone* ez = doc.zone(st.zname);
      if (ez) { if (!ez->byCode) res.conflicts.push_back({std::string("ручная зона '") + st.zname + "' повторно объявлена кодом (R2)", st.zname}); else { ez->color = z.color; ez->byCode = true; } }
      else { doc.zones.push_back(z); res.created++; }
      continue;
    }
    if (st.k == Stmt::Unit) {
      ExprCtx cx; cx.i = (double)inst++; cx.seed = st.line;
      applyUnit(doc, st.unit, cx, 0, errs, res, codeIds);
      // инкремент номера — детерминированная сквозная нумерация код-юнитов зоны/типа
      continue;
    }
    if (st.k == Stmt::Repeat) {
      applyRepeat(doc, st, errs, res, codeIds);
      continue;
    }
    if (st.k == Stmt::Cluster) {
      Cluster c;
      c.name = st.cname; c.byCode = true;
      // параметры как в 3.2
      auto gp = [&](const char* key) -> const std::vector<Tok>* {
        auto it = st.cparams.find(key);
        return it == st.cparams.end() ? nullptr : &it->second;
      };
      auto setF = [&](const char* key, OptF& o) {
        if (const auto* t = gp(key)) { ExprCtx cx; o = OptF(evalParam(*t, cx, errs, st.line)); }
      };
      auto setI = [&](const char* key, OptI& o) {
        if (const auto* t = gp(key)) { ExprCtx cx; o = OptI((long long)evalParam(*t, cx, errs, st.line)); }
      };
      std::string zoneCode = "";
      if (const auto* t = gp("zone")) { ExprCtx cx; zoneCode = evalStr(*t, st.line, errs); }
      setF("radius", c.radius);
      setI("count", c.count);
      setI("seed", c.seed);
      setF("thrVar", c.thrVar);
      setF("leakVar", c.leakVar);
      setF("wVar", c.wVar);
      setF("neuroProb", c.neuroProb);
      setF("pruneProb", c.pruneProb);
      setF("density", c.density);
      setI("knnK", c.knnK);
      if (const auto* t = gp("shape")) { ExprCtx cx; std::string sv = evalStr(*t, st.line, errs); c.shape = OptI((long long)(sv == "grid" ? Shape::Grid : sv == "box" ? Shape::Box : Shape::Sphere)); }
      if (const auto* t = gp("topo")) { ExprCtx cx; std::string tv = evalStr(*t, st.line, errs); c.topo = OptI((long long)(tv == "grid" ? Topo::Grid : tv == "random" ? Topo::Random : tv == "full" ? Topo::Full : Topo::KNN)); }
      if (const auto* t = gp("center")) { ExprCtx cx; c.center = parseVec(*t, cx, errs, st.line); }
      if (const auto* t = gp("growZones")) c.growZones = evalStrList(*t, st.line, errs);
      if (const auto* t = gp("growDirs")) c.growZones = evalStrList(*t, st.line, errs); // (3.2) разрешённые направления роста
      if (!c.growZones.empty()) c.growAll = false;
      // existing?
      Cluster* ex = nullptr;
      for (auto& cc : doc.clusters)
        if (cc.name == c.name && cc.byCode) { ex = &cc; break; }
      c.zone = zoneCode;
      if (!ex) {
        c.id = doc.nextClusterId();
        doc.clusters.push_back(c);
        ex = &doc.clusters.back();
        res.created++;
      } else {
        c.id = ex->id;
        *ex = c;
        res.updated++;
      }
      // РАЗВЁРТКА (3.3): сфера/сетка/бокс → нейроны; кластер виден объёмом
      if (c.count.set && c.count.v > 0 && c.radius.set && c.shape.set && c.topo.set && c.seed.set) {
        long long N = c.count.v;
        double R = c.radius.v;
        long long sd = c.seed.v;
        double thrVar = c.thrVar.set ? c.thrVar.v : 0.0;
        double leakVar = c.leakVar.set ? c.leakVar.v : 0.0;
        // базовые динамические параметры кластера (thr/leak если заданы)
        double baseThr = 1.0, baseLeak = 0.02;
        if (const auto* t = gp("thr")) { ExprCtx cx; baseThr = evalParam(*t, cx, errs, st.line); }
        if (const auto* t = gp("leak")) { ExprCtx cx; baseLeak = evalParam(*t, cx, errs, st.line); }
        std::vector<std::string> newMembers;
        std::vector<V3> pts((size_t)std::max(0LL, N));
        for (long long i = 0; i < N; ++i) {
          V3 p;
          Shape sh = (Shape)c.shape.v;
          if (sh == Shape::Sphere) {
            // финская сфера (fibonacci) внутри радиуса
            double y = 1.0 - 2.0 * (i + 0.5) / (double)N;
            double r = std::sqrt(std::max(0.0, 1.0 - y * y));
            double phi = 2.399963229728653 * (double)i;
            p = V3{R * r * std::cos(phi), R * y, R * r * std::sin(phi)};
          } else if (sh == Shape::Grid) {
            long long cols = (long long)std::ceil(std::sqrt((double)N));
            long long gx = i % cols, gy = i / cols;
            double sp = (2.0 * R) / std::max(1.0, (double)cols);
            p = V3{-R + sp * (gx + 0.5), sp * (gy + 0.5), -R + sp * ((i / std::max(1LL, cols * cols)) + 0.5) * sp};
          } else { // box
            // детерминированный LCG

            sd = (long long)((sd * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
            double rx = (double)(sd & 0xffff) / 65536.0;
            sd = (long long)((sd * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
            double ry = (double)(sd & 0xffff) / 65536.0;
            sd = (long long)((sd * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
            double rz = (double)(sd & 0xffff) / 65536.0;
            p = V3{(rx - 0.5) * 2 * R, (ry - 0.5) * 2 * R, (rz - 0.5) * 2 * R};
          }
          pts[(size_t)i] = V3{c.center.x + p.x, c.center.y + p.y, c.center.z + p.z};
        }
        for (long long i = 0; i < N; ++i) {
          std::string zoneCode2 = zoneCode.empty() ? "Z0" : zoneCode;
          // R2: если номер занят ручным юнитом или другим код-юнитом этой сборки —
          // следующий свободный номер (конфликт фиксируется, ручное не стираем)
          std::string id;
          bool shifted = false;
          for (long long nn = i + 1;; ++nn) {
            std::string cand = zoneCode2 + ".H." + std::to_string(nn);
            Neuron* exn0 = doc.neuron(cand);
            bool manualBlocked = exn0 && !exn0->byCode;
            bool dupThisBuild = std::find(codeIds.begin(), codeIds.end(), cand) != codeIds.end();
            if (manualBlocked) { shifted = true; continue; }
            if (dupThisBuild) continue;
            id = cand;
            break;
          }
          if (shifted)
            res.conflicts.push_back({std::string("номер зоны '") + zoneCode2 + "' занят ручным юнитом (R2), кластер смещает номера", id});
          // id: зоны кластера видно в канонике (код.тип.ннн)
          Neuron n;
          n.byCode = true;
          n.srcCluster = c.id;
          n.id = id;
          n.type = OptI((long long)NType::Hidden);
          n.zone = zoneCode;
          n.pos = pts[(size_t)i];
          // вариации параметров с сидом (3.2)
          long long sd2 = c.seed.v + i * 7919;
          sd2 = (long long)((sd2 * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
          double r1 = (double)(sd2 & 0xffffff) / 16777216.0;
          sd2 = (long long)((sd2 * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
          double r2 = (double)(sd2 & 0xffffff) / 16777216.0;
          n.thr = OptF(baseThr * (1.0 + thrVar * (r1 - 0.5)));
          n.leak = OptF(std::max(0.0, baseLeak * (1.0 + leakVar * (r2 - 0.5))));
          n.refrMax = OptI(3); // обязательно задаём явно? нет: R1 требует явности — base от decl
          if (const auto* t2 = gp("refrMax")) { ExprCtx cx; n.refrMax = OptI((long long)evalParam(*t2, cx, errs, st.line)); }
          if (const auto* t2 = gp("noise")) { ExprCtx cx; n.noise = OptF(evalParam(*t2, cx, errs, st.line)); }
          if (const auto* t2 = gp("trace")) { ExprCtx cx; n.trace = OptI((long long)evalParam(*t2, cx, errs, st.line)); }
          Neuron* exn = doc.neuron(id);
          if (exn && !exn->byCode) {
            res.conflicts.push_back({std::string("ручной юнит '") + id + "' занят кластером кода (R2)", id});
            continue;
          }
          if (exn) {
            if (exn->spiked) {} // динамика сбрасывается при подмене
            *exn = n;
            res.updated++;
          } else {
            doc.addNeuron(n);
            res.created++;
          }
          codeIds.push_back(id);
          newMembers.push_back(id);
        }
        ex->members = newMembers;
        // внутренние связи по топологии (3.2): knn/random/full/grid
        // веса из формулы connect с wVar/евидом
        std::string wExpr;
        if (const auto* t2 = gp("w")) { wExpr = ""; (void)t2; }
        double wBase = 0.5;
        if (const auto* t2 = gp("w")) {
          DLexer WL; WL.toks.clear(); Tok wt; wt.k = Tok::Num; wt.num = 0; wt.line = st.line;
          for (const auto& tk : *t2) WL.toks.push_back(tk);
          WL.toks.push_back(Tok{});
          ExprCtx cx;
          wBase = evalExpr(WL, cx, errs);
        }
        double wVar = c.wVar.set ? c.wVar.v : 0.0;
        double dens = c.density.set ? c.density.v : 1.0;
        Topo topo = (Topo)c.topo.v;
        (void)wExpr;
        auto addSyn = [&](const std::string& a, const std::string& b, long long pairIdx) {
          Synapse s;
          s.byCode = true;
          s.pre = a; s.post = b;
          long long sd3 = (c.seed.set ? c.seed.v : 0) * 31 + pairIdx;
          sd3 = (long long)((sd3 * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
          double r = (double)(sd3 & 0xffffff) / 16777216.0;
          double wmag = wBase * (1.0 + wVar * (r - 0.5));
          s.weight = OptF(std::max(0.0, wmag));
          s.sign = OptI((long long)SSign::Exc);
          std::string cplast = "rigid";
          if (gp("plast")) cplast = evalStr(*gp("plast"), st.line, errs);
          s.plast = OptI((long long)plastFromString(cplast));
          s.lr = OptF(0.01);
          if (const auto* t2 = gp("lr")) { ExprCtx cx; s.lr = OptF(evalParam(*t2, cx, errs, st.line)); }
          s.pw = OptI(20);
          if (const auto* t2 = gp("pw")) { ExprCtx cx; s.pw = OptI((long long)evalParam(*t2, cx, errs, st.line)); }
          s.wMin = OptF(0.0);
          if (const auto* t2 = gp("wMin")) { ExprCtx cx; s.wMin = OptF(evalParam(*t2, cx, errs, st.line)); }
          s.wMax = OptF(1.0);
          if (const auto* t2 = gp("wMax")) { ExprCtx cx; s.wMax = OptF(evalParam(*t2, cx, errs, st.line)); }
          s.decay = OptF(0.0);
          if (const auto* t2 = gp("decay")) { ExprCtx cx; s.decay = OptF(evalParam(*t2, cx, errs, st.line)); }
          s.delay = OptI(0);
          if (const auto* t2 = gp("delay")) { ExprCtx cx; s.delay = OptI((long long)evalParam(*t2, cx, errs, st.line)); }
          std::string sid = "syn." + a + "." + b;
          Synapse* exs = nullptr;
          for (auto& ss : doc.synapses)
            if (ss.pre == a && ss.post == b) { exs = &ss; break; }
          if (exs) { long long idNum = exs->id.rfind("syn.", 0) == 0 ? (long long)std::atoll(exs->id.c_str() + 4) : 0; long long keep = exs->w; (void)keep; *exs = s; exs->id = idNum > 0 ? "syn." + std::to_string(idNum) : doc.nextSynId(); exs->w = exs->weight.v; exs->byCode = true; res.updated++; }
          else {
            s.id = doc.nextSynId();
            s.w = s.weight.v;
            doc.synapses.push_back(s);
            res.created++;
          }
        };
        // правило
        if (topo == Topo::Full) {
          long long pair = 0;
          for (long long a = 0; a < N; ++a)
            for (long long b = 0; b < N; ++b)
              if (a != b) addSyn(newMembers[(size_t)a], newMembers[(size_t)b], pair++);
        } else if (topo == Topo::KNN) {
          long long K = c.knnK.set ? c.knnK.v : 6;
          for (long long a = 0; a < N; ++a) {
            std::vector<std::pair<double, long long>> dist;
            for (long long b = 0; b < N; ++b)
              if (b != a) {
                double dx = pts[(size_t)a].x - pts[(size_t)b].x, dy = pts[(size_t)a].y - pts[(size_t)b].y, dz = pts[(size_t)a].z - pts[(size_t)b].z;
                dist.push_back({dx * dx + dy * dy + dz * dz, b});
              }
            std::sort(dist.begin(), dist.end());
            for (long long k = 0; k < K && k < (long long)dist.size(); ++k)
              addSyn(newMembers[(size_t)a], newMembers[(size_t)dist[(size_t)k].second], a * 1000 + k);
          }
        } else if (topo == Topo::Random) {
          long long pair = 0;
          for (long long a = 0; a < N; ++a)
            for (long long b = 0; b < N; ++b) {
              if (a == b) continue;
              long long sd4 = ((c.seed.set ? c.seed.v : 0) + pair * 7919);
              sd4 = (long long)((sd4 * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
              double r = (double)(sd4 & 0xffffff) / 16777216.0;
              if (r < dens) addSyn(newMembers[(size_t)a], newMembers[(size_t)b], pair);
              ++pair;
            }
        } else { // grid
          long long cols = (long long)std::ceil(std::sqrt((double)N));
          for (long long a = 0; a < N; ++a) {
            if (a % cols != cols - 1 && a + 1 < N) addSyn(newMembers[(size_t)a], newMembers[(size_t)(a + 1)], a * 4);
            if (a + cols < N) addSyn(newMembers[(size_t)a], newMembers[(size_t)(a + cols)], a * 4 + 1);
          }
        }
      }
      continue;
    }
    if (st.k == Stmt::Connect) {
      // целевые множества
      std::vector<std::string> fromSet, toSet;
      auto resolve = [&](const std::string& key, std::vector<std::string>& into) {
        if (Cluster* cc = doc.cluster(key)) { into = cc->members; return; }
        for (const auto& cc : doc.clusters)
          if (cc.name == key) { into = cc.members; return; }
        if (doc.zone(key)) {
          for (const auto& n : doc.neurons)
            if (n.zone == key) into.push_back(n.id);
          return;
        }
        into.push_back(key); // конкретный id
      };
      resolve(st.from, fromSet);
      resolve(st.to, toSet);
      if (fromSet.empty()) { errs.push_back({st.line, "нет юнитов у источника '" + st.from + "'"}); continue; }
      if (toSet.empty()) { errs.push_back({st.line, "нет юнитов у приёмника '" + st.to + "'"}); continue; }
      // неизвестные конкретные id — громкая ошибка, а не молчливый пропуск
      {
        bool bad = false;
        for (const auto& fv : fromSet) if (!doc.neuron(fv)) { errs.push_back({st.line, "источник '" + fv + "' не найден"}); bad = true; }
        for (const auto& tv : toSet) if (!doc.neuron(tv)) { errs.push_back({st.line, "приёмник '" + tv + "' не найден"}); bad = true; }
        if (bad) continue;
      }
      auto cp = [&](const char* key) -> const std::vector<Tok>* {
        auto it = st.connParams.find(key);
        return it == st.connParams.end() ? nullptr : &it->second;
      };
      std::string rule = "knn";
      if (const auto* t = cp("rule")) rule = evalStr(*t, st.line, errs);
      long long K = 6; if (const auto* t = cp("k")) { ExprCtx cx; K = (long long)evalParam(*t, cx, errs, st.line); }
      double dens = 1.0; if (const auto* t = cp("density")) { ExprCtx cx; dens = evalParam(*t, cx, errs, st.line); }
      double wBase = 0.5;
      if (const auto* t = cp("weight")) {
        DLexer WL; for (const auto& tk : *t) WL.toks.push_back(tk); WL.toks.push_back(Tok{});
        ExprCtx cx; wBase = evalExpr(WL, cx, errs);
      }
      double wVar = 0.0; if (const auto* t = cp("wVar")) { ExprCtx cx; wVar = evalParam(*t, cx, errs, st.line); }
      long long seedV = 1; if (const auto* t = cp("seed")) { ExprCtx cx; seedV = (long long)evalParam(*t, cx, errs, st.line); }
      std::string signS = "+"; if (const auto* t = cp("sign")) signS = evalStr(*t, st.line, errs);
      std::string plastS = "rigid"; if (const auto* t = cp("plast")) plastS = evalStr(*t, st.line, errs);
      double lr = 0.01, wMinV = 0.0, wMaxV = 1.0, decayV = 0.0;
      long long pwV = 20, delayV = 0;
      auto evalOpt = [&](const char* key, double& out) {
        if (const auto* t = cp(key)) { DLexer WL; for (const auto& tk : *t) WL.toks.push_back(tk); WL.toks.push_back(Tok{}); ExprCtx cx; out = evalExpr(WL, cx, errs); }
      };
      auto evalOptLL = [&](const char* key, long long& out) {
        if (const auto* t = cp(key)) { DLexer WL; for (const auto& tk : *t) WL.toks.push_back(tk); WL.toks.push_back(Tok{}); ExprCtx cx; out = (long long)evalExpr(WL, cx, errs); }
      };
      evalOpt("lr", lr); evalOpt("wMin", wMinV); evalOpt("wMax", wMaxV); evalOpt("decay", decayV);
      evalOptLL("pw", pwV); evalOptLL("delay", delayV);
      // соединения по правилу (4.5)
      long long pair = 0;
      for (size_t a = 0; a < fromSet.size(); ++a) {
        std::vector<std::pair<double, std::string>> cand;
        for (size_t b = 0; b < toSet.size(); ++b) {
          if (fromSet[a] == toSet[b]) continue;
          const Neuron* na = doc.neuron(fromSet[a]);
          const Neuron* nb = doc.neuron(toSet[b]);
          if (!na || !nb) continue;
          double dx = na->pos.x - nb->pos.x, dy = na->pos.y - nb->pos.y, dz = na->pos.z - nb->pos.z;
          cand.push_back({dx * dx + dy * dy + dz * dz, toSet[b]});
        }
        std::sort(cand.begin(), cand.end());
        long long taken = 0;
        for (size_t b = 0; b < cand.size(); ++b) {
          bool take = false;
          if (rule == "full") take = true;
          else if (rule == "knn") take = b < (size_t)std::max(1LL, K);
          else if (rule == "random") {
            long long sd = seedV + (long long)(a * 131 + b);
            sd = (long long)((sd * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
            take = (double)(sd & 0xffffff) / 16777216.0 < dens;
          } else { errs.push_back({st.line, "правило '" + rule + "': knn|random|full"}); break; }
          if (!take) continue;
          ++taken;
          long long sd = seedV + pair;
          sd = (long long)((sd * 1103515245ULL + 12345ULL) & 0x7fffffffULL);
          double r = (double)(sd & 0xffffff) / 16777216.0;
          double wmag = wBase * (1.0 + wVar * (r - 0.5));
          Synapse s;
          s.byCode = true;
          s.pre = fromSet[a];
          s.post = cand[b].second;
          s.weight = OptF(std::max(0.0, wmag));
          s.sign = OptI((long long)(signS == "-" ? SSign::Inh : SSign::Exc));
          s.plast = OptI((long long)(plastS == "hebb" ? Plasticity::Hebb : plastS == "reward" ? Plasticity::RewardGated : plastS == "antihebb" ? Plasticity::AntiHebb : Plasticity::Rigid));
          s.lr = OptF(lr);
          s.pw = OptI(pwV);
          s.wMin = OptF(wMinV);
          s.wMax = OptF(wMaxV);
          s.decay = OptF(decayV);
          s.delay = OptI(delayV);
          Synapse* exs = nullptr;
          for (auto& ss : doc.synapses)
            if (ss.pre == s.pre && ss.post == s.post) { exs = &ss; break; }
          if (exs) {
            if (!exs->byCode) {
              res.conflicts.push_back({std::string("ручная связь ") + s.pre + "→" + s.post + " занята кодом (R2)", exs->id});
              continue;
            }
            double wkeep = exs->w;
            *exs = s;
            exs->w = wkeep; // динамика сохраняется при пересборке кода (R2: не стираем мышью сделанное)
            double idNum = 0; (void)idNum;
            exs->byCode = true;
            res.updated++;
          } else {
            s.id = doc.nextSynId();
            s.w = s.weight.v;
            doc.synapses.push_back(s);
            res.created++;
          }
          ++pair;
        }
      }
      continue;
    }
  }

  // чистка: удаляем byCode-юниты, которых больше нет в коде (код — источник истины для своих юнитов)
  std::vector<std::string> stale;
  for (const auto& n : doc.neurons)
    if (n.byCode && std::find(codeIds.begin(), codeIds.end(), n.id) == codeIds.end())
      stale.push_back(n.id);
  for (const auto& id : stale) {
    if (doc.removeNeuron(id)) res.removed++;
  }

  res.errors = errs;
  res.ok = errs.empty();
  doc.recomputeIdCounters();
  doc.dirty = true;
  return res;
}


// ──────────────────────────── экспорт кода из схемы (4.4) ────────────────────────────
static const char* shapeName(Shape s) {
  return s == Shape::Grid ? "grid" : s == Shape::Box ? "box" : "sphere";
}
static const char* topoName(Topo t) {
  return t == Topo::Grid ? "grid" : t == Topo::Random ? "random" : t == Topo::Full ? "full" : "knn";
}
static const char* modelShort(NModel m) { return m == NModel::ALIF ? "alif" : "lif"; }
static const char* signShort(SSign s)   { return s == SSign::Inh ? "-" : "+"; }

std::string dslExport(const Project& doc) {   // (4.4) сериализация текущей схемы в код
  std::ostringstream o;
  for (const auto& z : doc.zones)
    o << "zone(\"" << z.code << "\", color=\"" << z.color << "\");\n";
  o << "\n";
  for (const auto& c : doc.clusters) {
    o << "cluster \"" << c.name << "\" {\n";
    o << "  zone = \"" << c.zone << "\";\n";
    o << "  shape = \"" << (c.shape.set ? shapeName((Shape)c.shape.v) : "sphere") << "\";\n";
    o << "  center = [" << c.center.x << ", " << c.center.y << ", " << c.center.z << "];\n";
    if (c.radius.set)    o << "  radius = "    << c.radius.v    << ";\n";
    if (c.count.set)     o << "  count = "     << c.count.v     << ";\n";
    o << "  topo = \"" << (c.topo.set ? topoName((Topo)c.topo.v) : "knn") << "\";\n";
    if (c.knnK.set)      o << "  knnK = "      << c.knnK.v      << ";\n";
    if (c.density.set)   o << "  density = "   << c.density.v   << ";\n";
    if (c.thrVar.set)    o << "  thrVar = "    << c.thrVar.v    << ";\n";
    if (c.leakVar.set)   o << "  leakVar = "   << c.leakVar.v   << ";\n";
    if (c.wVar.set)      o << "  wVar = "      << c.wVar.v      << ";\n";
    if (c.seed.set)      o << "  seed = "      << c.seed.v      << ";\n";
    if (c.neuroProb.set) o << "  neuroProb = " << c.neuroProb.v << ";\n";
    if (c.pruneProb.set) o << "  pruneProb = " << c.pruneProb.v << ";\n";
    o << "}\n";
  }
  if (!doc.clusters.empty()) o << "\n";
  for (const auto& n : doc.neurons) {
    if (n.byCode) continue;             // код-юниты выводятся кластерами/повторами (R2)
    if (!n.type.set) continue;
    o << ntypeName((NType)n.type.v) << " {\n";
    o << "  zone = \"" << n.zone << "\";\n";
    if (n.model.set)    o << "  model = \""   << modelShort((NModel)n.model.v) << "\";\n";
    if (n.thr.set)      o << "  thr = "       << n.thr.v  << ";\n";
    if (n.leak.set)     o << "  leak = "      << n.leak.v << ";\n";
    if (n.refrMax.set)  o << "  refrMax = "   << n.refrMax.v << ";\n";
    if (n.noise.set)    o << "  noise = "     << n.noise.v << ";\n";
    if (n.trace.set)    o << "  trace = "     << n.trace.v << ";\n";
    if (n.homeoOn)      o << "  homeoOn = 1;\n";
    if (n.homeoRate.set) o << "  homeoRate = " << n.homeoRate.v << ";\n";
    if (n.target.set)   o << "  target = "    << n.target.v << ";\n";
    if (n.integSec.set) o << "  integSec = "  << n.integSec.v << ";\n";
    if (n.kThr.set)     o << "  kThr = "      << n.kThr.v << ";\n";
    if (n.kPlast.set)   o << "  kPlast = "    << n.kPlast.v << ";\n";
    if (n.rate.set)     o << "  rate = "      << n.rate.v << ";\n";
    if (n.mkind.set)    o << "  mkind = \""   << (n.mkind.v == (long long)ModKind::Punishment ? "punishment" : n.mkind.v == (long long)ModKind::Novelty ? "novelty" : "dopamine") << "\";\n";
    if (!n.growth.empty()) {
      o << "  growth = [";
      for (size_t gi = 0; gi < n.growth.size(); ++gi)
        o << (gi ? ", " : "") << "\"" << n.growth[gi] << "\"";
      o << "];\n";
    }
    o << "  at [" << n.pos.x << ", " << n.pos.y << ", " << n.pos.z << "]\n";
    o << "}\n";
  }
  o << "\n";
  for (const auto& s : doc.synapses) {
    o << "connect(\"" << s.pre << "\" -> \"" << s.post << "\", rule=\"full\"";
    if (s.weight.set) o << ", weight=" << s.weight.v;
    if (s.sign.set)   o << ", sign=\"" << signShort((SSign)s.sign.v) << "\"";
    if (s.plast.set)  o << ", plast=\"" << plasticityName((Plasticity)s.plast.v) << "\"";
    if (s.lr.set)     o << ", lr="     << s.lr.v;
    if (s.pw.set)     o << ", pw="     << s.pw.v;
    if (s.wMin.set)   o << ", wMin="   << s.wMin.v;
    if (s.wMax.set)   o << ", wMax="   << s.wMax.v;
    if (s.decay.set)  o << ", decay="  << s.decay.v;
    if (s.delay.set)  o << ", delay="  << s.delay.v;
    o << ");\n";
  }
  return o.str();
}
} // namespace mp
