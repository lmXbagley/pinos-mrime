#pragma once
// gui/scene.hpp — 3D-вьюпорт: honest picking (5.7), отрисовка (6.x), оверлеи.
#include "app.hpp"

namespace mpui {

// обновляет app.hoverN/hoverS по позиции мыши
void scenePick(App& app);
// вся 3D-геометрия: сетка, кластеры, нейроны, связи, импульсы, предпросмотры
void sceneDraw3D(App& app);
// поверх 2D: ID-билборды (6.1), рамка выделения, хинт режима
void sceneOverlay2D(App& app);

} // namespace mpui
