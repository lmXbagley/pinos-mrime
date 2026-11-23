#pragma once
// mp/json.hpp — минимальный, но полный JSON (RFC 8259) reader/writer. Без зависимостей.
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mp {

class Json {
public:
  enum Type { Null, Bool, Num, Str, Arr, Obj };

  Json() : type_(Null) {}
  static Json nul() { return Json(Null); }
  static Json boolean(bool b) { Json j(Bool); j.b_ = b; return j; }
  static Json number(double v) { Json j(Num); j.n_ = v; return j; }
  static Json str(const std::string& s) { Json j(Str); j.s_ = s; return j; }
  static Json array() { return Json(Arr); }
  static Json object() { return Json(Obj); }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Null; }
  bool isBool() const { return type_ == Bool; }
  bool isNum() const { return type_ == Num; }
  bool isStr() const { return type_ == Str; }
  bool isArr() const { return type_ == Arr; }
  bool isObj() const { return type_ == Obj; }

  bool asBool(bool dft = false) const { return isBool() ? b_ : dft; }
  double asNum(double dft = 0) const { return isNum() ? n_ : dft; }
  // asInt: только точное чтение числа как целого (для счётчиков ID)
  long long asInt(long long dft = 0) const {
    if (!isNum()) return dft;
    return (long long)n_ == n_ ? (long long)n_ : dft;
  }
  const std::string& asStr() const { return s_; }

  // массив
  size_t size() const;
  const Json& at(size_t i) const;
  void push_back(Json v);

  // объект
  bool has(const std::string& key) const;
  const Json& at(const std::string& key) const;   // отсутствие → Json::nul()
  void set(const std::string& key, Json v);
  std::vector<std::string> keys() const;

  std::string dump() const;           // компактный
  std::string dump(int indent) const; // pretty

  // Парсер: возвращает false при ошибке (err = описание, line = 1-based)
  static bool parse(const std::string& text, Json& out, std::string& err, int& line);

private:
  explicit Json(Type t) : type_(t) {}
  Type type_;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  std::vector<Json> a_;
  std::vector<std::pair<std::string, Json>> o_; // упорядоченный (стабильный save)

  static const Json NULL_;
  bool dumpTo(std::string& out, int indent, int depth) const;
};

} // namespace mp
