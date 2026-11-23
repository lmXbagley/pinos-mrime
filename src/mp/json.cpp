#include "mp/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace mp {

const Json Json::NULL_ = Json::nul();

size_t Json::size() const { return type_ == Arr ? a_.size() : 0; }
const Json& Json::at(size_t i) const { return (type_ == Arr && i < a_.size()) ? a_[i] : NULL_; }
void Json::push_back(Json v) { if (type_ == Arr) a_.push_back(std::move(v)); }

bool Json::has(const std::string& key) const {
  if (type_ != Obj) return false;
  for (const auto& kv : o_) if (kv.first == key) return true;
  return false;
}
const Json& Json::at(const std::string& key) const {
  if (type_ == Obj)
    for (const auto& kv : o_) if (kv.first == key) return kv.second;
  return NULL_;
}
void Json::set(const std::string& key, Json v) {
  if (type_ != Obj) { type_ = Obj; }
  for (auto& kv : o_) if (kv.first == key) { kv.second = std::move(v); return; }
  o_.emplace_back(key, std::move(v));
}
std::vector<std::string> Json::keys() const {
  std::vector<std::string> r;
  if (type_ == Obj) for (const auto& kv : o_) r.push_back(kv.first);
  return r;
}

static void dumpNum(double v, std::string& out) {
  if (std::isnan(v) || std::isinf(v)) { out += "0"; return; }
  char buf[64];
  if (v == (double)(long long)v && std::fabs(v) < 9e15)
    std::snprintf(buf, sizeof buf, "%lld", (long long)v);
  else
    std::snprintf(buf, sizeof buf, "%.17g", v);
  out += buf;
}

static void dumpStr(const std::string& s, std::string& out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else out += (char)c;
    }
  }
  out += '"';
}

bool Json::dumpTo(std::string& out, int indent, int depth) const {
  auto nl = [&](int d) {
    if (indent <= 0) return;
    out += '\n';
    out.append((size_t)(indent * d), ' ');
  };
  switch (type_) {
    case Null: out += "null"; break;
    case Bool: out += b_ ? "true" : "false"; break;
    case Num: dumpNum(n_, out); break;
    case Str: dumpStr(s_, out); break;
    case Arr: {
      out += '[';
      for (size_t i = 0; i < a_.size(); ++i) {
        if (i) out += ',';
        nl(depth + 1);
        if (!a_[i].dumpTo(out, indent, depth + 1)) return false;
      }
      if (!a_.empty()) nl(depth);
      out += ']';
      break;
    }
    case Obj: {
      out += '{';
      for (size_t i = 0; i < o_.size(); ++i) {
        if (i) out += ',';
        nl(depth + 1);
        dumpStr(o_[i].first, out);
        out += ':';
        if (indent > 0) out += ' ';
        if (!o_[i].second.dumpTo(out, indent, depth + 1)) return false;
      }
      if (!o_.empty()) nl(depth);
      out += '}';
      break;
    }
  }
  return true;
}

std::string Json::dump() const { std::string s; dumpTo(s, 0, 0); return s; }
std::string Json::dump(int indent) const { std::string s; dumpTo(s, indent, 0); return s; }

namespace {
struct P {
  const char* p; const char* end; std::string err; int line = 1;
  bool fail(const std::string& m) {
    if (err.empty()) err = m + " (line " + std::to_string(line) + ")";
    return false;
  }
  void ws() {
    for (;;) {
      while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
      if (p < end && *p == '\n') { ++line; ++p; continue; }
      return;
    }
  }
  bool value(Json& out, int depth) {
    if (depth > 256) return fail("too deep");
    ws();
    if (p >= end) return fail("unexpected end");
    char c = *p;
    if (c == '{') return obj(out, depth);
    if (c == '[') return arr(out, depth);
    if (c == '"') { std::string sv; if (!str(sv)) return false; out = Json::str(sv); return true; }
    if (c == 't') { return lit("true", Json::boolean(true), out); }
    if (c == 'f') { return lit("false", Json::boolean(false), out); }
    if (c == 'n') { return lit("null", Json::nul(), out); }
    return num(out);
  }
  bool lit(const char* s, Json&& v, Json& out) {
    size_t n = std::char_traits<char>::length(s);
    if ((size_t)(end - p) < n || std::char_traits<char>::compare(p, s, n) != 0)
      return fail(std::string("invalid literal, expected ") + s);
    p += n;
    out = std::move(v);
    return true;
  }
  bool num(Json& out) {
    const char* start = p;
    if (p < end && (*p == '-' || *p == '+')) ++p;
    bool digits = false;
    while (p < end && *p >= '0' && *p <= '9') { ++p; digits = true; }
    if (p < end && *p == '.') { ++p; while (p < end && *p >= '0' && *p <= '9') { ++p; digits = true; } }
    if (p < end && (*p == 'e' || *p == 'E')) {
      ++p;
      if (p < end && (*p == '-' || *p == '+')) ++p;
      while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    if (!digits) return fail("invalid number");
    double v = std::strtod(std::string(start, p).c_str(), nullptr);
    out = Json::number(v);
    return true;
  }
  bool str(std::string& out) {
    if (p >= end || *p != '"') return fail("expected string");
    ++p;
    out.clear();
    for (;;) {
      if (p >= end) return fail("unterminated string");
      char c = *p;
      if (c == '"') { ++p; return true; }
      if (c == '\n') return fail("newline in string");
      if ((unsigned char)c < 0x20) return fail("control char in string");
      if (c == '\\') {
        ++p;
        if (p >= end) return fail("bad escape");
        char e = *p++;
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
              if (p >= end) return fail("bad \\u");
              char h = *p++;
              code <<= 4;
              if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
              else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
              else return fail("bad \\u digit");
            }
            // суррогаты
            if (code >= 0xD800 && code <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
              unsigned lo = 0; const char* save = p; p += 2;
              for (int i = 0; i < 4; ++i) {
                char h = *p++; lo <<= 4;
                if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                else { p = save - 2; return fail("bad low surrogate"); }
              }
              code = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
            }
            // UTF-8 encode
            if (code < 0x80) out += (char)code;
            else if (code < 0x800) { out += (char)(0xC0 | (code >> 6)); out += (char)(0x80 | (code & 63)); }
            else if (code < 0x10000) {
              out += (char)(0xE0 | (code >> 12)); out += (char)(0x80 | ((code >> 6) & 63)); out += (char)(0x80 | (code & 63));
            } else {
              out += (char)(0xF0 | (code >> 18)); out += (char)(0x80 | ((code >> 12) & 63));
              out += (char)(0x80 | ((code >> 6) & 63)); out += (char)(0x80 | (code & 63));
            }
            break;
          }
          default: return fail("bad escape char");
        }
        continue;
      }
      out += c; ++p;
    }
  }
  bool arr(Json& out, int depth) {
    ++p; out = Json::array();
    ws();
    if (p < end && *p == ']') { ++p; return true; }
    for (;;) {
      Json v;
      if (!value(v, depth + 1)) return false;
      out.push_back(std::move(v));
      ws();
      if (p < end && *p == ',') { ++p; continue; }
      if (p < end && *p == ']') { ++p; return true; }
      return fail("expected ',' or ']'");
    }
  }
  bool obj(Json& out, int depth) {
    ++p; out = Json::object();
    ws();
    if (p < end && *p == '}') { ++p; return true; }
    for (;;) {
      ws();
      std::string k;
      if (!str(k)) return false;
      ws();
      if (p >= end || *p != ':') return fail("expected ':'");
      ++p;
      Json v;
      if (!value(v, depth + 1)) return false;
      out.set(k, std::move(v));
      ws();
      if (p < end && *p == ',') { ++p; continue; }
      if (p < end && *p == '}') { ++p; return true; }
      return fail("expected ',' or '}'");
    }
  }
};
} // namespace

bool Json::parse(const std::string& text, Json& out, std::string& err, int& line) {
  P ps{text.data(), text.data() + text.size(), {}, 1};
  bool ok = ps.value(out, 0);
  if (ok) {
    ps.ws();
    if (ps.p != ps.end) ok = ps.fail("trailing garbage");
  }
  err = ps.err;
  line = ps.line;
  if (!ok) out = Json::nul();
  return ok;
}

} // namespace mp
