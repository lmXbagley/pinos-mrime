// gui/draw.cpp — 2D/3D-отрисовка: шрифтовой атлас (GL_ALPHA), примитивы, хелперы.
#include "draw.hpp"
#include "font_atlas.hpp"

#include <SDL2/SDL_opengl.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace mpui {

// ---------- утилиты ----------
static uint32_t nextCp(const char*& s) {
  unsigned char c = (unsigned char)*s++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) {
    uint32_t r = c & 0x1F;
    if (*s) r = (r << 6) | (*s++ & 0x3F);
    return r;
  }
  if ((c & 0xF0) == 0xE0) {
    uint32_t r = c & 0x0F;
    if (*s) r = (r << 6) | (*s++ & 0x3F);
    if (*s) r = (r << 6) | (*s++ & 0x3F);
    return r;
  }
  if ((c & 0xF8) == 0xF0) {
    uint32_t r = c & 0x07;
    int k = 3;
    while (k-- && *s) r = (r << 6) | (*s++ & 0x3F);
    return r;
  }
  return 0xFFFD;
}

unsigned parseColor(const std::string& hex, unsigned fallback) {
  if (hex.size() >= 7 && hex[0] == '#') {
    unsigned r, g, b;
    if (sscanf(hex.c_str() + 1, "%2x%2x%2x", &r, &g, &b) == 3) return (r << 24) | (g << 16) | (b << 8) | 0xff;
  }
  return fallback;
}

// ---------- атлас ----------
static unsigned gTex = 0;
static int gWinH = 0;
struct ClipRect { float x, y, w, h; };

void initDraw() {
  if (gTex == 0) {
    glGenTextures(1, &gTex);
    glBindTexture(GL_TEXTURE_2D, gTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlasW, kAtlasH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, kAtlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

void begin2D(int winW, int winH) {
  (void)winW;
  gWinH = winH;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, winW, winH, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glDisable(GL_SCISSOR_TEST);
}

void end2D() {
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glEnable(GL_DEPTH_TEST);
}

// ---------- примитивы ----------
void quad(float x, float y, float w, float h, unsigned rgba) {
  glDisable(GL_TEXTURE_2D);
  glColor4ub(GLubyte((rgba >> 24) & 0xff), GLubyte((rgba >> 16) & 0xff), GLubyte((rgba >> 8) & 0xff),
             GLubyte(rgba & 0xff));
  glBegin(GL_QUADS);
  glVertex2f(x, y);
  glVertex2f(x + w, y);
  glVertex2f(x + w, y + h);
  glVertex2f(x, y + h);
  glEnd();
}

void frame(float x, float y, float w, float h, unsigned rgba, float t) {
  if (w <= 0 || h <= 0) return;
  quad(x, y, w, t, rgba);
  quad(x, y + h - t, w, t, rgba);
  quad(x, y + t, t, h - 2 * t, rgba);
  quad(x + w - t, y + t, t, h - 2 * t, rgba);
}

void text(float x, float y, const std::string& utf8, unsigned rgba, float px) {
  if (utf8.empty()) return;
  glColor4ub(GLubyte((rgba >> 24) & 0xff), GLubyte((rgba >> 16) & 0xff), GLubyte((rgba >> 8) & 0xff),
             GLubyte(rgba & 0xff));
  glBindTexture(GL_TEXTURE_2D, gTex);
  glEnable(GL_TEXTURE_2D);
  float scale = px / 16.0f;
  float cx = std::floor(x);
  float cy = std::floor(y);
  glBegin(GL_QUADS);
  const char* s = utf8.c_str();
  while (*s) {
    uint32_t cp = nextCp(s);
    const Glyph* g = glyph(cp);
    if (!g) g = glyph('?');
    if (g && g->w > 0) {
      float u0 = g->u / (float)kAtlasW, v0 = g->v / (float)kAtlasH;
      float u1 = (g->u + g->w) / (float)kAtlasW, v1 = (g->v + g->h) / (float)kAtlasH;
      float dx = cx + g->ox * scale, dy = cy + g->oy * scale;
      float dw = g->w * scale, dh = g->h * scale;
      glTexCoord2f(u0, v0);
      glVertex2f(dx, dy);
      glTexCoord2f(u1, v0);
      glVertex2f(dx + dw, dy);
      glTexCoord2f(u1, v1);
      glVertex2f(dx + dw, dy + dh);
      glTexCoord2f(u0, v1);
      glVertex2f(dx, dy + dh);
    }
    cx += g->adv * scale;
  }
  glEnd();
  glDisable(GL_TEXTURE_2D);
}

float textWidth(const std::string& utf8, float px) {
  float scale = px / 16.0f, w = 0;
  const char* s = utf8.c_str();
  while (*s) {
    uint32_t cp = nextCp(s);
    const Glyph* g = glyph(cp);
    if (!g) g = glyph('?');
    w += (g ? g->adv : 8) * scale;
  }
  return w;
}

float fontHeight(float px) { return px * 1.26f; }

// ---------- клип ----------
void pushClip(float x, float y, float w, float h) {
  glFlush();
  glEnable(GL_SCISSOR_TEST);
  int sy = gWinH - int(y + h);
  glScissor(int(x), sy, int(std::max(0.0f, w)), int(std::max(0.0f, h)));
}
void popClip() {
  glDisable(GL_SCISSOR_TEST);
}

// ---------- 3D ----------
void line3D(float ax, float ay, float az, float bx, float by, float bz, float w, unsigned rgba) {
  glDisable(GL_TEXTURE_2D);
  glColor4ub(GLubyte((rgba >> 24) & 0xff), GLubyte((rgba >> 16) & 0xff), GLubyte((rgba >> 8) & 0xff),
             GLubyte(rgba & 0xff));
  glLineWidth(w);
  glBegin(GL_LINES);
  glVertex3f(ax, ay, az);
  glVertex3f(bx, by, bz);
  glEnd();
}

void ball3D(float x, float y, float z, float r, unsigned rgba) {
  float mv[16];
  glGetFloatv(GL_MODELVIEW_MATRIX, mv);
  float rx = mv[0], ry = mv[4], rz = mv[8];
  float ux = mv[1], uy = mv[5], uz = mv[9];
  glColor4ub(GLubyte((rgba >> 24) & 0xff), GLubyte((rgba >> 16) & 0xff), GLubyte((rgba >> 8) & 0xff),
             GLubyte(rgba & 0xff));
  glDisable(GL_TEXTURE_2D);
  glBegin(GL_TRIANGLE_FAN);
  glVertex3f(x, y, z);
  for (int i = 0; i <= 14; ++i) {
    float a = i / 14.0f * 6.2831853f;
    float ca = std::cos(a) * r, sa = std::sin(a) * r;
    glVertex3f(x + ca * rx + sa * ux, y + ca * ry + sa * uy, z + ca * rz + sa * uz);
  }
  glEnd();
}

void sphere3D(float x, float y, float z, float r, unsigned rgba, int seg) {
  glColor4ub(GLubyte((rgba >> 24) & 0xff), GLubyte((rgba >> 16) & 0xff), GLubyte((rgba >> 8) & 0xff),
             GLubyte(rgba & 0xff));
  glDisable(GL_TEXTURE_2D);
  for (int i = 0; i < seg; ++i) {
    float t0 = 3.1415926f * i / seg, t1 = 3.1415926f * (i + 1) / seg;
    float c0 = std::cos(t0), c1 = std::cos(t1);
    float s0 = std::sin(t0), s1 = std::sin(t1);
    glBegin(GL_TRIANGLE_STRIP);
    for (int j = 0; j <= seg; ++j) {
      float p = 6.2831853f * j / seg;
      float cp = std::cos(p), sp = std::sin(p);
      glVertex3f(x + r * s0 * cp, y + r * c0, z + r * s0 * sp);
      glVertex3f(x + r * s1 * cp, y + r * c1, z + r * s1 * sp);
    }
    glEnd();
  }
}


void disc2D(float x, float y, float r, unsigned rgba) {
  glDisable(GL_TEXTURE_2D);
  glColor4ub(GLubyte((rgba >> 24) & 0xff), GLubyte((rgba >> 16) & 0xff), GLubyte((rgba >> 8) & 0xff),
             GLubyte(rgba & 0xff));
  glBegin(GL_TRIANGLE_FAN);
  glVertex2f(x, y);
  for (int i = 0; i <= 16; ++i) {
    float a = i / 16.0f * 6.2831853f;
    glVertex2f(x + std::cos(a) * r, y + std::sin(a) * r);
  }
  glEnd();
}

void line2D(float x0, float y0, float x1, float y1, unsigned rgba) {
  glDisable(GL_TEXTURE_2D);
  glColor4ub(GLubyte((rgba >> 24) & 0xff), GLubyte((rgba >> 16) & 0xff), GLubyte((rgba >> 8) & 0xff),
             GLubyte(rgba & 0xff));
  glBegin(GL_LINES);
  glVertex2f(x0, y0);
  glVertex2f(x1, y1);
  glEnd();
}
} // namespace mpui
