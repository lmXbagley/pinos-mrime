#pragma once
// mp/serialize.hpp — Save/Load ВСЕХ полей (R5). После load вызывается recomputeIdCounters().
#include "mp/model.hpp"

namespace mp {

std::string saveProjectJson(const Project& p);          // полный дамп (разделы 1–3 + проектные)
bool loadProjectJson(const std::string& text, Project& out, std::string& err);

// Экпорт/импорт схемы в JSON с полной схемой полей (7.4) — тот же формат.
inline std::string exportSchemaJson(const Project& p) { return saveProjectJson(p); }
inline bool importSchemaJson(const std::string& t, Project& out, std::string& e) {
  return loadProjectJson(t, out, e);
}

} // namespace mp
