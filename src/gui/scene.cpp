#include "scene.hpp"

#include "widgets.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace mpui {

// ---------- цвета ----------
static unsigned typeColor(int t) {
  switch (t) {
    case 0: return 0x5fd471ff; // sensor
    case 1: return 0x6fa8dcff; // hidden
    case 2: return 0xe8a33dff; // motor
    case 3: return 0xb07fe0ff; // modulator
    case 4: return 0x4fc3c3ff; // homeostat
  }
  return 0x9aa7b4ff;
}

static unsigned mixw(unsigned base, float w) { // w 0..1 — яркость к белому
  int r = (base >> 24) & 0xff, g = (base >> 16) & 0xff, b = (base >> 8) & 0xff;
  r = (int)(r + (255 - r) * w);
  g = (int)(g + (255 - g) * w);
  b = (int)(b + (255 - b) * w);
  return ((unsigned)r << 24) | ((unsigned)g << 16) | ((unsigned)b << 8) | 0xff;
}

static unsigned fadeAlpha(unsigned c, float a) {
  return (c & 0xffffff00u) | (unsigned)(std::max(0.0f, std::min(1.0f, a)) * 255.0f);
}

static float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }

// ---------- picking (5.7) ----------
void scenePick(App& app) {
  UI& ui = app.ui;
  Ray ray = app.cam.pickRay(ui.mx, ui.my, ui.winW, ui.winH);
  const float tbase = std::max(0.8f, app.cam.dist * 0.02f);
  std::string bestN;
  float bestNT = 1e30f;
  for (const auto& n : app.doc.neurons) {
    if (!n.alive) continue;
    float d = 0, t = 0;
    if (!rayPoint(ray, n.pos, d, &t)) continue;
    if (d < tbase && t < bestNT) {
      bestN = n.id;
      bestNT = t;
    }
  }
  std::string bestS;
  float bestST = 1e30f;
  if (bestN.empty()) {
    for (const auto& s : app.doc.synapses) {
      const mp::Neuron* a = app.doc.neuron(s.pre);
      const mp::Neuron* b = app.doc.neuron(s.post);
      if (!a || !b) continue;
      float d = 0, t = 0;
      if (!raySegment(ray, a->pos, b->pos, d, &t)) continue;
      if (d < tbase * 0.6f && t < bestST) {
        bestS = s.id;
        bestST = t;
      }
    }
  }
  app.hoverN = bestN;
  app.hoverS = bestS;
}

// ---------- 3D ----------
void sceneDraw3D(App& app) {
  UI& ui = app.ui;
  glClearColor(0.169f, 0.176f, 0.188f, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf(app.cam.proj(ui.winW, ui.winH).m);
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf(app.cam.view().m);

  // --- сетка ---
  float ext = clampf(app.cam.dist * 1.2f, 24.0f, 500.0f);
  float step = ext > 120 ? 10.0f : 2.0f;
  unsigned gridC = fadeAlpha(0x2b3440ff, 0.8f);
  for (float i = -ext; i <= ext + 0.01f; i += step) {
    line3D(i, 0, -ext, i, 0, ext, 1, gridC);
    line3D(-ext, 0, i, ext, 0, i, 1, gridC);
  }
  line3D(-ext, 0, 0, ext, 0, 0, 1.5f, 0x8a5a5aff);
  line3D(0, 0, -ext, 0, 0, ext, 1.5f, 0x5a708aff);
  line3D(0, 0, 0, 0, ext * 0.25f, 0, 1.5f, 0x608a5aff);

  // --- зоны-группы не геометричны; кластеры (3.x) полупрозрачные ---
  for (const auto& c : app.doc.clusters) {
    const mp::Zone* z = app.doc.zone(c.zone);
    unsigned col = z ? (parseColor(z->color, 0x3f8fddff) & 0xffffff00u) : 0x3f8fdd00u;
    col |= 0x22;
    float r = (float)(c.radius.set ? c.radius.v : 8.0);
    if (c.shape.v == 1) { // grid — рисуем сферу-габарит
      sphere3D((float)c.center.x, (float)c.center.y, (float)c.center.z, r, col, 8);
    } else if (c.shape.v == 2) { // box
      float x = (float)c.center.x, y = (float)c.center.y, zz = (float)c.center.z;
      for (int e = 0; e < 12; ++e) {
        static const int seg[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
        float s = r / 1.41f;
        float px[24] = {x-s,y-s,zz-s, x+s,y-s,zz-s, x-s,y+s,zz-s, x+s,y+s,zz-s, x-s,y-s,zz+s, x+s,y-s,zz+s, x-s,y+s,zz+s, x+s,y+s,zz+s};
        line3D(px[seg[e][0]*3], px[seg[e][0]*3+1], px[seg[e][0]*3+2],
               px[seg[e][1]*3], px[seg[e][1]*3+1], px[seg[e][1]*3+2], 1.2f, fadeAlpha(col | 0x50, 1));
      }
      sphere3D(x, y, zz, r, fadeAlpha(col, 0.6f), 6);
    } else {
      sphere3D((float)c.center.x, (float)c.center.y, (float)c.center.z, r, col, 10);
    }
  }

  // --- сосед-подсветка (6.5): смежность фокуса один раз за кадр ---
  std::set<std::string> adj;
  if (!app.focusUnit.empty()) {
    for (const auto& s : app.doc.synapses)
      if (s.alive && (s.pre == app.focusUnit || s.post == app.focusUnit)) {
        adj.insert(s.pre);
        adj.insert(s.post);
      }
  }
  auto lit = [&](const std::string& id) {
    if (app.focusUnit.empty()) return 1.0f;
    if (id == app.focusUnit) return 1.15f;
    return adj.count(id) ? 0.95f : 0.35f;
  };

  // --- связи (6.4) ---
  for (const auto& s : app.doc.synapses) {
    const mp::Neuron* a = app.doc.neuron(s.pre);
    const mp::Neuron* b = app.doc.neuron(s.post);
    if (!a || !b) continue;
    bool sel = app.selS.count(s.id) > 0 || app.hoverS == s.id;
    float br = lit(s.pre) * (s.pre == app.focusUnit || s.post == app.focusUnit || app.focusUnit.empty() ? 1.0f : 0.45f);
    unsigned col;
    if (!s.enabled)
      col = fadeAlpha(0x5a5a5aff, br);
    else if ((s.sign.v == 1))
      col = mixw(0xd25a5aff, clampf((float)(std::fabs(s.w) * 0.25f), 0, 0.5f));
    else
      col = mixw(0x59c08aff, clampf((float)(std::fabs(s.w) * 0.25f), 0, 0.5f));
    float w = 1.0f + (float)std::min(5.0, std::fabs(s.w) * 1.2);
    col = fadeAlpha(col, clampf(br, 0.15f, 1.0f));
    if (sel) {
      col = 0xf2f6ffff;
      w += 1.0f;
    }
    if (!s.enabled) {
      // штриховая пунктирная линия
      mp::V3 p0 = a->pos, p1 = b->pos;
      for (int i = 0; i < 6; ++i) {
        float f0 = i / 6.0f, f1 = f0 + 1.0f / 12.0f;
        line3D((float)(p0.x + (p1.x - p0.x) * f0), (float)(p0.y + (p1.y - p0.y) * f0), (float)(p0.z + (p1.z - p0.z) * f0),
               (float)(p0.x + (p1.x - p0.x) * f1), (float)(p0.y + (p1.y - p0.y) * f1), (float)(p0.z + (p1.z - p0.z) * f1),
               w, col);
      }
    } else {
      line3D((float)a->pos.x, (float)a->pos.y, (float)a->pos.z, (float)b->pos.x, (float)b->pos.y, (float)b->pos.z, w, col);
    }
  }

  // --- импульсы (6.3, view-layer из spiked-диффа) ---
  for (const auto& im : app.impulses) {
    const mp::Neuron* a = app.doc.neuron(im.pre);
    const mp::Neuron* b = app.doc.neuron(im.post);
    if (!a || !b) continue;
    mp::V3 p{a->pos.x + (b->pos.x - a->pos.x) * im.t, a->pos.y + (b->pos.y - a->pos.y) * im.t,
             a->pos.z + (b->pos.z - a->pos.z) * im.t};
    unsigned col = fadeAlpha(0xffe08aff, clampf(0.3f + (float)std::fabs(im.w) * 0.5f, 0, 1));
    ball3D((float)p.x, (float)p.y, (float)p.z, 0.34f, col);
  }

  // --- нейроны (6.2) ---
  for (const auto& n : app.doc.neurons) {
    if (!n.alive) continue;
    float f = (float)clampf(app.flash.count(n.id) ? app.flash[n.id] : 0.0, 0.0, 1.0);
    float activity = app.weightMap
                         ? clampf((float)(n.inWeightSum / 8.0), 0, 1)
                         : clampf((float)(n.lastInSum / 3.0) + f * 1.5f, 0, 1);
    unsigned base = typeColor(n.type.v);
    if (n.disabled) base = 0x8a3030ff;
    unsigned col = mixw(base, activity * 0.75f);
    float br = lit(n.id);
    col = fadeAlpha(col, clampf(br, 0.15f, 1.0f));
    bool sel = app.selN.count(n.id) > 0;
    bool hov = app.hoverN == n.id || app.focusUnit == n.id;
    float r = n.type.v == 0 || n.type.v == 2 ? 0.75f : 0.6f;
    if (f > 0.25f) r += f * 0.35f;
    if (sel || hov) {
      ball3D((float)n.pos.x, (float)n.pos.y, (float)n.pos.z, r + 0.42f,
             sel ? 0x4d9be0ff : 0x7a8898aa);
    }
    ball3D((float)n.pos.x, (float)n.pos.y, (float)n.pos.z, r, col);
    if (n.disabled) {
      line3D((float)n.pos.x - 0.8f, (float)n.pos.y, (float)n.pos.z, (float)n.pos.x + 0.8f, (float)n.pos.y, (float)n.pos.z, 2, 0xff7060e0);
      line3D((float)n.pos.x, (float)n.pos.y, (float)n.pos.z - 0.8f, (float)n.pos.x, (float)n.pos.y, (float)n.pos.z + 0.8f, 2, 0xff7060e0);
    }
  }

  // --- предпросмотр связи (5.2) ---
  if (app.mode == Mode::Wire && app.wirePre) {
    const mp::Neuron* a = app.doc.neuron(app.wireFrom);
    if (a) {
      line3D((float)a->pos.x, (float)a->pos.y, (float)a->pos.z, (float)app.lastPos.x, (float)app.lastPos.y,
             (float)app.lastPos.z, 2, 0x79c0ffcc);
      ball3D((float)app.lastPos.x, (float)app.lastPos.y, (float)app.lastPos.z, 0.8f, 0x79c0ff88);
    }
  }
  // --- призрак установки (5.1) ---
  if (app.mode == Mode::Neuron && !app.place.open && !app.overAny) {
    ball3D((float)app.lastPos.x, 0.0f + 0.7f, (float)app.lastPos.z, 0.7f, 0x6fa8dc66);
  }
  // --- гизмо осевого переноса (G) ---
  if (app.gizmoOn && app.doc.neuron(app.gizmoId)) {
    mp::V3 p = app.doc.neuron(app.gizmoId)->pos;
    float L = (float)std::max(6.0, app.cam.dist * 0.06);
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x + L, (float)p.y, (float)p.z, 2.5f, app.gizmoAxis == 0 ? 0xff8a5aff : 0xc0504a99);
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x, (float)p.y + L, (float)p.z, 2.5f, app.gizmoAxis == 1 ? 0x9ade6aff : 0x4f8a3d99);
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x, (float)p.y, (float)p.z + L, 2.5f, app.gizmoAxis == 2 ? 0x6aa8ffff : 0x3d5f9e99);
    ball3D((float)p.x, (float)p.y, (float)p.z, 0.9f, 0xf2f6ffff);
  }
  glDisable(GL_BLEND);
}

// ---------- 2D-оверлей ----------
void sceneOverlay2D(App& app) {
  UI& ui = app.ui;
  // гизмо-куб ориентации в углу
  {
    float cx = 58, cy = (float)ui.winH - 58, R = 34;
    quad(cx - 46, cy - 46, 92, 92, 0x2f323740);
    frame(cx - 46, cy - 46, 92, 92, 0x52555a88);
    const double ax[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const unsigned cols[3] = {0xd0605aff, 0x7cc45aff, 0x5f9edaff};
    const char* lbl[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
      mp::V3 q = app.cam.target;
      q.x += ax[i][0]; q.y += ax[i][1]; q.z += ax[i][2];
      mp::V3 s0 = app.cam.screenPoint(app.cam.target, ui.winW, ui.winH);
      mp::V3 s1 = app.cam.screenPoint(q, ui.winW, ui.winH);
      float dx = s1.x - s0.x, dy = s1.y - s0.y;
      float len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-3f) continue;
      // передняя ось ярче, задняя тусклее: по глубине камеры
      mp::V3 e1 = app.cam.eye();
      float depth = (float)((q.x - e1.x) * ax[i][0] + (q.y - e1.y) * ax[i][1] + (q.z - e1.z) * ax[i][2]);
      unsigned col = (depth < 0 ? cols[i] : (cols[i] & 0xffffff00) | 0x60);
      text(cx + dx / len * R - 5, cy + dy / len * R - 8, lbl[i], col, 13);
      line2D(cx - dx / len * 8, cy - dy / len * 8, cx + dx / len * R, cy + dy / len * R, col);
    }
  }
  // хинт режима кладёт HUD (panels.cpp)
  // ID-билборды (6.1), масштаб от дистанции
  if (app.showIds) {
      for (const auto& n : app.doc.neurons) {
      if (!n.alive) continue;
      bool ok = false;
      mp::V3 sp = app.cam.screenPoint(n.pos, ui.winW, ui.winH, &ok);
      if (!ok || sp.z > 1) continue;
      float d = (float)std::sqrt((n.pos.x - app.cam.eye().x) * (n.pos.x - app.cam.eye().x) +
                                 (n.pos.y - app.cam.eye().y) * (n.pos.y - app.cam.eye().y) +
                                 (n.pos.z - app.cam.eye().z) * (n.pos.z - app.cam.eye().z));
      float px = clampf(260.0f / std::max(6.0f, d), 9.0f, 15.0f);
      unsigned col = app.selN.count(n.id) ? 0xcfe6ffff : fadeAlpha(0xaebdd0ff, clampf(2.2f - d / 90.0f, 0.25f, 0.9f));
      text(sp.x + 10, sp.y - px - 4, n.label.empty() ? n.id : n.label, col, px);
    }
  }
  // рамка выделения (5.4)
  if (app.rubber) {
    float x0 = std::min(app.rubX0, app.rubX1), y0 = std::min(app.rubY0, app.rubY1);
    float w = std::fabs(app.rubX1 - app.rubX0), h = std::fabs(app.rubY1 - app.rubY0);
    quad(x0, y0, w, h, 0x4390e026);
    frame(x0, y0, w, h, 0x4390e0aa);
  }
}

} // namespace mpui
