#pragma once
// gui/panels.hpp — все 2D-панели: HUD (6.6), инспектор (1.x/2.x/3.x),
// код (4.x), файлы (7.x), стенды (9.x), флаги (8.3), диалоги (5.1/5.2/5.9).
#include "app.hpp"

namespace mpui {

// рисует весь 2D-слой; заполняет app.overAny
void panelsDraw(App& app);

} // namespace mpui
