#pragma once
// mp/validator.hpp — флаги схемы (8.1 красные, 8.2 жёлтые, 8.3 клик по флагу → камера).
#include "mp/model.hpp"

#include <vector>

namespace mp {

// Полный список флагов текущей схемы; у каждого флага unit=id юнита (8.3, K10) —
// HUD показывает список, клик направляет камеру на юнит.
std::vector<Flag> validate(const Project& doc);

bool hasRedFlags(const std::vector<Flag>& flags);
bool hasYellowFlags(const std::vector<Flag>& flags);

} // namespace mp
