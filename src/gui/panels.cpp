// gui/panels.cpp — HUD, код-панель (4.x), файлы (7.x), стенды (9.x), диалоги,
// вся мышь сцены (5.1–5.10) и контекстные меню.
#include "panels.hpp"
#include "inspector.hpp"
#include "scene.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mpui {

// ---------- общие окна (используются и инспектором) ----------
struct WinState { float dx, dy; bool drag = false; };
static std::map<unsigned, WinState>& winStates() {
  static std::map<unsigned, WinState> m;
  return m;
}

bool uiWindowShared(UI& ui, unsigned id, const char* title, float& x, float& y, float w, float h, bool closeBtn) {
  WinState& st = winStates()[id];
  if (Rect{x, y, w - (closeBtn ? 26.0f : 0.0f), 24}.hit(ui.mx, ui.my) && ui.lPressed && !ui.guardClicks) {
    st.drag = true;
    st.dx = ui.mx - x;
    st.dy = ui.my - y;
    ui.lPressed = false;
  }
  if (st.drag && !ui.lDown) st.drag = false;
  if (st.drag) {
    x = ui.mx - st.dx;
    y = ui.my - st.dy;
  }
  x = std::max(0.0f, std::min(x, (float)ui.winW - 60));
  y = std::max(0.0f, std::min(y, (float)ui.winH - 30));
  quad(x, y, w, h, C_PANEL);
  frame(x, y, w, h, C_BORDER);
  quad(x, y, w, 24, C_PANELTITLE);
  widgetLabel(ui, title, {x + 10, y + 4, w, 0}, C_TEXT);
  bool closed = false;
  if (closeBtn) {
    Rect cb{x + w - 24, y + 3, 18, 18};
    if (widgetButton(ui, hashStr("x", id), "\xc3\x97", cb)) closed = true;
  }
  return closed;
}

// ---------- подписи перечислений ----------
static const std::vector<std::string> kNType = {"сенсор", "скрытый", "мотор", "модулятор", "гомеостат"};
static const std::vector<std::string> kSSign = {"возб.", "тормоз"};
static const std::vector<std::string> kPlast = {"жёсткая", "хебб", "награда", "анти-хебб"};

static float numRow2(UI& ui, unsigned id, const char* name, Rect r, double& v, double step = 0.05) {
  mp::OptF tmp;
  tmp.set = true;
  tmp.v = v;
  widgetLabel(ui, name, {r.x + 4, r.y + 2, 118, 0}, C_DIM);
  widgetOptF(ui, id, r, tmp, step, -1e9, 1e9);
  v = tmp.v;
  return 20;
}

static float textRow2(UI& ui, unsigned id, const char* name, Rect r, std::string& v) {
  widgetLabel(ui, name, {r.x + 4, r.y + 2, 118, 0}, C_DIM);
  widgetTextField(ui, id, {r.x + 124, r.y, r.w - 128, 20}, v, false);
  return 22;
}

static float boolRow2(UI& ui, unsigned id, const char* name, Rect r, bool& v) {
  widgetCheckbox(ui, id, {r.x + 4, r.y, r.w - 8, 20}, v, name);
  return 22;
}

static float enumRow2(UI& ui, unsigned id, const char* name, Rect r, int& idx, const std::vector<std::string>& names) {
  widgetLabel(ui, name, {r.x + 4, r.y + 2, 118, 0}, C_DIM);
  widgetEnum(ui, id, {r.x + 124, r.y, r.w - 128, 20}, idx, names);
  return 22;
}

// ================= КОД (4.x) =================
static void drawCodePanel(App& app, UI& ui) {
  float w = 560, h = 300;
  float x = 8, y = (float)ui.winH - h - 128;
  if (uiWindowShared(ui, hashStr("code"), "Код — операция НАД документом (4.1–4.6, R2)", x, y, w, h, false)) {}
  float bw = 74;
  if (widgetButton(ui, hashStr("tab code"), "Код", {x + 8, y + 26, bw, 20}, true, app.codeTabErrors ? C_BORDER : C_ACCENT))
    app.codeTabErrors = false;
  int nerr = (int)(app.lastBuild.errors.size() + app.lastBuild.conflicts.size());
  std::string errT = "Ошибки";
  if (nerr) errT += " (" + std::to_string(nerr) + ")";
  if (widgetButton(ui, hashStr("tab err"), errT.c_str(), {x + 8 + bw + 6, y + 26, bw + 26, 20}, true,
                   app.codeTabErrors ? C_ACCENT : C_BORDER))
    app.codeTabErrors = true;
  if (widgetButton(ui, hashStr("build"), "Собрать (4.2)", {x + w - bw - 82, y + 26, bw + 74, 20})) app.applyBuild();
  if (widgetButton(ui, hashStr("export"), "Экспорт (4.4)", {x + w - bw, y + 26, bw, 20})) app.exportCode();
  if (app.codeTabErrors) {
    std::vector<std::string> lines;
    std::vector<std::pair<int, std::string>> targets;
    for (const auto& e : app.lastBuild.errors) {
      lines.push_back("строка " + std::to_string(e.line) + ": " + e.msg);
      targets.push_back({e.line, ""});
    }
    for (const auto& c : app.lastBuild.conflicts) {
      lines.push_back("[конфликт] " + c.txt);
      targets.push_back({-1, c.unit});
    }
    if (lines.empty()) lines.push_back("ошибок нет — «Собрать» применит код поверх документа (R2)");
    int pick = widgetErrorList(ui, {x + 8, y + 52, w - 16, h - 66}, "", lines, app.errScroll);
    if (pick >= 0 && (size_t)pick < targets.size()) {
      app.codeTabErrors = false;
      if (targets[(size_t)pick].first > 0) {
        editorCursorToLine(app.codeText, targets[(size_t)pick].first, app.codeCursor);
        app.codeErrLine = targets[(size_t)pick].first;
      }
      if (!targets[(size_t)pick].second.empty()) {
        if (const mp::Neuron* n = app.doc.neuron(targets[(size_t)pick].second)) {
          app.cam.flyTo(n->pos);
          app.focusUnit = n->id;
        }
      }
    }
  } else {
    std::string code = app.codeText;
    int cur = app.codeCursor;
    if (widgetCodeEditor(ui, hashStr("codeed"), {x + 8, y + 52, w - 16, h - 66}, code, cur, app.codeErrLine,
                         app.codeScroll)) {
      app.codeText = code;
      app.codeCursor = cur;
      app.codeErrLine = -1;
    } else {
      app.codeText = code;
      app.codeCursor = cur;
    }
  }
  std::string st = "сборка: ";
  if (app.lastBuild.ok)
    st += "ok";
  else
    st += "ошибок: " + std::to_string(app.lastBuild.errors.size());
  st += ", +" + std::to_string(app.lastBuild.created) + " ~" + std::to_string(app.lastBuild.updated) + " -" +
        std::to_string(app.lastBuild.removed);
  if (!app.statusMsg.empty()) st += "  ·  " + app.statusMsg;
  widgetLabel(ui, st, {x + 10, y + h - 20, w, 0}, C_DIM, 13);
  if (x < ui.mx && ui.mx < x + w && y < ui.my && ui.my < y + h) app.overAny = true;
}

// ================= ФАЙЛЫ (7.x) =================
static void drawFilesPanel(App& app, UI& ui) {
  float w = 410, h = 400;
  float x = 90, y = 70;
  if (uiWindowShared(ui, hashStr("files"), "Файл проекта (7.1–7.5)", x, y, w, h, false)) {
    app.winFiles = false;
    return;
  }
  float yw = y + 30;
  widgetLabel(ui, "путь:", {x + 10, yw + 2, 44, 0}, C_DIM);
  widgetTextField(ui, hashStr("fpath"), {x + 54, yw, w - 150, 22}, app.filePathEdit, false);
  if (widgetButton(ui, hashStr("fopen"), "Открыть", {x + 10, yw + 28, 92, 24})) app.openFile(app.filePathEdit);
  if (widgetButton(ui, hashStr("fsave"), "Сохранить", {x + 108, yw + 28, 100, 24})) app.saveFile(app.filePathEdit);
  if (widgetButton(ui, hashStr("fnew"), "Новая схема", {x + 214, yw + 28, 110, 24}, true, C_WARN)) {
    app.confirmWhat = 3;
    app.confirmTxt = "Начать новую схему? (текущая будет потеряна)";
    app.confirmOpen = true;
  }
  yw += 60;
  std::string as = "автосохранение (7.2): ";
  as += app.lastAutosaveW < 0 ? std::string("ещё не было") : std::to_string((int)app.lastAutosaveW) + " c назад (каждые 30 c)";
  widgetLabel(ui, as, {x + 10, yw, w, 0}, C_DIM, 13);
  yw += 22;
  widgetLabel(ui, "параметры проекта (8.1/8.2):", {x + 10, yw, w, 0}, C_TEXT);
  yw += 20;
  yw += textRow2(ui, hashStr("pname"), "имя", {x + 10, yw, w - 20, 0}, app.doc.name);
  double tb = (double)app.doc.tickBudget;
  yw += numRow2(ui, hashStr("ptb"), "бюджет тиков (8.1)", {x + 10, yw, w - 20, 0}, tb, 1000);
  app.doc.tickBudget = (long long)tb;
  yw += numRow2(ui, hashStr("pen"), "норма возб (8.2)", {x + 10, yw, w - 20, 0}, app.doc.excNormLimit, 0.5);
  yw += numRow2(ui, hashStr("phz"), "тик Гц", {x + 10, yw, w - 20, 0}, app.doc.tickHz, 50);
  yw += numRow2(ui, hashStr("pgm"), "гамма", {x + 10, yw, w - 20, 0}, app.doc.gammaConst, 0.005);
  yw += numRow2(ui, hashStr("pur"), "покой u", {x + 10, yw, w - 20, 0}, app.doc.uRest, 0.05);
  yw += 10;
  widgetLabel(ui, "недавние (7.5):", {x + 10, yw, w, 0}, C_DIM, 13);
  yw += 18;
  pushClip(x + 1, yw, w - 2, y + h - yw - 4);
  for (size_t i = 0; i < app.recent.size() && i < 8; ++i) {
    Rect ir{x + 6, yw + i * 20, w - 12, 20};
    if (ir.hit(ui.mx, ui.my)) {
      quad(ir.x, ir.y, ir.w, ir.h, 0x23304288);
      if (ui.lPressed) {
        app.openFile(app.recent[i]);
        ui.lPressed = false;
      }
    }
    widgetLabel(ui, app.recent[i], {ir.x + 6, ir.y + 3, ir.w, 0}, C_TEXT, 13);
  }
  popClip();
  if (x < ui.mx && ui.mx < x + w && y < ui.my && ui.my < y + h) app.overAny = true;
}

// ================= СТЕНДЫ (9.x) =================
static void drawStandsPanel(App& app, UI& ui) {
  float w = 440, h = 400;
  float x = (float)ui.winW / 2 - w / 2, y = 80;
  if (uiWindowShared(ui, hashStr("stands"), "Стенды: запись / прогон / регрессия (9.1–9.4)", x, y, w, h, false)) {
    app.winStands = false;
    return;
  }
  float yw = y + 30;
  float bw = (w - 46) / 4;
  if (widgetButton(ui, hashStr("st new"), "Новый", {x + 10, yw, bw, 24})) {
    app.undoPush("стенд: создать (9.1)");
    std::string nm = "стенд" + std::to_string(app.doc.stands.size() + 1);
    mp::createStand(app.doc, nm, 5.0);
    app.stSel = (int)app.doc.stands.size() - 1;
    app.revalidate();
  }
  if (!app.recording) {
    if (widgetButton(ui, hashStr("st rec"), "Записать", {x + 10 + bw + 6, yw, bw, 24})) app.startRecording();
  } else {
    if (widgetButton(ui, hashStr("st rec"), "Стоп+фикс", {x + 10 + bw + 6, yw, bw, 24}, true, C_ERR)) app.stopRecording();
  }
  if (widgetButton(ui, hashStr("st run"), "Прогон", {x + 10 + (bw + 6) * 2, yw, bw, 24})) {
    if (app.stSel >= 0 && app.stSel < (int)app.doc.stands.size()) {
      app.lastReport.clear();
      app.lastReport.push_back(mp::runStand(app.doc, app.doc.stands[(size_t)app.stSel]));
    }
  }
  if (widgetButton(ui, hashStr("st reg"), "Регрессия", {x + 10 + (bw + 6) * 3, yw, bw, 24}))
    app.lastReport = mp::runRegression(app.doc);
  yw += 30;
  if (app.recording) {
    char tb[200];
    snprintf(tb, 200, "ЗАПИСЬ: %s — t=%.1f c (мышиные 5.9 пишутся в стенд)", app.recName.c_str(), app.recT);
    widgetLabel(ui, tb, {x + 10, yw, w, 0}, C_ERR);
    yw += 22;
  }
  pushClip(x + 1, yw, w - 2, 90);
  for (size_t i = 0; i < app.doc.stands.size(); ++i) {
    Rect ir{x + 8, yw + i * 20, w - 16, 20};
    bool sel = (int)i == app.stSel;
    if (ir.hit(ui.mx, ui.my)) {
      quad(ir.x, ir.y, ir.w, ir.h, 0x23304258);
      if (ui.lPressed) {
        app.stSel = (int)i;
        ui.lPressed = false;
      }
    } else if (sel) {
      quad(ir.x, ir.y, ir.w, ir.h, 0x2d6ea850);
    }
    widgetLabel(ui, app.doc.stands[i].name, {ir.x + 6, ir.y + 3, 130, 0}, C_TEXT);
    char b[96];
    snprintf(b, 96, "t=%.1fc v=%.1fx событий=%zu %s", app.doc.stands[i].durationSec, app.doc.stands[i].speed,
             app.doc.stands[i].events.size(), app.doc.stands[i].resultHash.empty() ? "(нет эталона)" : "");
    widgetLabel(ui, b, {ir.x + 136, ir.y + 3, ir.w - 140, 0}, C_DIM, 13);
  }
  popClip();
  yw += 96;
  if (app.stSel >= 0 && app.stSel < (int)app.doc.stands.size()) {
    mp::Stand& st = app.doc.stands[(size_t)app.stSel];
    if (textRow2(ui, hashStr("st nm"), "имя (9.1)", {x + 10, yw, w - 20, 0}, st.name)) app.revalidate();
    yw += 22;
    yw += numRow2(ui, hashStr("st du"), "длина сек", {x + 10, yw, w - 20, 0}, st.durationSec, 0.5);
    yw += numRow2(ui, hashStr("st sp"), "скорость (9.2)", {x + 10, yw, w - 20, 0}, st.speed, 0.1);
    yw += 6;
    bool same = st.schemaHash.empty() || st.schemaHash == mp::schemaHash(app.doc);
    widgetLabel(ui, same ? "хеш схемы: совпадает (9.3)" : "ХЕШ СХЕМЫ НЕ СОВПАЛ (9.3) — эталон устарел",
                {x + 10, yw, w, 0}, same ? C_OK : C_WARN, 13);
    yw += 18;
    if (widgetButton(ui, hashStr("st del"), "Удалить стенд", {x + 10, yw, 130, 22}, true, C_ERR)) {
      app.undoPush("стенд: удалить");
      mp::removeStand(app.doc, st.name);
      app.stSel = -1;
      app.revalidate();
    }
    yw += 30;
    for (const auto& rr : app.lastReport) {
      std::string line = rr.name + ": ";
      line += rr.replayOk ? (rr.regressionOk ? "OK" : "расхождение") : "ошибка прогона";
      if (rr.hashMismatch) line += " [хеш схемы изменился]";
      char b2[80];
      snprintf(b2, 80, " спайков %lld (%.1f/c)", rr.spikesTotal, rr.spikesPerSec);
      line += b2;
      widgetLabel(ui, line, {x + 10, yw, w, 0}, rr.replayOk && rr.regressionOk ? C_OK : C_WARN, 13);
      yw += 17;
    }
  }
  if (x < ui.mx && ui.mx < x + w && y < ui.my && ui.my < y + h) app.overAny = true;
}

// ================= HUD (6.6) =================
static void apiToggle(App& app) {
  if (app.apiOn) {
    if (app.api) app.api->stop();
    app.api.reset();
    app.apiOn = false;
    app.apiStatus = "выкл";
  } else {
    app.api = std::make_unique<mp::ApiServer>(app.doc, app.eng, app.apiPort);
    std::string err;
    if (app.api->start(err)) {
      app.apiOn = true;
      app.apiStatus = "127.0.0.1:" + std::to_string(app.apiPort);
    } else {
      app.apiStatus = "ошибка: " + err;
      app.api.reset();
    }
  }
}

static void drawHud(App& app, UI& ui) {
  float W = (float)ui.winW;
  quad(0, 0, W, 32, 0x141920f2);
  frame(0, 0, W, 32, C_BORDER);
  float x = 8;
  struct MB { const char* n; Mode m; };
  MB mbs[] = {{"Выбор", Mode::Select}, {"Нейрон", Mode::Neuron}, {"Связь", Mode::Wire},
              {"Резать", Mode::Cut}, {"Повредить", Mode::Damage}};
  for (auto& mb : mbs) {
    if (widgetButton(ui, hashStr(mb.n), mb.n, {x, 4, 80, 24}, true, app.mode == mb.m ? C_ACCENT : C_BORDER))
      app.mode = mb.m;
    x += 84;
  }
  if (widgetButton(ui, hashStr("run"), app.running ? "Стоп" : "Пуск", {x, 4, 64, 24}, true,
                   app.running ? C_ERR : C_OK))
    app.running = !app.running;
  x += 70;
  widgetLabel(ui, "скор:", {x, 9, 0, 0}, C_DIM);
  x += 38;
  widgetSlider(ui, hashStr("spd"), {x, 8, 80, 18}, app.speed, 0.0, 4.0);
  char sb[24];
  snprintf(sb, 24, "x%.1f", app.speed);
  widgetLabel(ui, sb, {x + 84, 9, 0, 0}, C_DIM);
  x += 112;
  auto st = app.eng.stats();
  char cb[176];
  snprintf(cb, 176, "н %lld · с %lld · такт %lld · спайки %lld (%.0f/с) · имп %lld · %.0f fps", st.neurons,
           st.synapses, app.eng.ticks(), st.spikes, st.spikesPerSec, st.impulses, app.fps);
  widgetLabel(ui, cb, {x, 9, 0, 0}, C_TEXT, 13);
  x += textWidth(cb, 13) + 12;
  if (widgetButton(ui, hashStr("ids"), "ID", {x, 4, 40, 24}, true, app.showIds ? C_ACCENT : C_BORDER))
    app.showIds = !app.showIds;
  x += 44;
  if (widgetButton(ui, hashStr("wmap"), "Веса", {x, 4, 56, 24}, true, app.weightMap ? C_ACCENT : C_BORDER))
    app.weightMap = !app.weightMap;
  x += 60;
  if (widgetButton(ui, hashStr("undo"), "Отменить", {x, 4, 80, 24}, app.undo.canUndo())) {
    std::string lb;
    app.doc = app.undo.undo(app.doc, &lb);
    app.eng.resetDyn();
    app.selN.clear();
    app.selS.clear();
    app.revalidate();
    app.statusMsg = "отменено: " + lb;
  }
  x += 84;
  if (widgetButton(ui, hashStr("redo"), "Вернуть", {x, 4, 70, 24}, app.undo.canRedo())) {
    std::string lb;
    app.doc = app.undo.redo(app.doc, &lb);
    app.eng.resetDyn();
    app.selN.clear();
    app.selS.clear();
    app.revalidate();
    app.statusMsg = "повторено: " + lb;
  }
  x += 80;
  if (widgetButton(ui, hashStr("files"), "Файл", {x, 4, 50, 24}, true, app.winFiles ? C_ACCENT : C_BORDER))
    app.winFiles = !app.winFiles;
  x += 56;
  if (widgetButton(ui, hashStr("stands"), "Стенды", {x, 4, 66, 24}, true, app.winStands ? C_ACCENT : C_BORDER))
    app.winStands = !app.winStands;
  x += 72;
  if (widgetButton(ui, hashStr("help"), "?", {x, 4, 28, 24}, true, app.winHelp ? C_ACCENT : C_BORDER))
    app.winHelp = !app.winHelp;
  x += 36;
  char ap[48];
  snprintf(ap, 48, "API:%s", app.apiStatus.c_str());
  if (widgetButton(ui, hashStr("apiB"), ap, {x, 4, 150, 24}, true, app.apiOn ? C_OK : C_BORDER)) apiToggle(app);
  x += 158;
  // флаги (8.x): точка статуса
  bool red = false, yl = false;
  for (const auto& f : app.flags) {
    if (mp::FlagLevel::Red == f.level) red = true;
    else yl = true;
  }
  unsigned dotc = red ? C_ERR : yl ? C_WARN : C_OK;
  quad(W - 220, 12, 10, 10, dotc);
  widgetLabel(ui, red ? "есть красные флаги" : yl ? "есть жёлтые" : "схема валидна", {W - 204, 9, 0, 0}, dotc, 13);

  const char* hint = "";
  switch (app.mode) {
    case Mode::Select: hint = "ЛКМ юнит — выбрать/зажечь · ЛКМ поле — рамка группы · ПКМ юнит — меню · СКМ — орбита · колесо — зум"; break;
    case Mode::Neuron: hint = "Нейрон: ЛКМ по полю открывает окно параметров ДО установки (5.1)"; break;
    case Mode::Wire: hint = app.wirePre ? "Связь: ЛКМ по приёмнику · ESC — отмена" : "Связь: ЛКМ по источнику (5.2)"; break;
    case Mode::Cut: hint = "Резать: ЛКМ по связи — подтверждение (5.3)"; break;
    case Mode::Damage: hint = "Повредить: ЛКМ по нейрону/связи — вкл/выкл (5.10)"; break;
  }
  float hy = (float)ui.winH - 26;
  quad(0, hy, W, 26, 0x141920f2);
  text(8, hy + 6, hint, C_DIM, 13);
  std::string st2;
  if (app.doc.dirty) st2 = "не сохранено";
  if (app.recording) st2 += (st2.empty() ? "" : " · ") + std::string("ЗАПИСЬ СТЕНДА");
  if (!st2.empty()) text(W - textWidth(st2, 13) - 10, hy + 6, st2, app.recording ? C_ERR : C_WARN, 13);
  if (ui.my < 32) app.overAny = true;
}

static void drawFlagsBar(App& app, UI& ui) {
  float W = (float)ui.winW;
  float h = 92;
  float y = (float)ui.winH - 26 - h;
  float x = W - 320;
  std::vector<std::string> lines;
  for (const auto& f : app.flags)
    lines.push_back(std::string(mp::FlagLevel::Red == f.level ? "[КРАСНЫЙ " : "[жёлтый ") + f.code + "] " + f.txt +
                    (f.unit.empty() ? "" : "  ->  " + f.unit));
  if (lines.empty()) lines.push_back("флагов валидатора нет — схема валидна");
  int pick = widgetErrorList(ui, {x, y, 312, h}, "Флаги схемы (8.x; клик — полёт к юниту)", lines, app.flagsScroll);
  if (pick >= 0 && (size_t)pick < app.flags.size()) {
    const mp::Flag& f = app.flags[(size_t)pick];
    if (!f.unit.empty()) {
      if (const mp::Neuron* n = app.doc.neuron(f.unit)) {
        app.cam.flyTo(n->pos);
        app.selN.clear();
        app.selS.clear();
        app.selN.insert(n->id);
        app.focusUnit = n->id;
      } else if (const mp::Synapse* s = app.doc.synapse(f.unit)) {
        app.selN.clear();
        app.selS.clear();
        app.selS.insert(s->id);
        app.focusUnit = s->pre;
      }
    }
  }
  if (ui.mx > x && ui.my >= y) app.overAny = true;
}

// ================= диалоги =================
static void drawPlaceDialog(App& app, UI& ui) {
  float w = 350, h = 400;
  float x = (float)ui.winW / 2 - w / 2, y = 90;
  if (uiWindowShared(ui, hashStr("place"), "Новый нейрон — параметры ДО установки (5.1)", x, y, w, h, false)) {
    app.place.open = false;
    return;
  }
  float yw = y + 30;
  mp::Neuron& n = app.place.n;
  std::vector<std::string> tn = {"сенсор", "скрытый", "мотор", "модулятор", "гомеостат"};
  int t = (int)(n.type.set ? n.type.v : 1);
  enumRow2(ui, hashStr("pc nt"), "тип (1.2)", {x + 10, yw, w - 20, 0}, t, tn);
  n.type = t;
  yw += 22;
  yw += textRow2(ui, hashStr("pc zn"), "зона (1.3)", {x + 10, yw, w - 20, 0}, n.zone);
  yw += textRow2(ui, hashStr("pc lb"), "метка (1.1)", {x + 10, yw, w - 20, 0}, n.label);
  std::vector<std::string> mdn = {"LIF", "ALIF"};
  int m = (int)(n.model.set ? n.model.v : 0);
  enumRow2(ui, hashStr("pc md"), "модель (1.5)", {x + 10, yw, w - 20, 0}, m, mdn);
  n.model = m;
  yw += 22;
  widgetLabel(ui, "порог (1.6)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptF(ui, hashStr("pc thr"), {x + 132, yw, w - 156, 20}, n.thr, 0.05);
  yw += 20;
  widgetLabel(ui, "утечка (1.7)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptF(ui, hashStr("pc lk"), {x + 132, yw, w - 156, 20}, n.leak, 0.01, 0, 1);
  yw += 20;
  widgetLabel(ui, "рефрак (1.8)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptI(ui, hashStr("pc rf"), {x + 132, yw, w - 156, 20}, n.refrMax, 1, 0, 1e6);
  yw += 20;
  widgetLabel(ui, "шум (1.9)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptF(ui, hashStr("pc ns"), {x + 132, yw, w - 156, 20}, n.noise, 0.01, 0, 1e3);
  yw += 22;
  std::string zhint = "зоны: ";
  for (const auto& z : app.doc.zones) zhint += z.code + " ";
  if (zhint.size() > 6) widgetLabel(ui, zhint, {x + 10, yw, w, 0}, C_DIM, 12);
  yw += 40;
  if (widgetButton(ui, hashStr("pc ok"), "Установить (5.1: OK ставит заполненную ячейку)", {x + 10, yw, w - 20, 28})) {
    app.undoPush("мышь: добавить нейрон (5.1)");
    mp::Neuron c = n;
    mp::NType tt = (mp::NType)t;
    std::string zc = c.zone.empty() ? std::string("u") : c.zone;
    c.id = app.doc.nextNeuronId(zc, tt, c.label);
    c.pos = app.place.pos;
    c.pos.y = std::max(0.0, c.pos.y);
    mp::Neuron* nn = app.doc.addNeuron(c);
    app.selN.clear();
    app.selN.insert(nn->id);
    app.structuralChanged();
    app.place.open = false;
  }
  if (widgetButton(ui, hashStr("pc no"), "Отмена", {x + 10, yw + 34, w - 20, 24}, true, C_BORDER))
    app.place.open = false;
}

static void drawSynDialog(App& app, UI& ui) {
  float w = 370, h = 430;
  float x = (float)ui.winW / 2 - w / 2, y = 90;
  if (uiWindowShared(ui, hashStr("synd"), app.synWin.editing ? "Связь — редактор (5.2/5.5)" : "Новая связь — параметры (5.2)", x, y, w, h, false)) {
    app.synWin.open = false;
    return;
  }
  float yw = y + 30;
  mp::Synapse& s = app.synWin.s;
  std::vector<std::string> ids;
  for (const auto& n : app.doc.neurons) ids.push_back(n.id);
  if (ids.empty()) ids.push_back("(нет нейронов)");
  int preI = 0;
  for (size_t i = 0; i < ids.size(); ++i)
    if (ids[i] == s.pre) preI = (int)i;
  int pe = preI;
  widgetLabel(ui, "пресинапс (2.1)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetEnum(ui, hashStr("sy pre"), {x + 132, yw, w - 156, 20}, pe, ids);
  if (pe != preI && !ids.empty() && ids[0][0] != '(') s.pre = ids[(size_t)pe];
  yw += 22;
  int postI = 0;
  for (size_t i = 0; i < ids.size(); ++i)
    if (ids[i] == s.post) postI = (int)i;
  int pe2 = postI;
  widgetLabel(ui, "постсинапс (2.1)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetEnum(ui, hashStr("sy post"), {x + 132, yw, w - 156, 20}, pe2, ids);
  if (pe2 != postI && !ids.empty() && ids[0][0] != '(') s.post = ids[(size_t)pe2];
  yw += 22;
  widgetLabel(ui, "вес (2.2)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptF(ui, hashStr("sy w"), {x + 132, yw, w - 156, 20}, s.weight, 0.05, 0, 1e6);
  yw += 20;
  int sg = (int)(s.sign.set ? s.sign.v : 0);
  enumRow2(ui, hashStr("sy sg"), "знак (2.3)", {x + 10, yw, w - 20, 0}, sg, kSSign);
  s.sign = sg;
  yw += 22;
  int pl = (int)(s.plast.set ? s.plast.v : 0);
  enumRow2(ui, hashStr("sy pl"), "пласт-ть (2.4)", {x + 10, yw, w - 20, 0}, pl, kPlast);
  s.plast = pl;
  yw += 22;
  widgetLabel(ui, "lr (2.5)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptF(ui, hashStr("sy lr"), {x + 132, yw, w - 156, 20}, s.lr, 0.005, 0, 1e6);
  yw += 20;
  widgetLabel(ui, "окно (2.6)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptI(ui, hashStr("sy pw"), {x + 132, yw, w - 156, 20}, s.pw, 1, 0, 1e6);
  yw += 20;
  widgetLabel(ui, "w мин (2.7)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptF(ui, hashStr("sy wmn"), {x + 132, yw, w - 156, 20}, s.wMin, 0.05);
  yw += 20;
  widgetLabel(ui, "w макс (2.7)", {x + 14, yw + 2, 118, 0}, C_DIM);
  widgetOptF(ui, hashStr("sy wmx"), {x + 132, yw, w - 156, 20}, s.wMax, 0.05);
  yw += 20;
  yw += boolRow2(ui, hashStr("sy en"), "включена (2.10)", {x + 10, yw, w - 20, 0}, s.enabled);
  if (widgetButton(ui, hashStr("sy ok"), app.synWin.editing ? "Применить" : "Создать связь",
                   {x + 10, yw + 26, w - 20, 28})) {
    if (app.synWin.editing) {
      app.undoPush("мышь: правка связи");
      if (mp::Synapse* ex = app.doc.synapse(app.synWin.editId)) *ex = s;
      app.revalidate();
      app.statusMsg = "связь изменена";
    } else {
      for (const auto& ex : app.doc.synapses)
        if (ex.pre == s.pre && ex.post == s.post) {
          app.synWin.editId = ex.id;
          app.synWin.editing = true;
          app.synWin.s = ex;
          app.selS.clear();
          app.selS.insert(ex.id);
          app.statusMsg = "связь между этой парой уже есть — открыта (5.2)";
          return;
        }
      app.undoPush("мышь: добавить связь (5.2)");
      s.id = app.doc.nextSynId();
      app.doc.synapses.push_back(s);
      app.selS.clear();
      app.selS.insert(s.id);
      app.structuralChanged();
      app.statusMsg = "связь создана";
    }
    app.synWin.open = false;
  }
  if (x < ui.mx && ui.mx < x + w && y < ui.my && ui.my < y + h) app.overAny = true;
}

// ---------- поток/заполнение/подтверждение/справка ----------
static void drawFlowDialog(App& app, UI& ui) {
  float w = 300, h = 130;
  float x = (float)ui.winW / 2 - w / 2, y = (float)ui.winH / 2 - 60;
  if (uiWindowShared(ui, hashStr("flow"), "Поток частоты (5.9)", x, y, w, h, false)) {
    app.eng.setFlow(app.flowId, 0);
    app.flowOpen = false;
    return;
  }
  char b[64];
  snprintf(b, 64, "%s  —  %.1f Гц", app.flowId.c_str(), app.flowHz);
  widgetLabel(ui, b, {x + 12, y + 32, w, 0}, C_TEXT);
  widgetSlider(ui, hashStr("flow sl"), {x + 12, y + 52, w - 24, 18}, app.flowHz, 0.0, 50.0);
  app.setFlowEvt(app.flowId, app.flowHz);
  if (widgetButton(ui, hashStr("flow stop"), "Стоп", {x + 12, y + 78, 80, 22})) {
    app.eng.setFlow(app.flowId, 0);
    app.flowHz = 0;
  }
  if (widgetButton(ui, hashStr("flow cl"), "Закрыть", {x + 100, y + 78, 90, 22}, true, C_BORDER)) {
    app.eng.setFlow(app.flowId, 0);
    app.flowOpen = false;
  }
}

static void drawFillDialog(App& app, UI& ui) {
  float w = 300, h = 140;
  float x = (float)ui.winW / 2 - w / 2, y = (float)ui.winH / 2 - 70;
  if (uiWindowShared(ui, hashStr("fill"), "Заполнение кластера (5.9)", x, y, w, h, false)) {
    app.fillOpen = false;
    return;
  }
  char b[64];
  snprintf(b, 64, "доля: %.0f%%", app.fillFrac * 100);
  widgetLabel(ui, b, {x + 12, y + 32, w, 0}, C_TEXT);
  widgetSlider(ui, hashStr("fill sl"), {x + 12, y + 52, w - 24, 18}, app.fillFrac, 0.0, 1.0);
  if (widgetButton(ui, hashStr("fill ok"), "Заполнить", {x + 12, y + 82, 110, 24})) {
    app.fillEvt(app.fillId, app.fillFrac);
    app.fillOpen = false;
  }
}

static void drawConfirmDialog(App& app, UI& ui) {
  float w = 380, h = 140;
  float x = (float)ui.winW / 2 - w / 2, y = (float)ui.winH / 2 - 70;
  if (uiWindowShared(ui, hashStr("cnf"), "Подтверждение", x, y, w, h, false)) {
    app.confirmOpen = false;
    return;
  }
  pushClip(x + 1, y + 26, w - 2, 60);
  text(x + 10, y + 34, app.confirmTxt, C_TEXT, 14);
  popClip();
  if (widgetButton(ui, hashStr("cnf y"), "Да", {x + 12, y + h - 38, 100, 26}, true, C_ERR)) {
    app.confirmOpen = false;
    if (app.confirmWhat == 3) {
      app.doc = mp::Project();
      app.newDocPrep();
    } else {
      app.undoPush("мышь: удаление");
      if (app.confirmWhat == 1) {
        app.doc.removeNeuron(app.confirmArg);
        app.selN.erase(app.confirmArg);
      } else if (app.confirmWhat == 2) {
        app.doc.removeSynapse(app.confirmArg);
        app.selS.erase(app.confirmArg);
      }
    }
    app.structuralChanged();
  }
  if (widgetButton(ui, hashStr("cnf n"), "Нет", {x + 126, y + h - 38, 100, 26}, true, C_BORDER))
    app.confirmOpen = false;
}

static void drawHelp(App& app, UI& ui) {
  const char* rows[] = {
      "Режимы: S выбор · N нейрон · C связь · X резать · D повреждение",
      "ЛКМ по юниту — выбор; ЛКМ по полю (реж. Выбор) — рамка группы (5.4)",
      "Удержание ЛКМ на нейроне (>0.4 c) — окно потока; клик без движения — спайк (5.9)",
      "ПКМ по нейрону: редакт/зажечь/поток/дубль/связь/повредить/удалить (5.5)",
      "ПКМ по связи: редакт (полное окно 2.x) / вкл-выкл / удалить; Alt+ЛКМ тоже редакт",
      "ПКМ по кластеру — заполнить долей; ПКМ по полю — меню сцены",
      "СКМ — орбита · Shift+СКМ — панорама · колесо — зум (5.8)",
      "Ctrl+C/V — копипаст группы с новыми ID (5.4) · Delete — удалить · Ctrl+Z/Y — undo/redo",
      "Код и мышь правят ОДИН документ; «Собрать» — код поверх схемы, ручные юниты не тронуты (R2)",
      "Флаги валидатора справа внизу; клик — полёт камеры к юниту (8.3)",
  };
  float w = 600, h = 40 + (float)10 * 22;
  float x = (float)ui.winW / 2 - w / 2, y = 120;
  if (uiWindowShared(ui, hashStr("help"), "Справка — мышь и клавиши", x, y, w, h, false)) {
    app.winHelp = false;
    return;
  }
  for (int i = 0; i < 10; ++i) widgetLabel(ui, rows[i], {x + 12, y + 34 + 22 * i, w, 0}, C_TEXT);
}

// ================= мышь сцены (5.x) =================
void sceneMouse(App& app) {
  UI& ui = app.ui;
  if (app.overAny || app.place.open || app.synWin.open || app.confirmOpen || app.fillOpen || app.flowOpen)
    return;

  Ray ray = app.cam.pickRay(ui.mx, ui.my, ui.winW, ui.winH);
  app.lastPos = app.cam.rayPlane(ray, 0.0f);

  // СКМ: орбита / панорама (5.8)
  bool mDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
  static bool midPrev = false;
  static float lastMx = 0, lastMy = 0;
  if (mDown && !midPrev) {
    lastMx = ui.mx;
    lastMy = ui.my;
  }
  if (mDown) {
    float dx = ui.mx - lastMx, dy = ui.my - lastMy;
    if (ui.shift)
      app.cam.pan(-dx, -dy);
    else
      app.cam.orbit(-dx, -dy);
    lastMx = ui.mx;
    lastMy = ui.my;
  }
  midPrev = mDown;

  // колесо — зум
  if (ui.wheel) {
    app.cam.zoom((float)ui.wheel);
    app.ui.wheel = 0;
  }

  // Alt+ЛКМ по связи — открыть редактор (5.5)
  if (ui.lPressed && ui.alt && !app.hoverS.empty()) {
    app.selS.clear();
    app.selS.insert(app.hoverS);
    if (mp::Synapse* s = app.doc.synapse(app.hoverS)) {
      app.synWin.open = true;
      app.synWin.editing = true;
      app.synWin.editId = app.hoverS;
      app.synWin.s = *s;
    }
    ui.lPressed = false;
    return;
  }

  if (ui.lPressed) {
    app.pressValid = true;
    app.pressX = ui.mx;
    app.pressY = ui.my;
    app.pressMoved = false;
    app.pressT = 0;
    app.pressN = app.hoverN;
    app.pressS = app.hoverS;
    app.pressPos = app.lastPos;
    app.rubber = false;
    app.rubberPending = app.mode == Mode::Select && app.hoverN.empty() && app.hoverS.empty();
    return;
  }

  // удержание на нейроне -> поток (5.9); перенос группы (5.4); рамка
  if (app.pressValid && !app.pressN.empty() && !app.pressMoved) {
    app.pressT += app.frameDt;
    if (app.pressT > 0.4) app.flowPending = true;
  }
  if (ui.lDown && app.pressValid && !app.pressMoved &&
      (std::fabs(ui.mx - app.pressX) > 4.0f || std::fabs(ui.my - app.pressY) > 4.0f))
    app.pressMoved = true;

  if (ui.lDown && app.pressValid && app.pressMoved && !app.rubber) {
    if (app.mode == Mode::Select && !app.pressN.empty()) {
      if (!app.movePushed) {
        app.undoPush("мышь: перенос группы");
        app.movePushed = true;
        // запомнить взаимные смещения группы
        app.dragRel.clear();
        const mp::Neuron* pn = app.doc.neuron(app.pressN);
        if (pn) {
          for (const auto& id : app.selN)
            if (id != app.pressN) {
              const mp::Neuron* q = app.doc.neuron(id);
              if (q) app.dragRel[id] = {q->pos.x - pn->pos.x, q->pos.y - pn->pos.y, q->pos.z - pn->pos.z};
            }
        }
      }
      if (const mp::Neuron* pn = app.doc.neuron(app.pressN)) {
        const_cast<mp::Neuron*>(pn)->pos = app.lastPos;
        for (const auto& kv : app.dragRel)
          if (mp::Neuron* q = app.doc.neuron(kv.first))
            q->pos = {app.lastPos.x + kv.second.x, app.lastPos.y + kv.second.y, app.lastPos.z + kv.second.z};
      }
      app.doc.dirty = true;
    } else if (app.mode == Mode::Select && app.pressN.empty() && app.pressS.empty() && app.rubberPending) {
      app.rubber = true;
      app.rubberPending = false;
    }
  }
  if (app.rubber && app.pressValid) {
    app.rubX0 = app.pressX;
    app.rubY0 = app.pressY;
    app.rubX1 = ui.mx;
    app.rubY1 = ui.my;
  }

  if (ui.lReleased && app.pressValid) {
    if (app.flowPending && !app.pressMoved && !app.pressN.empty()) {
      app.flowOpen = true;
      app.flowId = app.pressN;
      app.flowHz = 0;
    } else if (app.rubber && app.pressMoved) {
      float x0 = std::min(app.rubX0, app.rubX1), x1 = std::max(app.rubX0, app.rubX1);
      float y0 = std::min(app.rubY0, app.rubY1), y1 = std::max(app.rubY0, app.rubY1);
      for (const auto& n : app.doc.neurons) {
        bool ok = false;
        mp::V3 sp = app.cam.screenPoint(n.pos, ui.winW, ui.winH, &ok);
        if (ok && sp.x >= x0 && sp.x <= x1 && sp.y >= y0 && sp.y <= y1) app.selN.insert(n.id);
      }
      app.focusUnit = app.selN.empty() ? std::string() : *app.selN.begin();
    } else if (!app.pressMoved) {
      if (app.mode == Mode::Select) {
        if (!app.pressN.empty()) {
          app.selN.clear();
          app.selS.clear();
          app.selN.insert(app.pressN);
          app.focusUnit = app.pressN;
        } else if (!app.pressS.empty()) {
          app.selN.clear();
          app.selS.clear();
          app.selS.insert(app.pressS);
          app.focusUnit.clear();
        } else {
          app.selN.clear();
          app.selS.clear();
          app.selCluster.clear();
        }
      } else if (app.mode == Mode::Neuron) {
        if (!app.hoverN.empty()) {
          app.selN.clear();
          app.selS.clear();
          app.selN.insert(app.hoverN);
        } else {
          app.place.open = true;
          app.place.pos = app.lastPos;
          app.place.n = mp::Neuron();
          app.place.n.type = 1;
          app.place.n.model = 0;
          app.place.n.thr = 1.0;
          app.place.n.leak = 0.02;
        }
      } else if (app.mode == Mode::Wire) {
        if (!app.wirePre) {
          if (!app.hoverN.empty()) {
            app.wirePre = true;
            app.wireFrom = app.hoverN;
          } else if (!app.hoverS.empty()) {
            // клик по связи в реж. связи -> редактор существующей
            if (mp::Synapse* s = app.doc.synapse(app.hoverS)) {
              app.synWin.open = true;
              app.synWin.editing = true;
              app.synWin.editId = s->id;
              app.synWin.s = *s;
            }
          }
        } else if (!app.hoverN.empty() && app.hoverN != app.wireFrom) {
          app.synWin.open = true;
          app.synWin.editing = false;
          app.synWin.editId.clear();
          app.synWin.s = mp::Synapse();
          app.synWin.s.pre = app.wireFrom;
          app.synWin.s.post = app.hoverN;
          app.synWin.s.weight = 0.5;
          app.synWin.s.sign = 0;
          app.wirePre = false;
        }
      } else if (app.mode == Mode::Cut) {
        if (!app.hoverS.empty()) {
          app.confirmWhat = 2;
          app.confirmArg = app.hoverS;
          app.confirmTxt = "Перерезать связь " + app.hoverS + "? (5.3)";
          app.confirmOpen = true;
        }
      } else if (app.mode == Mode::Damage) {
        if (!app.hoverN.empty()) app.toggleDamage(app.hoverN, false);
        else if (!app.hoverS.empty()) app.toggleDamage(app.hoverS, true);
      }
    }
    app.rubber = false;
    app.rubberPending = false;
    app.pressValid = false;
    app.movePushed = false;
    app.flowPending = false;
    app.dragRel.clear();
  }

  // --- ПКМ: контекстные меню (5.5) ---
  if (ui.rPressed && !app.hoverN.empty()) {
    std::vector<std::string> items;
    bool dis = app.doc.neuron(app.hoverN)->disabled;
    items.push_back("Редактировать");
    items.push_back("Зажечь");
    items.push_back("Поток…");
    items.push_back("Дублировать");
    items.push_back("Добавить связь");
    items.push_back(dis ? "Починить" : "Повредить");
    items.push_back("Удалить");
    app.ui.openMenu(ui.mx, ui.my, items);
    app.menuCtx = 1;
    app.menuArg = app.hoverN;
  } else if (ui.rPressed && !app.hoverS.empty()) {
    app.ui.openMenu(ui.mx, ui.my, {"Редактировать связь", "Включить/Отключить", "Удалить связь"});
    app.menuCtx = 2;
    app.menuArg = app.hoverS;
  } else if (ui.rPressed) {
    std::string cid;
    for (const auto& c : app.doc.clusters) {
      float r = (float)(c.radius.set ? c.radius.v : 8.0);
      float dx = (float)(app.lastPos.x - c.center.x), dy = (float)(app.lastPos.y - c.center.y),
            dz = (float)(app.lastPos.z - c.center.z);
      if (dx * dx + dy * dy + dz * dz <= r * r) cid = c.id;
    }
    if (!cid.empty()) {
      app.ui.openMenu(ui.mx, ui.my, {"Заполнить (5.9)", "Редактировать кластер", "Удалить кластер"});
      app.menuCtx = 3;
      app.menuArg = cid;
    } else {
      app.ui.openMenu(ui.mx, ui.my, {"Нейрон здесь (5.1)", "Новый кластер", "Собрать код (4.2)", "Экспорт кода",
                                     "Новый проект", "Открыть файл…", "Сохранить", "Вид: сверху", "Вид: сброс"});
      app.menuCtx = 0;
    }
  }
}

// выбор из контекстных меню
void handleMenusStatic(App& app) {
  UI& ui = app.ui;
  int m = ui.menuChoice();
  if (m < 0) return;
  int ctx = app.menuCtx;
  std::string arg = app.menuArg;
  if (ctx == 1) {
    switch (m) {
      case 0:
        app.selN.clear();
        app.selS.clear();
        app.selN.insert(arg);
        app.focusUnit = arg;
        break;
      case 1:
        app.fireOnceEvt(arg);
        app.flash[arg] = 1.0;
        break;
      case 2:
        app.flowOpen = true;
        app.flowId = arg;
        app.flowHz = 0;
        break;
      case 3:
        app.undoPush("мышь: дублировать");
        if (mp::Neuron* n = app.doc.neuron(arg)) {
          mp::Neuron c = *n;
          c.id.clear();
          c.pos.x += 3;
          c.pos.z += 3;
          c.byCode = false;
          c.srcCluster.clear();
          c.u = 0;
          c.refr = 0;
          c.spikeCount = 0;
          c.uThrAdapt = 0;
          c.homeoAdj = 0;
          mp::Neuron* nn = app.doc.addNeuron(c);
          app.selN.clear();
          app.selN.insert(nn->id);
          app.structuralChanged();
        }
        break;
      case 4:
        app.mode = Mode::Wire;
        app.wirePre = true;
        app.wireFrom = arg;
        break;
      case 5:
        app.toggleDamage(arg, false);
        break;
      case 6:
        app.confirmWhat = 1;
        app.confirmArg = arg;
        app.confirmTxt = "Удалить " + arg + "? (R4)";
        app.confirmOpen = true;
        break;
    }
  } else if (ctx == 2) {
    if (m == 0) {
      app.selS.clear();
      app.selN.clear();
      app.selS.insert(arg);
      if (mp::Synapse* s = app.doc.synapse(arg)) {
        app.synWin.open = true;
        app.synWin.editing = true;
        app.synWin.editId = arg;
        app.synWin.s = *s;
      }
    } else if (m == 1) {
      app.toggleDamage(arg, true);
    } else if (m == 2) {
      app.confirmWhat = 2;
      app.confirmArg = arg;
      app.confirmTxt = "Перерезать связь " + arg + "?";
      app.confirmOpen = true;
    }
  } else if (ctx == 3) {
    if (m == 0) {
      app.fillOpen = true;
      app.fillId = arg;
      app.fillFrac = 0.5;
    } else if (m == 1) {
      app.selCluster = arg;
      app.selN.clear();
      app.selS.clear();
    } else if (m == 2) {
      app.undoPush("мышь: удалить кластер");
      app.doc.removeClusterNeurons(arg);
      for (size_t i = 0; i < app.doc.clusters.size(); ++i)
        if (app.doc.clusters[i].id == arg) {
          app.doc.clusters.erase(app.doc.clusters.begin() + (long)i);
          break;
        }
      app.structuralChanged();
    }
  } else if (ctx == 0) {
    switch (m) {
      case 0: app.mode = Mode::Neuron; break;
      case 1: app.newClusterOnPlane(); break;
      case 2: app.applyBuild(); break;
      case 3: app.exportCode(); break;
      case 4:
        app.confirmWhat = 3;
        app.confirmTxt = "Начать новую схему? (текущая будет потеряна)";
        app.confirmOpen = true;
        break;
      case 5: app.winFiles = true; break;
      case 6: app.saveFile(app.filePath.empty() ? "autosave.mnp.json" : app.filePath); break;
      case 7: app.cam.pitch = 1.52f; break;
      case 8:
        app.cam.pitch = 0.5f;
        app.cam.dist = 40;
        app.cam.target = {0, 1, 0};
        break;
    }
  }
}

// ================= главный вызов 2D-слоя =================
void panelsDraw(App& app) {
  UI& ui = app.ui;
  app.overAny = false;
  sceneMouse(app);
  handleMenusStatic(app);
  scenePick(app);

  drawHud(app, ui);
  drawFlagsBar(app, ui);
  inspectorDraw(app);
  if (app.winCode) drawCodePanel(app, ui);
  if (app.winFiles) drawFilesPanel(app, ui);
  if (app.winStands) drawStandsPanel(app, ui);
  if (app.winHelp) drawHelp(app, ui);
  if (app.place.open) drawPlaceDialog(app, ui);
  if (app.synWin.open) drawSynDialog(app, ui);
  if (app.flowOpen) drawFlowDialog(app, ui);
  if (app.fillOpen) drawFillDialog(app, ui);
  if (app.confirmOpen) drawConfirmDialog(app, ui);
  // клавиатура вне панелей
  if (!app.overAny) {
    std::vector<std::string> keys = ui.keys;
    for (const auto& k : keys) {
      if (k == "S" && !ui.ctrl) app.mode = Mode::Select;
      else if (k == "N" && !ui.ctrl) app.mode = Mode::Neuron;
      else if (k == "C" && !ui.ctrl) app.mode = Mode::Wire;
      else if (k == "X" && !ui.ctrl) app.mode = Mode::Cut;
      else if (k == "D" && !ui.ctrl) app.mode = Mode::Damage;
      else if (k == "Escape") {
        app.wirePre = false;
        app.selN.clear();
        app.selS.clear();
        app.selCluster.clear();
      }
    }
    int cp = 0;
    if (ui.keyCopyPasteUndoRedo(&cp)) {
      if (cp == 2) app.copySel();
      else if (cp == 3) app.pasteClip();
      else if (cp == 1) app.copySel();
      else if (cp == 4) {
        if (app.undo.canUndo()) {
          std::string lb;
          app.doc = app.undo.undo(app.doc, &lb);
          app.eng.resetDyn();
          app.selN.clear();
          app.selS.clear();
          app.revalidate();
          app.statusMsg = "отменено: " + lb;
        }
      } else if (cp == 5) {
        if (app.undo.canRedo()) {
          std::string lb;
          app.doc = app.undo.redo(app.doc, &lb);
          app.eng.resetDyn();
          app.selN.clear();
          app.selS.clear();
          app.revalidate();
          app.statusMsg = "повторено: " + lb;
        }
      }
    }
    if (ui.keyOnce("Delete") || ui.keyOnce("Backspace")) app.deleteSelection();
  }
}

} // namespace mpui
