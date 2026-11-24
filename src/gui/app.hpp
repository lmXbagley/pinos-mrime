#pragma once
// gui/app.hpp — состояние десктоп-приложения MINOSPRIME CAD.
// Режимы мыши 5.1–5.10, код поверх документа 4.x (R2), стенды 9.x, файлы 7.x,
// HUD 6.6/8.3. Формулы движка не трогаются (R3) — GUI только вью+редактор.
#include "../mp/api.hpp"
#include "../mp/dsl.hpp"
#include "../mp/engine.hpp"
#include "../mp/model.hpp"
#include "../mp/stands.hpp"
#include "../mp/undo.hpp"
#include "../mp/validator.hpp"

#include "camera.hpp"
#include "widgets.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace mpui {

enum class Mode { Select = 0, Neuron, Wire, Cut, Damage, Move };

struct PendingPlace {         // 5.1: окно параметров ДО установки ячейки
  bool open = false;
  mp::V3 pos{};
  mp::Neuron n;
};

struct PendingSyn {           // 5.2: окно параметров связи (prev/post перепривязываемы)
  bool open = false;
  mp::Synapse s;
  bool editing = false;
  std::string editId;
};

struct App {
  // ---- документ + ядро ----
  mp::Project doc;
  mp::Engine eng{doc};
  mp::UndoStack undo;
  std::vector<mp::Flag> flags;

  // ---- камера / ввод ----
  Camera cam;
  Mode mode = Mode::Select;
  std::set<std::string> selN, selS;
  std::string hoverN, hoverS;
  bool camDrag = false, camPan = false;
  bool pressValid = false;
  std::string pressN, pressS;
  float pressX = 0, pressY = 0;
  mp::V3 pressPos{};
  bool pressMoved = false;
  double pressT = 0;             // время удержания для "потока" (5.9)
  bool flowPending = false;
  bool rubber = false;           // рамка выделения
  float rubX0 = 0, rubY0 = 0, rubX1 = 0, rubY1 = 0;
  mp::V3 lastPos{};              // позиция курсора на рабочей плоскости

  // ---- незавершённые операции ----
  bool wirePre = false;          // выбрана ли точка "из" в режиме связи
  std::string wireFrom;
  PendingPlace place;
  PendingSyn synWin;
  bool flowOpen = false;
  std::string flowId;
  double flowHz = 0;
  bool fillOpen = false;
  std::string fillId;
  double fillFrac = 0;
  bool confirmOpen = false;
  std::string confirmTxt;
  int confirmWhat = 0;           // 1 удалить юниты 2 перерезать связь 3 новая схема
  std::string confirmArg;

  // ---- код (4.x) ----
  std::string codeText = "# код поверх схемы: зона(\"V1\"); sensor { zone=\"V1\"; thr=1; }\n";
  int codeCursor = 0;
  float codeScroll = 0;
  int codeErrLine = -1;
  mp::BuildResult lastBuild;
  float errScroll = 0;
  bool codeTabErrors = false;
  bool wantFs = false;
  bool winCode = true, winInsp = true, winStands = false, winFiles = false, winHelp = false;

  // ---- файлы (7.x) ----
  std::string filePath;
  std::string filePathEdit;
  double autosaveT = 0;
  // ---- состояние панелей ----
  std::map<std::string, bool> sectOpen;   // секции инспектора
  int stSel = -1;                          // выбранный стенд
  float filesScroll = 0, stListScroll = 0;
  std::string statusMsg;                   // строка статуса (сохранено/собрано)
  double lastAutosaveW = -1;
  std::vector<std::string> recent;

  // ---- стенды (9.x) ----
  bool recording = false;
  std::string recName;
  double recT = 0;
  std::vector<mp::StandRunResult> lastReport;
  float stScroll = 0;

  // ---- симуляция ----
  bool running = false;
  double speed = 1.0;
  double tickAcc = 0;

  // ---- API (10.x) ----
  bool apiOn = false;
  int apiPort = 18080;
  std::string apiStatus = "выкл";
  std::unique_ptr<mp::ApiServer> api;

  // ---- буфер обмена (5.4) ----
  std::vector<mp::Neuron> clipN;
  std::vector<mp::Synapse> clipS;

  // ---- представление (6.x) ----
  bool showIds = true;
  bool weightMap = false;        // активность / карта весов (6.2)
  std::map<std::string, double> flash;      // вспышка спайка
  std::map<std::string, bool> lastSpiked;
  struct Imp { std::string sid; std::string pre, post; double t; double w; };
  std::vector<Imp> impulses;     // бегущие импульсы (view-layer, 6.3)
  std::string focusUnit;         // подсветка соседей (6.5)

  // ---- прочее ----
  UI ui;
  int lastInspectUndo = 0; // защита от дублирования снапшотов за кадр
  std::string selCluster;  // выбранный кластер (3.x)
  bool rubberPending = false; // ожидание подтверждения рамки после отпускания
  bool movePushed = false;    // снапшот на групповое перемещение уже снят
  std::map<std::string, mp::V3> dragRel; // смещения юнитов при групповом_drag
  double frameDt = 0;
  int menuCtx = 0;             // открытое контекстное меню (0 нет)
  std::string menuArg;         // аргумент меню (id юнита/кластера)
  float flagsScroll = 0;
  bool gizmoOn = false;    // G: осевой перенос выделенного (FreeCAD-стиль)
  int gizmoAxis = -1;      // 0=X 1=Y 2=Z, -1 = ось не выбрана
  std::vector<std::string> gizmoIds;
  std::vector<mp::V3> gizmoStart;
  mp::V3 gizmoOrig{};
  float gizmoGrab = 0;
  bool gizmoDrag = false;
  bool mvDrag = false;
  mp::V3 mvGrabPoint{};
  std::vector<mp::V3> mvStart;
  bool overAny = false; // мышь над любой 2D-панелью (для призрака/пиков)
  bool overAnyPrev = false; // значение overAny прошлого кадра (для роутинга кликов)
  bool quit = false;
  double fps = 0;
  double now = 0;

  // ================= методы =================
  void startup(const char* openArg);
  void update(double dt);        // тики движка, импульсы, автосейв
  void drawUI();                 // 2D-слой: HUD/панели/диалоги

  bool undoPush(const char* label);
  void revalidate();
  void structuralChanged();      // resetDyn + revalidate + dirty

  void openFile(const std::string& path);
  void saveFile(const std::string& path);
  void newDoc();
  void applyBuild();
  void exportCode();
  void deleteSelection();
  void copySel();
  void pasteClip();
  void toggleDamage(const std::string& id, bool isSyn);
  void addRecent(const std::string& p);

  // 5.9 + 9.2: действия мыши, при записи попадают в стенд
  void fireOnceEvt(const std::string& id);
  void setFlowEvt(const std::string& id, double hz);
  void fillEvt(const std::string& clusterId, double frac);

  bool appStSelValid() const;
  void startRecording();
  void stopRecording();
  void fitView();

  void newClusterOnPlane();    // 3.x: DSL-кластер в точке курсора
  void newDocPrep();
};

const char* modeName(Mode m);

} // namespace mpui
