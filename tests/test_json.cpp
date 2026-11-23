#include "test_util.hpp"

#include "mp/json.hpp"

using namespace mp;

MPT_TEST(json_parse_dump_roundtrip) {
  std::string txt = "{\"a\":1,\"b\":[true,false,null,\"x,y\"],\"c\":{\"d\":-2.5e2}}";
  Json j;
  std::string err; int line = 0;
  CHECK(Json::parse(txt, j, err, line), "parse ok");
  CHECK(j.isObj() && j.has("a"), "obj + key a");
  CHECK(mpt::near(j.at("a").asNum(), 1), "a == 1");
  CHECK(j.at("b").size() == 4, "b has 4 elems");
  CHECK(j.at("b").at(0).isBool(), "b[0] bool");
  CHECK(j.at("b").at(2).isNull(), "b[2] null");
  CHECK(j.at("b").at(3).asStr() == "x,y", "b[3] string");
  CHECK(mpt::near(j.at("c").at("d").asNum(), -250), "c.d == -250");
  std::string txt2 = j.dump();
  Json j2; CHECK(Json::parse(txt2, j2, err, line), "dump reparse");
  CHECK(mpt::near(j2.at("c").at("d").asNum(), -250), "roundtrip value");
}

MPT_TEST(json_string_escapes) {
  Json j;
  std::string err; int line;
  CHECK(Json::parse("\"line\\na\\t\\u0041\\\"q\\\\\"", j, err, line), "escape parse");
  CHECK(j.asStr() == std::string("line\na\tA\"q\\"), "escape decode");
  Json o = Json::str("та\"б\t\\\n");
  std::string dumped = o.dump();
  Json p; CHECK(Json::parse(dumped, p, err, line), "escape dump reparse");
  CHECK(p.asStr() == o.asStr(), "escape roundtrip");
}

MPT_TEST(json_parse_errors) {
  Json j; std::string err; int line = 0;
  CHECK(!Json::parse("{\"a\":}", j, err, line) && !err.empty(), "error reported");
  CHECK(!Json::parse("{\"a\":1\n\"b\":2}", j, err, line), "no trailing junk accepted");
}

MPT_TEST(json_object_mutation) {
  Json o = Json::object();
  o.set("k", Json::number(7));
  Json a = Json::array(); a.push_back(Json::str("s"));
  o.set("arr", a);
  CHECK(o.at("arr").at(0).asStr() == "s", "array push_back");
  CHECK(o.at("k").asInt() == 7, "asInt exact read");
}
