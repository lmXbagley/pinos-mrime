#pragma once
// mp/dsl.hpp — окно кода (4.1–4.6): OpenSCAD-модель DSL.
//
// ГРАММАТИКА (минимальная, п.4.5; документ minos_prime_spec.md недоступен —
// грамматика составлена по директивам v0.2 и ЗАФИКСИРОВАНА здесь до получения оригинала):
//
//   zone("V1", color="#88aaff");                       // зона
//   повтор 8 по сетке 4x2 at [0,0,0] step 2 {
//     sensor { zone="V1"; thr=1.0+0.01*i; leak=0.02; refrMax=3; noise=0.0; trace=5; }
//   }                                                  // циклы повтора «повтор N по сетке WxH»
//   hidden { zone="V1"; thr=1.0; ...; model="lif|alif"; homeoOn=true; homeoRate=0.2; }
//   motor  { ... }
//   modulator("dopamine"){ scope="zone"; zone="V1"; windowSec=2.0; rate=1.0; }
//   homeostat { target=5; integSec=1.0; kThr=0.5; kPlast=0.2; }
//   cluster "midbrain" { zone="V1"; shape="sphere"; center=[0,5,0]; radius=10; count=64;
//                        topo="knn"; knnK=6; density=0.2;
//                        thrVar=0.1; leakVar=0.05; wVar=0.05; seed=7;
//                        neuroProb=0.01; pruneProb=0.01; growZones=["V2","V3"]; }
//   connect("midbrain" -> "V1", rule="knn", k=6, weight=uniform(0.2,0.8,7),
//           sign="+|-", plast="rigid|hebb|reward|antihebb", lr=0.01, pw=20,
//           wMin=0, wMax=1, decay=0.001, delay=2);
//
// R2: код — операция НАД документом: создаёт/обновляет только то, что объявлено кодом
// (byCode), ручные юниты не трогает; конфликты (ручной юнит с тем же ID) возвращаются списком.
#include "mp/model.hpp"

#include <string>
#include <vector>

namespace mp {

struct DslError {
  int line = 0;        // (4.3) номер строки для клика
  std::string msg;
};

struct DslConflict {   // R2: конфликт показывается, не разрешается молча
  std::string txt;
  std::string unit;
};

struct BuildResult {
  bool ok = false;
  int created = 0, updated = 0, removed = 0;
  std::vector<DslError> errors;
  std::vector<DslConflict> conflicts;
};

BuildResult dslBuild(Project& doc, const std::string& code);  // 4.2 «Собрать»
std::string dslExport(const Project& doc);                    // 4.4 «Экспорт кода из схемы»

} // namespace mp
