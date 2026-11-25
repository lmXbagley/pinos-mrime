#include "test_util.hpp"

#include "mp/dsl.hpp"

using namespace mp;

static OptF OF(double v) { return OptF(v); }
static OptI OI(long long v) { return OptI(v); }

static Project manualDoc() {
  Project doc;
  Zone z; z.code = "V1"; z.color = "#123456";
  doc.zones.push_back(z);
  Neuron n;                        // РУЧНОЙ юнит (мышь): created NOT by code
  n.type = OI((long long)NType::Hidden); n.zone = "V1"; n.pos = {1, 1, 1};
  n.thr = OF(1.0); n.leak = OF(0.8); n.refrMax = OI(3); n.noise = OF(0); n.trace = OI(0);
  doc.addNeuron(n);
  return doc;
}

// 4.2 + R2: сборка кода создаёт byCode-юниты и НЕ трогает ручные
MPT_TEST(dsl_build_creates_bycode_units) {
  Project doc = manualDoc();
  int manualBefore = (int)doc.neurons.size() - 1;
  std::string code =
      "zone(\"V1\", color=\"#123456\");\n"
      "hidden { zone=\"V1\"; thr=1.0; leak=0.8; refrMax=3; noise=0.0; trace=0; at [5,0,0] }\n";
  BuildResult r = dslBuild(doc, code);
  CHECK(r.ok, "build ok");
  if (!r.ok) { for (auto& e : r.errors) std::fprintf(stderr, "  err L%d: %s\n", e.line, e.msg.c_str()); }
  CHECK(r.created == 1, "one unit created");
  CHECK((int)doc.neurons.size() == manualBefore + 2, "manual unit untouched");
  int byCodeCount = 0, manualCount = 0;
  for (const auto& n : doc.neurons) (n.byCode ? byCodeCount : manualCount)++;
  CHECK(byCodeCount == 1 && manualCount == 1, "R2: byCode/manual separated");
}

// 4.6/5.x + R2: пересборка НЕ стирает юниты, созданные мышью; старые byCode заменяются
MPT_TEST(dsl_rebuild_preserves_manual) {
  Project doc = manualDoc();
  std::string code1 =
      "hidden { zone=\"V1\"; thr=1.0; leak=0.8; refrMax=3; noise=0.0; trace=0; at [5,0,0] }\n";
  BuildResult r1 = dslBuild(doc, code1);
  CHECK(r1.ok && doc.neurons.size() == 2, "first build");
  std::string manualId;
  for (const auto& n : doc.neurons) if (!n.byCode) manualId = n.id;
  std::string code2 =
      "hidden { zone=\"V1\"; thr=2.0; leak=0.8; refrMax=3; noise=0.0; trace=0; at [7,0,0] }\n";
  BuildResult r2 = dslBuild(doc, code2);
  CHECK(r2.ok, "second build ok");
  CHECK(doc.neurons.size() == 2, "manual unit survives rebuild (R2)");
  CHECK(doc.neuron(manualId) != nullptr, "R2: exactly that manual unit survives");
  int cs = 0; for (const auto& n : doc.neurons) if (n.byCode) cs++;
  CHECK(cs == 1, "stale byCode replaced, not duplicated");
}

// 4.3: ошибки сборки с номером строки
MPT_TEST(dsl_error_line_numbers) {
  Project doc = manualDoc();
  std::string code = "zone(\"V1\");\n\n  oops\n";
  BuildResult r = dslBuild(doc, code);
  CHECK(!r.ok, "build fails");
  CHECK(!r.errors.empty() && r.errors.back().line == 3, "error has line 3");
}

// 2.x: connect создаёт связь со всеми параметрами; повторный connect правит ту же связь
MPT_TEST(dsl_connect_then_reconnect_edits) {
  Project doc = manualDoc();
  std::string code =
      "hidden { zone=\"V1\"; thr=1.0; leak=0.8; refrMax=3; noise=0.0; trace=0; at [0,0,0] }\n";
  dslBuild(doc, code);
  std::string hid;
  for (const auto& n : doc.neurons) if (n.byCode) hid = n.id;
  std::string code2 =
      "connect(\"V1.S.1\" -> \"" + hid + "\", \"?\");\n";  // несуществующий сенсор — ошибка, а не молчание
  BuildResult r0 = dslBuild(doc, code2);
  CHECK(!r0.ok || r0.conflicts.size() > 0 || !r0.errors.empty(),
        "link to unknown pre fails loudly");
  CHECK(doc.synapses.empty(), "nothing silently created");

  std::string code3 =
      "connect(\"" + hid + "\" -> \"V1.S.1\");\n";  // post тоже неизвестен
  BuildResult r1 = dslBuild(doc, code3);
  CHECK(!r1.ok || r1.conflicts.size() > 0 || !r1.errors.empty(), "unknown post fails loudly");
  CHECK(doc.synapses.empty(), "still nothing silently created");
}

// 3.x: кластер разворачивается в нейроны с дет. вариацией параметров от seed
MPT_TEST(dsl_cluster_expansion) {
  Project doc;
  Zone z; z.code = "V1"; doc.zones.push_back(z);
  std::string code =
      "zone(\"V1\");\n"
      "cluster \"midbrain\" { zone=\"V1\"; shape=\"sphere\"; center=[0,0,0]; radius=5;\n"
      "  count=8; topo=\"knn\"; knnK=2; density=0.5; thrVar=0.1; leakVar=0.05;\n"
      "  wVar=0.02; seed=7; neuroProb=0; pruneProb=0; }\n";
  BuildResult r = dslBuild(doc, code);
  CHECK(r.ok, "cluster build ok");
  CHECK(doc.neurons.size() == 8, "cluster expanded into count neurons");
  if (!r.ok) for (auto& e : r.errors) std::fprintf(stderr, "  err L%d: %s\n", e.line, e.msg.c_str());
  CHECK(doc.clusters.size() == 1 && doc.clusters[0].members.size() == 8, "members linked");
  // детерминизм той же сборки: тот же seed → те же пороги
  Project doc2;
  Zone z2; z2.code = "V1"; doc2.zones.push_back(z2);
  dslBuild(doc2, code);
  CHECK(doc2.neurons.size() == doc.neurons.size(), "same expansion scale");
  double thrA = doc.neurons[0].thr.v, thrB = doc2.neurons[0].thr.v;
  CHECK(mpt::near(thrA, thrB, 1e-12), "seeded variation deterministic");
  // вариация реально применена (thrVar=0.1): не все пороги равны базовому
  bool anyDiff = false;
  for (const auto& n : doc.neurons) if (!mpt::near(n.thr.v, 0.0)) anyDiff = true;
  CHECK(anyDiff, "variation applied");
  // внутренние связи knn
  CHECK(!doc.synapses.empty(), "knn topo created internal links");
  for (const auto& s : doc.synapses) CHECK(s.byCode, "R2: cluster synapses are byCode");
  for (const auto& n : doc.neurons) {
    CHECK(n.byCode, "R2: cluster neurons are byCode");
    CHECK(n.srcCluster == doc.clusters[0].id, "3.3: srcCluster set");
  }
}

// 4.4: экспорт кода из схемы и обратная сборка не теряют юниты
MPT_TEST(dsl_export_rebuild_roundtrip) {
  Project doc = manualDoc();
  dslBuild(doc,
      "hidden { zone=\"V1\"; thr=1.0; leak=0.8; refrMax=3; noise=0.0; trace=0; at [5,0,0] }\n"
      "cluster \"c1\" { zone=\"V1\"; shape=\"box\"; center=[3,3,3]; radius=2; count=4;\n"
      "  topo=\"full\"; thrVar=0.0; leakVar=0.0; wVar=0.0; seed=1; neuroProb=0; pruneProb=0; }\n");
  size_t before = doc.neurons.size();
  std::string code = dslExport(doc);
  BuildResult r = dslBuild(doc, code);
  if (!r.ok) for (auto& e : r.errors) std::fprintf(stderr, "  re-export err L%d: %s\n", e.line, e.msg.c_str());
  CHECK(r.ok, "exported code rebuilds ok");
  CHECK(doc.neurons.size() == before, "export->build keeps all units (manual + cluster)");
  bool sawManual = false;
  for (const auto& n : doc.neurons) if (!n.byCode || n.srcCluster.empty()) sawManual = sawManual || !n.byCode;
  CHECK(sawManual, "manual unit still there");
}

// repeat N bygrid WxH at [...] + строковые параметры enc/src: обе ветки раньше падали
// (лексер склеивал «5x2» в Num+Ident, at-вектор терял скобки, Str-параметры шли в evalParam)
MPT_TEST(dsl_repeat_grid_str_params) {
  Project doc;
  std::string code =
      "zone(\"RA\");\n"
      "repeat 4 bygrid 2x2 at [-2, 0, -1] step 2 { sensor { zone=\"RA\"; enc=\"rate\"; fireThr=0.2 } }\n"
      "hidden { zone=\"RA\"; enc=\"rate\"; pos=[0, 0, 0] }\n"
      "connect(\"RA\" -> \"RA.H.1\", rule=\"full\", weight=0.55, delay=1);\n";
  BuildResult r = dslBuild(doc, code);
  if (!r.ok) for (auto& e : r.errors) std::fprintf(stderr, "  err L%d: %s\n", e.line, e.msg.c_str());
  CHECK(r.ok, "repeat grid + str params build ok");
  CHECK(r.created == 10, "1 zone + 4 retina + 1 hidden + 4 syn");
  int sensors = 0;
  for (const auto& n : doc.neurons) {
    if (n.type.set && n.type.v == (long long)NType::Sensor) {
      ++sensors;
      CHECK(n.enc.v == (long long)SensorEncoding::Rate, "enc=rate applied without spurious errors");
    }
  }
  CHECK(sensors == 4, "retina ids");
  // сетка 2x2: x = -2..0, z строка одна плоскость at: gx->x, gy->y
  const Neuron* a = doc.neuron("RA.S.1");
  const Neuron* b = doc.neuron("RA.S.4");
  CHECK(a && b && mpt::near(a->pos.x, -2.0) && mpt::near(b->pos.x, 0.0), "bygrid laid cells out");
  CHECK(doc.synapses.size() == 4, "zone->unit full rule made 4 synapses");
}
