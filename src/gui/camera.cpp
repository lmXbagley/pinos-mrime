#include "camera.hpp"

#include <algorithm>

namespace mpui {


Mat4 matIdentity() {
  Mat4 r;
  for (int i = 0; i < 16; ++i) r.m[i] = 0;
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
  return r;
}

Mat4 matMul(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int rr = 0; rr < 4; ++rr) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
      r.m[c * 4 + rr] = s;
    }
  return r;
}

Mat4 matPerspective(float fovyDeg, float aspect, float zn, float zf) {
  Mat4 r = matIdentity();
  float f = 1.0f / std::tan(fovyDeg * 3.1415926f / 360.0f);
  r.m[0] = f / aspect;
  r.m[5] = f;
  r.m[10] = (zf + zn) / (zn - zf);
  r.m[11] = -1;
  r.m[14] = 2 * zf * zn / (zn - zf);
  r.m[15] = 0;
  return r;
}

Mat4 matLookAt(const mp::V3& eye, const mp::V3& center, const mp::V3& up) {
  mp::V3 f{center.x - eye.x, center.y - eye.y, center.z - eye.z};
  float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
  if (fl > 0) {
    f.x /= fl;
    f.y /= fl;
    f.z /= fl;
  }
  mp::V3 s{f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x};
  float sl = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
  if (sl > 0) {
    s.x /= sl;
    s.y /= sl;
    s.z /= sl;
  }
  mp::V3 u{s.y * f.z - s.z * f.y, s.z * f.x - s.x * f.z, s.x * f.y - s.y * f.x};
  Mat4 r;
  r.m[0] = s.x;
  r.m[4] = s.y;
  r.m[8] = s.z;
  r.m[1] = u.x;
  r.m[5] = u.y;
  r.m[9] = u.z;
  r.m[2] = -f.x;
  r.m[6] = -f.y;
  r.m[10] = -f.z;
  r.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
  r.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
  r.m[14] = f.x * eye.x + f.y * eye.y + f.z * eye.z;
  return r;
}

Mat4 matInvert(const Mat4& a) {
  float inv[16], m[16];
  for (int i = 0; i < 16; ++i) m[i] = a.m[i];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  Mat4 r = matIdentity();
  if (std::fabs(det) < 1e-12f) return r;
  det = 1.0f / det;
  for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * det;
  return r;
}

mp::V3 matTransformPoint(const Mat4& a, const mp::V3& p) {
  mp::V3 r;
  float w = a.m[3] * p.x + a.m[7] * p.y + a.m[11] * p.z + a.m[15];
  r.x = a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12];
  r.y = a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13];
  r.z = a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14];
  if (std::fabs(w) > 1e-12f) {
    r.x /= w;
    r.y /= w;
    r.z /= w;
  }
  return r;
}

// ---------- Camera ----------
mp::V3 Camera::eye() const {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cp = std::cos(pitch), sp = std::sin(pitch);
  return {target.x + dist * cp * sy, target.y + dist * sp, target.z + dist * cp * cy};
}
mp::V3 Camera::right() const { return {std::cos(yaw), 0, -std::sin(yaw)}; }
mp::V3 Camera::upv() const {
  mp::V3 f{target.x - eye().x, target.y - eye().y, target.z - eye().z};
  float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
  if (fl > 0) {
    f.x /= fl;
    f.y /= fl;
    f.z /= fl;
  }
  mp::V3 r = right();
  mp::V3 u{f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x};
  return u;
}

Mat4 Camera::proj(int w, int h) const {
  return matPerspective(fov, h > 0 ? (float)w / h : 1.0f, 0.05f, 4000.0f);
}

Mat4 Camera::view() const {
  mp::V3 u{0, 1, 0};
  return matLookAt(eye(), target, u);
}

void Camera::orbit(float dx, float dy) {
  yaw -= dx * 0.008f;
  pitch += dy * 0.008f;
  pitch = std::max(-1.55f, std::min(1.55f, pitch));
}

void Camera::pan(float dx, float dy) {
  mp::V3 r = right();
  mp::V3 u = upv();
  float k = dist * 0.0016f;
  target.x -= (dx * r.x - dy * u.x) * k;
  target.y -= (dx * r.y - dy * u.y) * k;
  target.z -= (dx * r.z - dy * u.z) * k;
  flying = false;
}

void Camera::zoom(float f) {
  dist *= std::pow(0.9f, f);
  dist = std::max(2.0f, std::min(1500.0f, dist));
}

void Camera::flyTo(const mp::V3& p, float d) {
  flying = true;
  flyTarget = p;
  flyDist = d > 0 ? d : std::max(6.0f, dist * 0.5f);
}

void Camera::tickFly(float dt) {
  if (!flying) return;
  float k = 1.0f - std::pow(0.0025f, dt);
  mp::V3 dv{flyTarget.x - target.x, flyTarget.y - target.y, flyTarget.z - target.z};
  target.x += dv.x * k;
  target.y += dv.y * k;
  target.z += dv.z * k;
  dist += (flyDist - dist) * k;
  if (std::fabs(dv.x) + std::fabs(dv.y) + std::fabs(dv.z) < 0.02f && std::fabs(flyDist - dist) < 0.05f) flying = false;
}

Ray Camera::pickRay(float mx, float my, int w, int h) const {
  Mat4 pv = matMul(proj(w, h), view());
  Mat4 inv = matInvert(pv);
  float nx = mx / std::max(1, w) * 2.0f - 1.0f;
  float ny = 1.0f - my / std::max(1, h) * 2.0f;
  mp::V3 p0 = matTransformPoint(inv, {nx, ny, -1.0f});
  mp::V3 p1 = matTransformPoint(inv, {nx, ny, 1.0f});
  mp::V3 d{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
  float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  if (l > 0) {
    d.x /= l;
    d.y /= l;
    d.z /= l;
  }
  return {p0, d};
}

mp::V3 Camera::screenPoint(const mp::V3& p, int w, int h, bool* ok) const {
  Mat4 pv = matMul(proj(w, h), view());
  mp::V3 c = matTransformPoint(pv, p);
  if (ok) {
    float wclip = pv.m[3] * p.x + pv.m[7] * p.y + pv.m[11] * p.z + pv.m[15];
    *ok = wclip > 0;
  }
  return {(c.x * 0.5f + 0.5f) * w, (0.5f - c.y * 0.5f) * h, c.z};
}

mp::V3 Camera::rayPlane(const Ray& r, float planeY) const {
  if (std::fabs(r.d.y) < 1e-8f) return target;
  float t = (planeY - r.o.y) / r.d.y;
  if (t < 0) return target;
  return {r.o.x + r.d.x * t, planeY, r.o.z + r.d.z * t};
}

bool rayPoint(const Ray& r, const mp::V3& p, float& dist, float* tOut) {
  mp::V3 v{p.x - r.o.x, p.y - r.o.y, p.z - r.o.z};
  float t = v.x * r.d.x + v.y * r.d.y + v.z * r.d.z;
  if (t < 0) {
    dist = 1e30f;
    return false;
  }
  mp::V3 q{v.x - r.d.x * t, v.y - r.d.y * t, v.z - r.d.z * t};
  dist = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
  if (tOut) *tOut = t;
  return true;
}

bool raySegment(const Ray& r, const mp::V3& a, const mp::V3& b, float& dist, float* tOut) {
  // минимум по семплам вдоль отрезка (сегмент проекции в пиксели не нужен:
  // порог подбирается в мировых единицах от дистанции камеры)
  float minD = 1e30f, minT = 0;
  const int N = 24;
  for (int i = 0; i <= N; ++i) {
    float f = i / (float)N;
    mp::V3 p{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
    float t, dd;
    if (!rayPoint(r, p, dd, &t)) continue;
    if (dd < minD) {
      minD = dd;
      minT = t;
    }
  }
  if (minD >= 1e30f) return false;
  dist = minD;
  if (tOut) *tOut = minT;
  return true;
}

} // namespace mpui
