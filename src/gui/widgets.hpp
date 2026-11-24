#pragma once
// gui/widgets.hpp — immediate-mode UI тулкит (панели/кнопки/поля/слайдеры/меню),
// поверх draw.hpp. Семантика R1: у Opt-полей есть явная кнопка set/unset.
#include "draw.hpp"

#include "../mp/model.hpp"

#include <string>
#include <vector>

namespace mpui {

// ---------- тема (стиль FreeCAD dark) ----------
inline constexpr unsigned C_BG = 0x2b2d30ff;
inline constexpr unsigned C_PANEL = 0x2f3237f2;
inline constexpr unsigned C_PANELTITLE = 0x3a3d42ff;
inline constexpr unsigned C_BORDER = 0x52555aff;
inline constexpr unsigned C_TEXT = 0xe2e4e7ff;
inline constexpr unsigned C_DIM = 0x9aa0a6ff;
inline constexpr unsigned C_ACCENT = 0x4d7cc1ff;
inline constexpr unsigned C_ACCENTHI = 0x7799d9ff;
inline constexpr unsigned C_FIELD = 0x26282cff;
inline constexpr unsigned C_OK = 0x5aa06aff;
inline constexpr unsigned C_WARN = 0xd0a048ff;
inline constexpr unsigned C_ERR = 0xcc5550ff;
inline constexpr unsigned C_SEL = 0x3d5a80ff;

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  bool hit(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

unsigned hashStr(const char* s, unsigned seed = 0);

struct UI {
  int winW = 1, winH = 1;
  float mx = 0, my = 0;
  float mvx = 0, mvy = 0; // дельта мыши за кадр
  bool lDown = false, lPressed = false, lReleased = false, rPressed = false, rReleased = false;
  int wheel = 0;
  bool ctrl = false, shift = false, alt = false;

  std::string entered;             // текст этого кадра (SDL_TEXTINPUT)
  std::vector<std::string> keys;   // SDL_GetKeyName нажатий этого кадра
  bool keyOnce(const char* name) const;
  bool keyCopyPasteUndoRedo(int* what) const; // 1=cut 2=copy 3=paste 4=undo 5=redo
  void clearOneShot();

  // фокус текстового ввода
  int focus = 0;
  int prevFocus = 0;
  std::string buf;
  int cur = 0;
  bool enter = false;
  bool takeFocus(unsigned id, const std::string& initial);
  void dropFocus();

  // выдвижные слои (рисуются в endFrame поверх всего, экранируя клики)
  struct Drop { float x, y, w; int cur; std::vector<std::string> items; int* out; };
  bool dropOpen = false;
  Drop drop{};
  struct Menu { bool open = false; float x = 0, y = 0; std::vector<std::string> items; };
  bool menuOpen = false;
  Menu menu;
  int menuResult = -1;
  bool guardClicks = false; // клик уходит в выпадающий слой

  void beginFrame(int w, int h);
  void endFrame();
  void openMenu(float x, float y, std::vector<std::string> items);
  int menuChoice(); // -1 = нет выбора; потребляет результат
};

// --- виджеты ---
void widgetLabel(UI&, const std::string& txt, Rect r, unsigned col = C_TEXT, float px = 14);
bool widgetButton(UI&, unsigned id, const char* label, Rect r, bool enabled = true, unsigned col = C_ACCENT);
bool widgetButton(UI&, const char* label, Rect r, bool enabled = true, unsigned col = C_ACCENT);
bool widgetCheckbox(UI&, unsigned id, Rect r, bool& v, const char* label = nullptr);
bool widgetSlider(UI&, unsigned id, Rect r, double& v, double mn, double mx);
// текстовое поле: коммитит в val (Enter/уход фокуса); numeric — только число
bool widgetTextField(UI&, unsigned id, Rect r, std::string& val, bool numeric = false, float px = 14);
// Opt-поля (R1): [●/○] [значение] [−][+]
void widgetOptF(UI&, unsigned id, Rect r, mp::OptF& v, double step = 0.05, double mn = -1e9, double mx = 1e9);
void widgetOptI(UI&, unsigned id, Rect r, mp::OptI& v, long long step = 1, long long mn = -1e9, long long mx = 1e9);
// enum-поле с выпадающим списком
void widgetEnum(UI&, unsigned id, Rect r, int& idx, const std::vector<std::string>& names);
// секция инспектора (сворачиваемая)
bool widgetSection(UI&, unsigned id, const char* name, bool& open, Rect r);
float widgetRowLabel(UI&, const std::string& txt, Rect r);
bool widgetToggleRow(UI&, unsigned id, const std::string& txt, Rect r, bool& v);

// редактор кода (4.x): курсор offset, errLine подсвечивается; scroll в пикселях
bool widgetCodeEditor(UI&, unsigned id, Rect r, std::string& code, int& cursor, int errLine, float& scroll);
int editorLineFromCursor(const std::string& code, int cursor);
void editorCursorToLine(const std::string& code, int line, int& cursor);

// кликнутая строка списка (-1 = нет); scroll в пикселях
int widgetErrorList(UI&, Rect r, const std::string& title, const std::vector<std::string>& lines, float& scroll);

} // namespace mpui
