#include "widgets.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mpui {

unsigned hashStr(const char* s, unsigned seed) {
  unsigned h = seed ? seed : 0x9e3779b9u;
  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 0x01000193u;
  }
  return h;
}

bool UI::keyOnce(const char* name) const {
  for (const auto& k : keys)
    if (k == name) return true;
  return false;
}

bool UI::keyCopyPasteUndoRedo(int* what) const {
  if (!ctrl) return false;
  for (const auto& k : keys) {
    if (k == "X" || k == "x") { *what = 1; return true; }
    if (k == "C" || k == "c") { *what = 2; return true; }
    if (k == "V" || k == "v") { *what = 3; return true; }
    if (k == "Z" || k == "z") { *what = shift ? 5 : 4; return true; }
    if (k == "Y" || k == "y") { *what = 5; return true; }
  }
  return false;
}

bool UI::takeFocus(unsigned id, const std::string& initial) {
  prevFocus = focus;
  focus = (int)id;
  buf = initial;
  cur = (int)buf.size();
  return true;
}

void UI::dropFocus() {
  prevFocus = focus;
  focus = 0;
}

void UI::beginFrame(int w, int h) {
  winW = w;
  winH = h;
  guardClicks = dropOpen || menuOpen;
}

void UI::clearOneShot() {
  keys.clear();
  entered.clear();
  wheel = 0;
  lPressed = false;
  lReleased = false;
  rPressed = false;
  rReleased = false;
}

void UI::endFrame() {
  const float ih = 20;
  int n = (int)drop.items.size();
  float h = n * ih + 4;
  float wmax = drop.w;
  for (const auto& it : drop.items) wmax = std::max(wmax, textWidth(it, 14) + 22);
  // --- выпадающий список ---
  if (dropOpen) {
    float dy = std::min(drop.y, (float)winH - h - 2);
    quad(drop.x, dy, wmax, h, 0x0d1016ff);
    frame(drop.x, dy, wmax, h, C_ACCENT);
    bool closed = false;
    for (int i = 0; i < n; ++i) {
      Rect ir{drop.x + 2, dy + 2 + i * ih, wmax - 4, ih};
      if (ir.hit(mx, my)) {
        quad(ir.x, ir.y, ir.w, ir.h, i == drop.cur ? C_SEL : 0x233042ff);
        if (lPressed || lReleased) {
          dropPickId = drop.wid;
          dropPickVal = i;
          closed = true;
          lPressed = false;
          lReleased = false;
          lDown = false;
        }
      }
      widgetLabel(*this, drop.items[i], {ir.x + 8, ir.y + 3, ir.w, ih}, i == drop.cur ? C_ACCENTHI : C_TEXT);
    }
    if (closed) {
      dropOpen = false;
      lReleased = false;
      lDown = false;
    } else if (lPressed && !Rect{drop.x, dy, wmax, h}.hit(mx, my)) {
      dropOpen = false;
      lPressed = false;
      lDown = false;
    }
  }
  // --- контекстное меню ---
  if (menuOpen) {
    float wmenu = 70;
    for (const auto& it : menu.items) wmenu = std::max(wmenu, textWidth(it, 14) + 26);
    float mh = menu.items.size() * ih + 4;
    float x = std::max(2.0f, std::min(menu.x, (float)winW - wmenu - 2));
    float y = std::max(2.0f, std::min(menu.y, (float)winH - mh - 2));
    menuRect = {x, y, wmenu, mh};
    quad(x, y, wmenu, mh, 0x10151cff);
    frame(x, y, wmenu, mh, C_ACCENT);
    int pick = -1;
    for (size_t i = 0; i < menu.items.size(); ++i) {
      Rect ir{x + 2, y + 2 + i * ih, wmenu - 4, ih};
      if (ir.hit(mx, my)) {
        quad(ir.x, ir.y, ir.w, ir.h, 0x233042ff);
        if (lPressed || lReleased) {
          pick = (int)i;
          lPressed = false;
          lReleased = false;
          lDown = false;
        }
      }
      widgetLabel(*this, menu.items[i], {ir.x + 10, ir.y + 3.5f, ir.w, ih}, C_TEXT);
    }
    if (keyOnce("Escape")) {
      menuOpen = false;
      lPressed = false;
      lDown = false;
      lReleased = false;
    } else if (pick >= 0) {
      menuResult = pick;
      lPressed = false;
      lReleased = false;
      lDown = false;
    }
  }
  clearOneShot();
  ctrl = shift = alt = false;
}

void UI::openMenu(float x, float y, std::vector<std::string> items) {
  menu = {true, x, y, std::move(items)};
  menuResult = -1;
}

int UI::menuChoice() {
  int r = menuResult;
  menuResult = -1;
  return r;
}

// ---------- базовые ----------
void widgetLabel(UI&, const std::string& txt, Rect r, unsigned col, float px) {
  if (r.w > 0) {
    float ch = r.h > 0 ? r.h : fontHeight(px) + 4.0f;
    pushClip(r.x, r.y, r.w, ch);
    text(r.x, r.y, txt, col, px);
    popClip();
  } else {
    text(r.x, r.y, txt, col, px);
  }
}

static bool over(const Rect& r, const UI& ui) { return r.hit(ui.mx, ui.my); }

bool widgetButton(UI& ui, unsigned id, const char* label, Rect r, bool enabled, unsigned col) {
  (void)id;
  if (!enabled) {
    quad(r.x, r.y, r.w, r.h, 0x171c23ff);
    frame(r.x, r.y, r.w, r.h, 0x222a33ff);
    widgetLabel(ui, label, {r.x + r.w / 2 - textWidth(label, 14) / 2, r.y + (r.h - 17) / 2 + 1, r.w, 0}, 0x556070ff);
    return false;
  }
  bool hov = over(r, ui) && !ui.guardClicks;
  unsigned border = hov ? C_ACCENTHI : col;
  unsigned fill = hov ? 0x20303fff : 0x1c2530ff;
  bool act = false;
  if (hov && ui.lPressed) {
    fill = 0x0d1016ff;
    act = true;
    ui.lPressed = false;
  }
  quad(r.x, r.y, r.w, r.h, fill);
  frame(r.x, r.y, r.w, r.h, border);
  widgetLabel(ui, label, {r.x + r.w / 2 - textWidth(label, 14) / 2, r.y + (r.h - 17) / 2 + 1, r.w, 0}, act ? C_ACCENTHI : C_TEXT);
  return act;
}

bool widgetButton(UI& ui, const char* label, Rect r, bool enabled, unsigned col) {
  return widgetButton(ui, hashStr(label), label, r, enabled, col);
}

bool widgetCheckbox(UI& ui, unsigned id, Rect r, bool& v, const char* label) {
  Rect box{r.x, r.y + (r.h - 16) / 2, 16, 16};
  bool act = false;
  Rect full{r.x, r.y, 16, r.h};
  if (over(full, ui) && !ui.guardClicks && ui.lPressed) {
    act = true;
    v = !v;
    ui.lPressed = false;
  }
  quad(box.x, box.y, 16, 16, C_FIELD);
  frame(box.x, box.y, 16, 16, act ? C_ACCENTHI : (v ? C_ACCENT : C_BORDER));
  if (v) quad(box.x + 4, box.y + 4, 8, 8, C_ACCENTHI);
  if (label) widgetLabel(ui, label, {r.x + 22, r.y + (r.h - 17) / 2 + 1, r.w, 0}, C_TEXT);
  (void)id;
  return act;
}

bool widgetSlider(UI& ui, unsigned id, Rect r, double& v, double mn, double mx) {
  (void)id;
  quad(r.x, r.y + r.h / 2 - 2, r.w, 4, 0x232a33ff);
  float f = (float)((v - mn) / (mx - mn));
  f = std::max(0.0f, std::min(1.0f, f));
  quad(r.x, r.y + r.h / 2 - 2, r.w * f, 4, C_ACCENT);
  bool changed = false;
  Rect full{r.x - 4, r.y - 8, r.w + 8, r.h + 16};
  if (over(full, ui) && !ui.guardClicks && (ui.lDown || ui.lPressed)) {
    float g = (ui.mx - r.x) / std::max(1.0f, r.w);
    g = std::max(0.0f, std::min(1.0f, g));
    double nv = mn + (mx - mn) * g;
    if (nv != v) {
      v = nv;
      changed = true;
    }
  }
  float kx = r.x + r.w * f - 4;
  quad(kx, r.y + r.h / 2 - 6, 8, 12, over(full, ui) && ui.lDown ? C_ACCENTHI : 0x7f8ea0ff);
  return changed;
}

bool widgetTextField(UI& ui, unsigned id, Rect r, std::string& val, bool numeric, float px) {
  bool focused = ui.focus == (int)id;
  if (over(r, ui) && !ui.guardClicks && ui.lPressed) {
    ui.takeFocus(id, val);
    focused = true;
    ui.lPressed = false;
  }
  quad(r.x, r.y, r.w, r.h, focused ? 0x0c1016ff : C_FIELD);
  frame(r.x, r.y, r.w, r.h, focused ? C_ACCENT : C_BORDER);
  const std::string& shown = focused ? ui.buf : val;
  widgetLabel(ui, shown, {r.x + 6, r.y + (r.h - 16) / 2, r.w - 8, 0}, focused ? C_TEXT : (shown.empty() ? 0x556070ff : C_TEXT), px);
  if (!focused) return false;
  bool commit = false;
  if (ui.keyOnce("Return") || ui.keyOnce("Enter") || ui.keyOnce("Keypad Enter")) commit = true;
  for (const auto& k : ui.keys) {
    if (k == "Backspace" && ui.cur > 0) {
      int nb = 1;
      while (ui.cur - nb > 0 && (ui.buf[(size_t)(ui.cur - nb)] & 0xC0) == 0x80) ++nb;
      ui.buf.erase((size_t)(ui.cur - nb), (size_t)nb);
      ui.cur -= nb;
    } else if (k == "Delete" && ui.cur < (int)ui.buf.size()) {
      int nb = 1;
      while (ui.cur + nb < (int)ui.buf.size() && (ui.buf[(size_t)(ui.cur + nb)] & 0xC0) == 0x80) ++nb;
      ui.buf.erase((size_t)ui.cur, (size_t)nb);
    } else if (k == "Left") {
      while (ui.cur > 0 && (ui.buf[(size_t)(ui.cur - 1)] & 0xC0) == 0x80) --ui.cur;
      if (ui.cur > 0) --ui.cur;
    } else if (k == "Right") {
      if (ui.cur < (int)ui.buf.size()) ++ui.cur;
      while (ui.cur < (int)ui.buf.size() && (ui.buf[(size_t)ui.cur] & 0xC0) == 0x80) ++ui.cur;
    } else if (k == "Home") {
      ui.cur = 0;
    } else if (k == "End") {
      ui.cur = (int)ui.buf.size();
    } else if (k == "Escape") {
      ui.prevFocus = ui.focus;
      ui.focus = 0;
      return false;
    }
  }
  for (char c : ui.entered) {
    if (c == '\r' || c == '\n') {
      commit = true;
      continue;
    }
    if (numeric && !(c == '-' || c == '+' || c == '.' || c == ',' || c == 'e' || c == 'E' ||
                     (c >= '0' && c <= '9')))
      continue;
    if (c == ',') c = '.';
    if ((unsigned char)c < 0x20) continue;
    ui.buf.insert((size_t)ui.cur, 1, c);
    ++ui.cur;
  }
  std::string before = ui.buf.substr(0, (size_t)std::min(ui.cur, (int)ui.buf.size()));
  float cx = r.x + 6 + textWidth(before, px);
  if ((SDL_GetTicks() / 400) % 2 == 0) quad(cx, r.y + 3, 1, r.h - 6, C_ACCENTHI);
  if (commit) {
    ui.prevFocus = ui.focus;
    ui.focus = 0;
    if (numeric) {
      double dv = strtod(ui.buf.c_str(), nullptr);
      char tmp[32];
      snprintf(tmp, sizeof(tmp), "%g", dv);
      if (val != tmp) {
        val = tmp;
        return true;
      }
      return false;
    }
    if (val != ui.buf) {
      val = ui.buf;
      return true;
    }
  }
  return false;
}

// ---------- Opt-поля ----------
static void optRowToggle(UI& ui, unsigned id, Rect r, bool& set) {
  (void)id;
  if (over(r, ui) && !ui.guardClicks && ui.lPressed) {
    set = !set;
    ui.lPressed = false;
  }
  quad(r.x + 3, r.y + (r.h - 10) / 2, 10, 10, set ? C_ACCENT : C_FIELD);
  frame(r.x + 3, r.y + (r.h - 10) / 2, 10, 10, set ? C_ACCENTHI : C_BORDER);
  if (set) quad(r.x + 6, r.y + (r.h - 10) / 2 + 3, 4, 4, C_ACCENTHI);
}

static void optSteppers(UI& ui, Rect r, float fieldRight, bool& set, double& v, double step, double mn, double mx) {
  Rect bm{fieldRight + 2, r.y, 9, r.h};
  Rect bp{bm.x + bm.w + 2, r.y, 9, r.h};
  for (int i = 0; i < 2; ++i) {
    Rect b = i == 0 ? bm : bp;
    const char* lab = i == 0 ? "-" : "+";
    bool hov = over(b, ui) && !ui.guardClicks;
    quad(b.x, b.y, b.w, b.h, hov ? 0x2a3542ff : 0x20272fff);
    widgetLabel(ui, lab, {b.x + 2.5f, b.y + (r.h - 15) / 2, 0, 0}, hov ? C_ACCENTHI : C_DIM);
    if (hov && ui.lPressed) {
      v = std::max(mn, std::min(mx, (i == 0 ? v - step : v + step)));
      set = true;
      ui.lPressed = false;
    }
  }
}

void widgetOptF(UI& ui, unsigned id, Rect r, mp::OptF& v, double step, double mn, double mx) {
  bool set = v.set;
  optRowToggle(ui, hashStr("tog", id), {r.x, r.y, 16, r.h}, set);
  v.set = set;
  double val = v.v;
  Rect frect{r.x + 18, r.y, r.w - 42, r.h};
  std::string edit;
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%g", val);
  edit = tmp;
  if (!set) edit.clear();
  if (widgetTextField(ui, id, frect, edit, true)) {
    val = strtod(edit.c_str(), nullptr);
    val = std::max(mn, std::min(mx, val));
    set = true;
  }
  if (!v.set && ui.focus == (int)id && !edit.empty()) set = true;
  optSteppers(ui, r, frect.x + frect.w, set, val, step, mn, mx);
  v.v = val;
  v.set = set;
}

void widgetOptI(UI& ui, unsigned id, Rect r, mp::OptI& v, long long step, long long mn, long long mx) {
  bool set = v.set;
  optRowToggle(ui, hashStr("tog", id), {r.x, r.y, 16, r.h}, set);
  v.set = set;
  long long val = v.v;
  Rect frect{r.x + 18, r.y, r.w - 42, r.h};
  std::string edit = set ? std::to_string(val) : std::string();
  if (widgetTextField(ui, id, frect, edit, true)) {
    val = strtoll(edit.c_str(), nullptr, 10);
    val = std::max(mn, std::min(mx, val));
    set = true;
  }
  if (!v.set && ui.focus == (int)id && !edit.empty()) set = true;
  Rect bm{frect.x + frect.w + 2, r.y, 9, r.h};
  Rect bp{bm.x + bm.w + 2, r.y, 9, r.h};
  for (int i = 0; i < 2; ++i) {
    Rect b = i == 0 ? bm : bp;
    const char* lab = i == 0 ? "-" : "+";
    bool hov = over(b, ui) && !ui.guardClicks;
    quad(b.x, b.y, b.w, b.h, hov ? 0x2a3542ff : 0x20272fff);
    widgetLabel(ui, lab, {b.x + 2.5f, b.y + (r.h - 15) / 2, 0, 0}, hov ? C_ACCENTHI : C_DIM);
    if (hov && ui.lPressed) {
      val = std::max(mn, std::min(mx, (i == 0 ? val - step : val + step)));
      set = true;
      ui.lPressed = false;
    }
  }
  v.v = val;
  v.set = set;
}

// ---------- enum ----------
void widgetEnum(UI& ui, unsigned id, Rect r, int& idx, const std::vector<std::string>& names) {
  if (ui.dropPickId == id) {
    idx = ui.dropPickVal;
    ui.dropPickId = 0;
  }
  if (idx < 0 || idx >= (int)names.size()) idx = 0;
  bool hov = over(r, ui) && !ui.guardClicks;
  quad(r.x, r.y, r.w, r.h, hov ? 0x1d2630ff : C_FIELD);
  frame(r.x, r.y, r.w, r.h, hov ? C_ACCENT : C_BORDER);
  widgetLabel(ui, names[(size_t)idx], {r.x + 6, r.y + (r.h - 16) / 2, r.w - 14, 0}, C_TEXT);
  widgetLabel(ui, "\xe2\x96\xbc", {r.x + r.w - 15, r.y + (r.h - 15) / 2, 0, 0}, C_DIM);
  if (hov && ui.lPressed) {
    ui.drop = {r.x, r.y + r.h, r.w, idx, names, id};
    ui.dropOpen = true;
    ui.lPressed = false;
  }
}

// ---------- секция ----------
bool widgetSection(UI& ui, unsigned id, const char* name, bool& open, Rect r) {
  bool hov = over(r, ui) && !ui.guardClicks;
  quad(r.x, r.y, r.w, 22, hov ? 0x232d38ff : C_PANELTITLE);
  widgetLabel(ui, open ? "\xe2\x96\xbe " : "\xe2\x96\xb8 ", {r.x + 6, r.y + 4, 0, 0}, C_ACCENT);
  widgetLabel(ui, name, {r.x + 24, r.y + 4, r.w, 0}, C_TEXT);
  if (hov && ui.lPressed) {
    open = !open;
    ui.lPressed = false;
  }
  (void)id;
  return open;
}

float widgetRowLabel(UI& ui, const std::string& txt, Rect r) {
  widgetLabel(ui, txt, {r.x + 4, r.y + 2, r.w - 8, 0}, C_DIM);
  return 18;
}

bool widgetToggleRow(UI& ui, unsigned id, const std::string& txt, Rect r, bool& v) {
  widgetCheckbox(ui, id, {r.x, r.y, r.w, 18}, v, txt.c_str());
  return false;
}

// ---------- редактор кода ----------
int editorLineFromCursor(const std::string& code, int cursor) {
  int line = 1;
  for (int i = 0; i < cursor && i < (int)code.size(); ++i)
    if (code[(size_t)i] == '\n') ++line;
  return line;
}

void editorCursorToLine(const std::string& code, int line, int& cursor) {
  int cur = 1;
  for (size_t i = 0; i <= code.size(); ++i) {
    if (cur == line) {
      cursor = (int)i;
      return;
    }
    if (i == code.size()) break;
    if (code[i] == '\n') ++cur;
  }
  cursor = (int)code.size();
}

bool widgetCodeEditor(UI& ui, unsigned id, Rect r, std::string& code, int& cursor, int errLine, float& scroll, int& selAnchor) {
  const float px = 14.0f;
  const float lh = 18.0f;
  quad(r.x, r.y, r.w, r.h, 0x0c0f14ff);
  frame(r.x, r.y, r.w, r.h, ui.focus == (int)id ? C_ACCENT : C_BORDER);
  bool focused = ui.focus == (int)id;
  if (over(r, ui) && ui.wheel) scroll += ui.wheel * lh * 3;
  int lines = 1;
  for (char c : code)
    if (c == '\n') ++lines;
  float contentH = lh * (lines + 2);
  scroll = std::max(0.0f, std::min(scroll, std::max(0.0f, contentH - (r.h - 8))));

  // позиция курсора по точке мыши (колонка по ширине символов, UTF-8)
  auto lineBounds = [&](int c) {
    int st = (int)code.rfind('\n', (size_t)std::max(0, c - 1));
    st = (c == 0 || st < 0) ? 0 : st + 1;
    int en = (int)code.find('\n', (size_t)st);
    if (en < 0) en = (int)code.size();
    return std::make_pair(st, en);
  };
  auto posAt = [&](float mx, float my) {
    int targetLine = (int)((my + scroll - (r.y + 4)) / lh) + 1;
    if (targetLine < 1) targetLine = 1;
    if (targetLine > lines) targetLine = lines;
    size_t ls = 0, le = 0;
    int no = 1;
    while (no <= lines) {
      le = code.find('\n', ls);
      if (le == std::string::npos) le = code.size();
      if (no >= targetLine) break;
      ls = le + 1;
      ++no;
    }
    float x = r.x + 40;
    size_t i = ls;
    while (i < le) {
      size_t c = 1;
      while (i + c < le && (code[(size_t)(i + c)] & 0xC0) == 0x80) ++c;
      float cw = textWidth(code.substr(i, c), px);
      if (mx < x + cw * 0.5f) break;
      x += cw;
      i += c;
    }
    return (int)i;
  };

  static bool edDrag = false;
  if (ui.lPressed && over(r, ui) && !ui.guardClicks) {
    if (!focused) ui.takeFocus(id, code);
    focused = true;
    int p = posAt(ui.mx, ui.my);
    if (!(ui.shift && selAnchor >= 0)) selAnchor = p;
    cursor = p;
    edDrag = true;
    ui.lPressed = false;
  }
  if (edDrag && focused && ui.lDown) {
    if (selAnchor < 0) selAnchor = cursor;
    cursor = posAt(ui.mx, ui.my);
  }
  if (!ui.lDown) edDrag = false;

  bool changed = false;
  auto cursorCol = [&]() {
    int lineStart = (int)code.rfind('\n', (size_t)std::max(0, cursor - 1));
    lineStart = cursor == 0 ? 0 : (lineStart == (int)std::string::npos ? 0 : lineStart + 1);
    return cursor - lineStart;
  };
  auto delUtf8 = [&](int pos, int dir) {
    int nb = 1;
    if (dir < 0) {
      while (pos - nb > 0 && (code[(size_t)(pos - nb)] & 0xC0) == 0x80) ++nb;
      code.erase((size_t)(pos - nb), (size_t)nb);
      cursor -= nb;
    } else {
      while (pos + nb < (int)code.size() && (code[(size_t)(pos + nb)] & 0xC0) == 0x80) ++nb;
      code.erase((size_t)pos, (size_t)nb);
    }
    changed = true;
  };
  auto selErase = [&]() {
    int a = std::min(selAnchor, cursor), b = std::max(selAnchor, cursor);
    if (selAnchor >= 0 && a < b) {
      code.erase((size_t)a, (size_t)(b - a));
      cursor = a;
      selAnchor = -1;
      changed = true;
      return true;
    }
    return false;
  };
  for (const auto& k : ui.keys) {
    if (ui.ctrl) {
      if (k == "x" || k == "X") { ui.wantBuild = true; continue; }
      if (k == "c" || k == "C") {
        int a, b;
        if (selAnchor >= 0) { a = std::min(selAnchor, cursor); b = std::max(selAnchor, cursor); }
        else { auto pr = lineBounds(cursor); a = pr.first; b = pr.second; }
        SDL_SetClipboardText(code.substr((size_t)a, (size_t)(b - a)).c_str());
        continue;
      }
      if (k == "v" || k == "V") {
        char* cb = SDL_GetClipboardText();
        if (cb && *cb) {
          std::string ins;
          for (char ch : std::string(cb)) if (ch != '\r') ins += ch;
          SDL_free(cb);
          selErase();
          code.insert((size_t)cursor, ins);
          cursor += (int)ins.size();
          changed = true;
        } else if (cb) SDL_free(cb);
        continue;
      }
      if (k == "k" || k == "K") {
        auto pr = lineBounds(cursor);
        int en = pr.second;
        if (en < (int)code.size()) ++en;
        SDL_SetClipboardText(code.substr((size_t)pr.first, (size_t)(en - pr.first)).c_str());
        code.erase((size_t)pr.first, (size_t)(en - pr.first));
        cursor = pr.first;
        selAnchor = -1;
        changed = true;
        continue;
      }
      continue; // ctrl+прочее — не редактируем
    }
    if (k == "Left") {
      if (ui.shift) { if (selAnchor < 0) selAnchor = cursor; }
      else selAnchor = -1;
      while (cursor > 0 && (code[(size_t)(cursor - 1)] & 0xC0) == 0x80) --cursor;
      if (cursor > 0) --cursor;
    } else if (k == "Right") {
      if (ui.shift) { if (selAnchor < 0) selAnchor = cursor; }
      else selAnchor = -1;
      if (cursor < (int)code.size()) ++cursor;
      while (cursor < (int)code.size() && (code[(size_t)cursor] & 0xC0) == 0x80) ++cursor;
    } else if (k == "Up" || k == "Down") {
      if (ui.shift) { if (selAnchor < 0) selAnchor = cursor; }
      else selAnchor = -1;
      int line = editorLineFromCursor(code, cursor);
      int col = cursorCol();
      line += k == "Up" ? -1 : 1;
      if (line < 1) line = 1;
      editorCursorToLine(code, line, cursor);
      cursor += std::min(col, (int)code.size() - cursor);
      int le = (int)code.find('\n', (size_t)cursor);
      if (le == (int)std::string::npos) le = (int)code.size();
      cursor = std::min(cursor, le);
    } else if (k == "Backspace" && cursor > 0) {
      if (!selErase()) delUtf8(cursor, -1);
    } else if (k == "Delete") {
      if (!selErase() && cursor < (int)code.size()) delUtf8(cursor, +1);
    } else if (k == "Return" || k == "Enter" || k == "Keypad Enter") {
      if (!selErase()) {
        code.insert((size_t)cursor, "\n");
        ++cursor;
        changed = true;
      }
    } else if (k == "Home") {
      while (cursor > 0 && code[(size_t)(cursor - 1)] != '\n') --cursor;
    } else if (k == "End") {
      while (cursor < (int)code.size() && code[(size_t)cursor] != '\n') ++cursor;
    } else if (k == "Tab") {
      code.insert((size_t)cursor, "  ");
      cursor += 2;
      changed = true;
    }
  }
  if (!ui.entered.empty()) {
    std::string clean;
    for (char c : ui.entered)
      if (c != '\r' && c != '\n' && (unsigned char)c >= 0x20) clean += c;
    if (!clean.empty()) {
      selErase();
      code.insert((size_t)cursor, clean);
      cursor += (int)clean.size();
      changed = true;
    }
  }
  // удержание Backspace: после задержки продолжает удалять по символу, пока зажата
  if (focused) {
    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    bool held = kb[SDL_GetScancodeFromKey(SDLK_BACKSPACE)] != 0;
    static Uint32 holdStart = 0, holdNext = 0;
    if (held) {
      Uint32 now = SDL_GetTicks();
      if (holdStart == 0) { holdStart = now; holdNext = now + 320; }
      else if (now >= holdNext && cursor > 0) {
        delUtf8(cursor, -1);
        holdNext = now + 45;
      }
    } else holdStart = 0;
  }
  // отрисовка
  int curLine = editorLineFromCursor(code, cursor);
  int selLo = selAnchor >= 0 ? std::min(selAnchor, cursor) : 0;
  int selHi = selAnchor >= 0 ? std::max(selAnchor, cursor) : 0;
  bool hasSel = selAnchor >= 0 && selLo < selHi;
  pushClip(r.x + 1, r.y + 1, r.w - 2, r.h - 2);
  float ty = r.y + 4 - scroll;
  size_t ls = 0;
  int lineNo = 1;
  while (ls <= code.size()) {
    size_t le = code.find('\n', ls);
    if (le == std::string::npos) le = code.size();
    std::string ln = code.substr(ls, le - ls);
    if (ty > r.y - lh && ty < r.y + r.h) {
      if (lineNo == errLine) quad(r.x + 2, ty - 2, r.w - 4, lh, 0x551c1cff);
      else if (lineNo == curLine && focused) quad(r.x + 2, ty - 2, r.w - 4, lh, 0x1a212bff);
      if (hasSel) {
        int sa = (int)std::max((size_t)selLo, ls), sb = (int)std::min((size_t)selHi, le);
        if (sa < sb && selHi > (int)ls && selLo < (int)le && sb >= sa) {
          float x0 = r.x + 40 + textWidth(code.substr(ls, (size_t)(sa - (int)ls)), px);
          float x1 = r.x + 40 + textWidth(code.substr(ls, (size_t)(sb - (int)ls)), px);
          quad(x0, ty - 2, std::max(2.0f, x1 - x0), lh, 0x2d5176cc);
        }
      }
      char no[16];
      snprintf(no, sizeof(no), "%d", lineNo);
      text(r.x + 6, ty, no, 0x4d5766ff, px - 2);
      text(r.x + 40, ty, ln, lineNo == errLine ? 0xffb3adff : C_TEXT, px);
    }
    if (focused && cursor >= (int)ls && cursor <= (int)le) {
      float cx = r.x + 40 + textWidth(ln.substr(0, (size_t)std::max(0, cursor - (int)ls)), px);
      if ((SDL_GetTicks() / 400) % 2 == 0) quad(cx, ty - 2, 1.5f, lh, C_ACCENTHI);
    }
    ++lineNo;
    ty += lh;
    if (le == code.size()) break;
    ls = le + 1;
  }
  popClip();
  return changed;
}

// ---------- список ----------
int widgetErrorList(UI& ui, Rect r, const std::string& title, const std::vector<std::string>& lines, float& scroll) {
  const float lh = 17.0f;
  int n = (int)lines.size();
  int pick = -1;
  quad(r.x, r.y, r.w, r.h, 0x0d1016ff);
  frame(r.x, r.y, r.w, r.h, C_BORDER);
  if (!title.empty()) widgetLabel(ui, title, {r.x + 6, r.y + 4, r.w, 0}, C_DIM, 13);
  float startY = r.y + 4 + (title.empty() ? 0 : 17.0f);
  if (over(r, ui) && ui.wheel) scroll += (float)ui.wheel * lh * 2;
  scroll = std::max(0.0f, std::min(scroll, std::max(0.0f, (float)n * lh - (r.h - 10))));
  pushClip(r.x + 1, r.y + 1, r.w - 2, r.h - 2);
  for (int i = 0; i < n; ++i) {
    float y = startY + i * lh - scroll;
    if (y < r.y - lh || y > r.y + r.h) continue;
    Rect ir{r.x + 2, y, r.w - 4, lh};
    if (ir.hit(ui.mx, ui.my)) {
      quad(ir.x, ir.y, ir.w, ir.h, 0x23304258);
      if (ui.lPressed) {
        pick = i;
        ui.lPressed = false;
      }
    }
    widgetLabel(ui, lines[(size_t)i], {ir.x + 8, ir.y + 1, ir.w, 0}, 0xb9c3d0ff, 13);
  }
  popClip();
  return pick;
}

} // namespace mpui
