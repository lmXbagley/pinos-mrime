#pragma once
// gui/camera.hpp — орбитальная камера CAD-вьюпорта + честный picking (5.7):
// пиксель -> луч (инверсия MVP), пересечение с юнитами по расстоянию до луча
// с учётом глубины (берём ближайший по параметру t вдоль луча).
#include "../mp/model.hpp"

#include <cmath>

namespace mpui {

struct Mat4 {
  float m[16];
};

Mat4 matIdentity();
Mat4 matMul(const Mat4& a, const Mat4& b);
Mat4 matPerspective(float fovyDeg, float aspect, float zn, float zf);
Mat4 matLookAt(const mp::V3& eye, const mp::V3& center, const mp::V3& up);
Mat4 matInvert(const Mat4& a);
mp::V3 matTransformPoint(const Mat4& a, const mp::V3& p);

struct Ray {
  mp::V3 o, d;
};

struct Camera {
  mp::V3 target{0, 0, 0};
  float yaw = 0.0f, pitch = 1.52f, dist = 40.0f;
  float fov = 45.0f;
  mp::V3 flyTarget{};
  float flyDist = 0;
  bool flying = false;

  mp::V3 eye() const;
  mp::V3 right() const;
  mp::V3 upv() const;
  Mat4 proj(int w, int h) const;
  Mat4 view() const;
  void orbit(float dx, float dy);
  void pan(float dx, float dy);
  void zoom(float f);
  void flyTo(const mp::V3& p, float d = 0);
  void tickFly(float dt);

  Ray pickRay(float mx, float my, int w, int h) const;
  mp::V3 screenPoint(const mp::V3& p, int w, int h, bool* ok = nullptr) const; // 3D->2D (билборды)
  mp::V3 rayPlane(const Ray& r, float planeY) const; // пересечение с y = planeY
};

bool rayPoint(const Ray& r, const mp::V3& p, float& dist, float* tOut = nullptr);
bool raySegment(const Ray& r, const mp::V3& a, const mp::V3& b, float& dist, float* tOut = nullptr);

} // namespace mpui
