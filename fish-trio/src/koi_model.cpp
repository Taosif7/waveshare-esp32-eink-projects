#include "koi_model.h"

#include "pond_world.h"

#include <math.h>

namespace pond {
namespace {

constexpr float kHalfU[] = {0.00f, 0.04f, 0.10f, 0.16f, 0.24f, 0.34f, 0.46f, 0.58f, 0.68f, 0.76f, 0.82f};
constexpr float kHalfW[] = {0.18f, 0.90f, 1.55f, 2.15f, 2.55f, 2.70f, 2.42f, 1.85f, 1.20f, 0.70f, 0.48f};

struct Patch {
  float u0;
  float vn0;
  float ru;
  float rvn;
  float rot;
};

constexpr Patch kPatches[] = {
    {0.16f, 0.12f, 0.115f, 0.90f, 0.28f},
    {0.30f, -0.08f, 0.145f, 1.10f, -0.20f},
    {0.48f, 0.22f, 0.125f, 0.95f, 0.35f},
    {0.63f, -0.28f, 0.085f, 0.68f, -0.15f},
};

struct Fleck {
  float u;
  float vn;
  float r;
};

constexpr Fleck kFlecks[] = {
    {0.22f, -0.55f, 0.028f},
    {0.55f, 0.48f, 0.022f},
    {0.41f, -0.62f, 0.016f},
};

float clampf(float value, float lo, float hi) {
  if (value < lo) {
    return lo;
  }
  if (value > hi) {
    return hi;
  }
  return value;
}

bool in_body(float u, float v_mm) {
  if (u < 0.f || u > 0.82f) {
    return false;
  }
  return fabsf(v_mm) <= koi_half_width(u);
}

bool body_near(float u, float v_mm) {
  if (u < 0.f || u > 0.82f) {
    return false;
  }
  const float half = koi_half_width(u);
  const float av = fabsf(v_mm);
  return av > half && av <= half + kOutlineMm;
}

bool pectoral_hit(float u, float v_mm, float phase, float extra_mm, bool *tip) {
  const float sweep = 0.075f * sinf(phase * 2.f);
  const float v_center = 3.575f + sweep;
  const float ru = 0.078f + extra_mm / kKoiLengthMm;
  const float rv = 1.25f + extra_mm;
  const float du = (u - 0.28f) / ru;
  const float dv = (fabsf(v_mm) - v_center) / rv;
  if (du * du + dv * dv > 1.f) {
    return false;
  }
  if (tip != nullptr) {
    *tip = fabsf(v_mm) > v_center + 0.35f;
  }
  return true;
}

bool pelvic_hit(float u, float v_mm, float phase, float extra_mm, bool *tip) {
  const float sweep = 0.06f * sinf(phase * 2.f + 1.2f);
  const float v_center = 2.15f + sweep;
  const float ru = 0.055f + extra_mm / kKoiLengthMm;
  const float rv = 0.62f + extra_mm;
  const float du = (u - 0.55f) / ru;
  const float dv = (fabsf(v_mm) - v_center) / rv;
  if (du * du + dv * dv > 1.f) {
    return false;
  }
  if (tip != nullptr) {
    *tip = fabsf(v_mm) > v_center + 0.18f;
  }
  return true;
}

bool tail_hit(float u, float v_mm, float phase, float extra_mm, bool *tip) {
  if (u < 0.76f || u > 1.03f) {
    return false;
  }
  float t = (u - 0.76f) / 0.27f;
  if (t > 1.f) {
    t = 1.f;
  }
  const float flutter = 0.16f * sinf(phase);
  const float spread = 0.22f + t * (1.20f + flutter);
  const float rad = 0.36f + 0.62f * sinf(t * kPi) + extra_mm;
  if (fabsf(fabsf(v_mm) - spread) > rad) {
    return false;
  }
  if (tip != nullptr) {
    *tip = t > 0.75f;
  }
  return true;
}

bool eye_hit(float u, float v_mm) {
  const float half = koi_half_width(0.08f);
  const float v_eye = 0.46f * half;
  const float du = (u - 0.08f) * kKoiLengthMm;
  const float dv = fabsf(v_mm) - v_eye;
  const float r = 0.22f;
  return du * du + dv * dv <= r * r;
}

bool fleck_hit(float u, float v_mm) {
  const float half = koi_half_width(u);
  if (half < 0.05f) {
    return false;
  }
  const float vn = v_mm / half;
  for (const Fleck &fleck : kFlecks) {
    const float du = (u - fleck.u) * kKoiLengthMm;
    const float dv = (vn - fleck.vn) * half;
    const float r = fleck.r * kKoiLengthMm;
    if (du * du + dv * dv <= r * r) {
      return true;
    }
  }
  return false;
}

bool calico_hit(float u, float v_mm, KoiMat *out) {
  const float half = koi_half_width(u);
  if (half < 0.05f) {
    return false;
  }
  const float vn = v_mm / half;
  for (const Patch &patch : kPatches) {
    const float du = u - patch.u0;
    const float dv = vn - patch.vn0;
    const float c = cosf(patch.rot);
    const float s = sinf(patch.rot);
    const float lu = c * du + s * dv;
    const float lv = -s * du + c * dv;
    const float n = (lu * lu) / (patch.ru * patch.ru) + (lv * lv) / (patch.rvn * patch.rvn);
    if (n <= 1.f) {
      if (lv > 0.25f * patch.rvn) {
        *out = KoiMat::CalicoHi;
      } else if (lv < -0.25f * patch.rvn) {
        *out = KoiMat::CalicoShade;
      } else {
        *out = KoiMat::Calico;
      }
      return true;
    }
  }
  return false;
}

}  // namespace

float koi_half_width(float u) {
  constexpr int kCount = static_cast<int>(sizeof(kHalfU) / sizeof(kHalfU[0]));
  if (u < kHalfU[0] || u > kHalfU[kCount - 1]) {
    return 0.f;
  }
  for (int i = 0; i < kCount - 1; ++i) {
    if (u <= kHalfU[i + 1]) {
      const float span = kHalfU[i + 1] - kHalfU[i];
      const float t = (u - kHalfU[i]) / span;
      return kHalfW[i] + t * (kHalfW[i + 1] - kHalfW[i]);
    }
  }
  return kHalfW[kCount - 1];
}

float koi_wave_amp_mm(float u) {
  const float uu = clampf(u, 0.f, 1.f);
  return kWaveTailAmpMm * powf(uu, kWaveExp);
}

float koi_spine_lat(float u, float phase) {
  const float uu = clampf(u, 0.f, 1.f);
  const float wave = koi_wave_amp_mm(uu) * sinf(phase - uu * kWaveCycles * 2.f * kPi);
  const float weave = kWeaveMm * sinf(phase * 0.31f);
  return wave + weave;
}

float koi_pectoral_span_mm(float phase) {
  const float sweep = 0.075f * sinf(phase * 2.f);
  return 2.f * (4.825f + sweep);
}

KoiMat koi_material_uv(float u, float v_mm, float phase) {
  if (u < -0.03f || u > 1.06f || fabsf(v_mm) > 6.6f) {
    return KoiMat::None;
  }
  if (in_body(u, v_mm)) {
    if (eye_hit(u, v_mm)) {
      return KoiMat::Eye;
    }
    if (fleck_hit(u, v_mm)) {
      return KoiMat::Fleck;
    }
    KoiMat calico = KoiMat::None;
    if (calico_hit(u, v_mm, &calico)) {
      return calico;
    }
    const float half = koi_half_width(u);
    if (v_mm < -0.08f * half) {
      return KoiMat::BodyShade;
    }
    return KoiMat::Body;
  }

  bool tip = false;
  if (pectoral_hit(u, v_mm, phase, 0.f, &tip)) {
    return tip ? KoiMat::FinTip : KoiMat::Fin;
  }
  if (pelvic_hit(u, v_mm, phase, 0.f, &tip)) {
    return tip ? KoiMat::FinTip : KoiMat::Fin;
  }
  if (tail_hit(u, v_mm, phase, 0.f, &tip)) {
    return tip ? KoiMat::FinTip : KoiMat::Fin;
  }
  if (body_near(u, v_mm) || pectoral_hit(u, v_mm, phase, kOutlineMm, nullptr) ||
      pelvic_hit(u, v_mm, phase, kOutlineMm, nullptr) || tail_hit(u, v_mm, phase, kOutlineMm, nullptr)) {
    return KoiMat::Outline;
  }
  return KoiMat::None;
}

KoiMat koi_material_world(const KoiPose &pose, float x_mm, float y_mm) {
  const float u = (x_mm - pose.x_nose_mm) / kKoiLengthMm;
  if (u < -0.05f || u > 1.08f) {
    return KoiMat::None;
  }
  const float spine = pose.y_mm + koi_spine_lat(u, pose.phase);
  const float v = y_mm - spine;
  if (fabsf(v) > 6.6f) {
    return KoiMat::None;
  }
  return koi_material_uv(u, v, pose.phase);
}

KoiPose koi_pose_from(float x_nose_mm, float phase) {
  KoiPose pose;
  pose.x_nose_mm = x_nose_mm;
  pose.y_mm = pond_bob_mm(phase);
  pose.phase = phase;
  return pose;
}

KoiPose koi_shadow_pose(const KoiPose &pose) {
  KoiPose shadow = pose;
  shadow.x_nose_mm += kShadowDxMm;
  shadow.y_mm += kShadowDyMm;
  return shadow;
}

void koi_tail_point(const KoiPose &pose, float *x_mm, float *y_mm) {
  constexpr float kTailU = 0.97f;
  *x_mm = pose.x_nose_mm + kTailU * kKoiLengthMm;
  *y_mm = pose.y_mm + koi_spine_lat(kTailU, pose.phase);
}

}  // namespace pond
