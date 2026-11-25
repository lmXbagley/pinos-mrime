#pragma once
// gui/draw.hpp — слой 2D-отрисовки поверх OpenGL (fixed function).
// Атлас шрифтов: font_atlas.hpp (запечён заранее, внешних зависимостей нет).
#include <string>

namespace mpui {

void initDraw();            // один раз после SDL_GL_CreateContext

void begin2D(int winW, int winH);
void end2D();

// --- примитивы 2D (координаты — экранные, y вниз) ---
void quad(float x, float y, float w, float h, unsigned rgba);
void frame(float x, float y, float w, float h, unsigned rgba, float t = 1.0f);
void text(float x, float y, const std::string& utf8, unsigned rgba, float px = 15.0f);
void line2D(float x0, float y0, float x1, float y1, unsigned rgba);
void disc2D(float x, float y, float r, unsigned rgba);
float textWidth(const std::string& utf8, float px = 15.0f);
float fontHeight(float px = 15.0f);

void pushClip(float x, float y, float w, float h);
void popClip();

// --- 3D-хелперы (используется текущая матрица modelview/projection) ---
void line3D(float ax, float ay, float az, float bx, float by, float bz, float w, unsigned rgba);
void ball3D(float x, float y, float z, float r, unsigned rgba); // billboard-диск
void sphere3D(float x, float y, float z, float r, unsigned rgba, int seg = 10);

// "#rrggbb" -> 0xrrggbbff; пусто -> fallback
unsigned parseColor(const std::string& hex, unsigned fallback = 0x8a9ab0ff);

} // namespace mpui
