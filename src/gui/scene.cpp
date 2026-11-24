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
  if (app.overAnyPrev) {
    app.hoverN.clear();
    app.hoverS.clear();
    return;
  }
    // пикрование в ЭКРАННЫХ пикселях: хитбокс точно по нарисованному кружку
  auto pxRadius = [&](const mp::V3& p, float rWorld) {
    mp::V3 e = app.cam.eye();
    float dx = (float)(p.x - e.x), dy = (float)(p.y - e.y), dz = (float)(p.z - e.z);
    float D = std::sqrt(dx * dx + dy * dy + dz * dz);
    float fl = 1.0f / std::tan(app.cam.fov * 3.1415926f / 360.0f);
    return rWorld * 0.5f * fl / std::max(0.1f, D);
  };
  std::string bestN;
  float bestDepth = 1e30f;
  for (const auto& n : app.doc.neurons) {
    if (!n.alive) continue;
    float visR = (n.type.set && (n.type.v == 0 || n.type.v == 2)) ? 0.75f : 0.6f;
    float f = 0;
    auto it = app.flash.find(n.id);
    if (it != app.flash.end()) f = (float)it->second;
    if (f > 0.25f) visR += f * 0.25f;
    bool ok = false;
    mp::V3 sp = app.cam.screenPoint(n.pos, ui.winW, ui.winH, &ok);
    if (!ok) continue;
    float pr = pxRadius(n.pos, visR) + 4.0f;
    float dx = sp.x - ui.mx, dy = sp.y - ui.my;
    if (dx * dx + dy * dy <= pr * pr && sp.z < bestDepth) {
      bestN = n.id;
      bestDepth = sp.z;
    }
  }
  std::string bestS;
  float bestSDepth = 1e30f;
  if (bestN.empty()) {
    for (const auto& s : app.doc.synapses) {
      const mp::Neuron* a = app.doc.neuron(s.pre);
      const mp::Neuron* b = app.doc.neuron(s.post);
      if (!a || !b) continue;
      bool oa = false, ob = false;
      mp::V3 pa = app.cam.screenPoint(a->pos, ui.winW, ui.winH, &oa);
      mp::V3 pb = app.cam.screenPoint(b->pos, ui.winW, ui.winH, &ob);
      if (!oa || !ob) continue;
      float ex = pb.x - pa.x, ey = pb.y - pa.y;
      float len2 = ex * ex + ey * ey;
      float tt = len2 > 1e-6f ? ((ui.mx - pa.x) * ex + (ui.my - pa.y) * ey) / len2 : 0.0f;
      tt = clampf(tt, 0.0f, 1.0f);
      float cx = pa.x + ex * tt - ui.mx, cy = pa.y + ey * tt - ui.my;
      float wpx = 1.0f + (float)std::min(5.0, std::fabs(s.w) * 1.2) + 8.0f;
      if (cx * cx + cy * cy <= wpx * wpx) {
        float depth = (pa.z + pb.z) * 0.5f;
        if (depth < bestSDepth) {
          bestS = s.id;
          bestSDepth = depth;
        }
      }
    }
  }
  (void)0;
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
  float ext = clampf(app.cam.dist * 1.6f, 40.0f, 800.0f);
  float step = ext > 400 ? 40.0f : ext > 200 ? 20.0f : ext > 100 ? 10.0f : 2.0f;
  unsigned gridC = fadeAlpha(0x2b3440ff, 0.8f);
  unsigned gridM = fadeAlpha(0x3d4a5aff, 0.9f);
  float tx = (float)app.cam.target.x, tz = (float)app.cam.target.z;
  float gx = std::floor((tx - ext) / step) * step;
  float gz = std::floor((tz - ext) / step) * step;
  int mi = 0;
  for (float g = gx; g <= tx + ext + 0.01f; g += step, ++mi)
    line3D(g, 0, tz - ext, g, 0, tz + ext, (mi % 5 == 0) ? 1.4f : 1.0f, (mi % 5 == 0) ? gridM : gridC);
  mi = 0;
  for (float g = gz; g <= tz + ext + 0.01f; g += step, ++mi)
    line3D(tx - ext, 0, g, tx + ext, 0, g, (mi % 5 == 0) ? 1.4f : 1.0f, (mi % 5 == 0) ? gridM : gridC);
  line3D(-ext, 0, 0, ext, 0, 0, 2.0f, 0xd06a6aff);
  line3D(0, 0, -ext, 0, 0, ext, 2.0f, 0x6a88d0ff);
  line3D(0, 0, 0, 0, ext * 0.25f, 0, 2.0f, 0x6ac06aff);

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
  // --- гизмо осевого переноса (режим Движ/G) ---
  if (app.mode == Mode::Move && !app.gizmoIds.empty()) {
    mp::V3 p{0, 0, 0};
    size_t cnt = 0;
    for (const auto& id : app.gizmoIds)
      if (mp::Neuron* q = app.doc.neuron(id)) { p.x += q->pos.x; p.y += q->pos.y; p.z += q->pos.z; ++cnt; }
    if (cnt) p = {p.x / cnt, p.y / cnt, p.z / cnt};
    float L = (float)std::max(6.0, app.cam.dist * 0.25);
    float ln = (float)std::max(3.0, app.cam.dist * 0.012);
    unsigned xc = app.gizmoAxis == 0 ? 0xffb08aff : 0xe06a5aff;
    unsigned yc = app.gizmoAxis == 1 ? 0xbaf08aff : 0x5ac04aff;
    unsigned zc = app.gizmoAxis == 2 ? 0x9ac8ffff : 0x5a88e0ff;
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x + L, (float)p.y, (float)p.z, ln, xc);
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x, (float)p.y + L, (float)p.z, ln, yc);
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x, (float)p.y, (float)p.z + L, ln, zc);
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x + L, (float)p.y, (float)p.z, ln + 2, fadeAlpha(xc, 0.30f));
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x, (float)p.y + L, (float)p.z, ln + 2, fadeAlpha(yc, 0.30f));
    line3D((float)p.x, (float)p.y, (float)p.z, (float)p.x, (float)p.y, (float)p.z + L, ln + 2, fadeAlpha(zc, 0.30f));
    ball3D((float)p.x, (float)p.y, (float)p.z, 1.1f, 0xf2f6ffff);
    }
  glDisable(GL_BLEND);
}

// ---------- 2D-оверлей ----------
void sceneOverlay2D(App& app) {
  UI& ui = app.ui;
  // подписи мировых осей
  if (app.cam.dist > 0) {
    float ea = clampf(app.cam.dist * 0.6f, 8.0f, 50.0f);
    mp::V3 exs{(double)ea, 0, 0}, ezs{0, 0, (double)ea};
    bool o1 = false, o2 = false;
    mp::V3 s1 = app.cam.screenPoint(exs, app.ui.winW, app.ui.winH, &o1);
    mp::V3 s2 = app.cam.screenPoint(ezs, app.ui.winW, app.ui.winH, &o2);
    if (o1) mpui::text(s1.x + 4, s1.y - 16, "X", 0xd08a8aff, 16);
    if (o2) mpui::text(s2.x + 4, s2.y - 16, "Z", 0x8aa0d0ff, 16);
  }
  // подписи осей гизма
  if (app.mode == Mode::Move && !app.gizmoIds.empty()) {
    mp::V3 c{0, 0, 0};
    size_t cnt = 0;
    for (const auto& id : app.gizmoIds)
      if (mp::Neuron* q = app.doc.neuron(id)) { c.x += q->pos.x; c.y += q->pos.y; c.z += q->pos.z; ++cnt; }
    if (cnt) {
      c = {c.x / cnt, c.y / cnt, c.z / cnt};
      float L = (float)std::max(6.0, app.cam.dist * 0.25);
      mp::V3 ends[3] = {{c.x + L, c.y, c.z}, {c.x, c.y + L, c.z}, {c.x, c.y, c.z + L}};
      const char* nm[3] = {"X", "Y", "Z"};
      unsigned cols[3] = {0xffb08aff, 0xbaf08aff, 0x9ac8ffff};
      for (int k = 0; k < 3; ++k) {
        bool ok = false;
        mp::V3 sp = app.cam.screenPoint(ends[k], app.ui.winW, app.ui.winH, &ok);
        if (ok) mpui::text(sp.x + 6, sp.y - 16, nm[k], cols[k], 18);
      }
    }
  }

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
