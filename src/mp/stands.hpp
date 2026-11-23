#pragma once
// mp/stands.hpp — именованные стенды (9.1: CRUD; 9.2: запись/воспроизведение;
// 9.3: привязка к хешу схемы; 9.4: регрессия).
#include "mp/engine.hpp"
#include "mp/model.hpp"

#include <string>
#include <vector>

namespace mp {

struct StandRunResult {
  std::string name;      // (9.1)
  bool hashMismatch = false; // (9.3) сигнализировать о несовпадении хеша схемы
  bool replayOk = false;     // (9.2) события применились без ошибок
  long long spikesTotal = 0;
  double spikesPerSec = 0;
  std::string resultHash;    // хеш траектории активности этого прогона
  bool regressionOk = false; // (9.4) resultHash совпал с эталоном stand.resultHash
};

// --- CRUD именованных стендов (9.1) ---
Stand*       findStand(Project& doc, const std::string& name);
const Stand* findStand(const Project& doc, const std::string& name);
Stand&       createStand(Project& doc, const std::string& name, double durationSec = 5.0);
bool         removeStand(Project& doc, const std::string& name);

// --- запись (9.2) ---
void recordEvent(Project& doc, const std::string& standName, const StandEvent& ev);
void registerRecordedStand(Project& doc, const std::string& standName, double durationSec);
//   recordEvent копит события; registerRecordedStand фиксирует stand.schemaHash (9.3)
//   и эталонный resultHash после эталонного прогона.

// --- воспроизведение (9.2) ---
// doc НЕ мутируется структурно: прогон идёт на глубокой копии; события доливаются
// в Engine по времени (t в секундах, speed влияет только на длину стены реального
// времени в GUI; здесь — детерминированный прогон по тактам).
StandRunResult runStand(const Project& doc, const Stand& stand, double speed = 1.0);

// --- регрессия (9.4): все стенды, pass/fail по каждому ---
std::vector<StandRunResult> runRegression(const Project& doc);

} // namespace mp
