#pragma once

#include "pond_config.h"

namespace pond {

enum class KoiMat : uint8_t {
  None = 0,
  Outline,
  Body,
  BodyShade,
  Calico,
  CalicoHi,
  CalicoShade,
  Eye,
  Fleck,
  Fin,
  FinTip,
};

struct KoiPose {
  float x_nose_mm;
  float y_mm;
  float phase;
};

float koi_half_width(float u);
float koi_wave_amp_mm(float u);
float koi_spine_lat(float u, float phase);
float koi_pectoral_span_mm(float phase);

KoiMat koi_material_uv(float u, float v_mm, float phase);
KoiMat koi_material_world(const KoiPose &pose, float x_mm, float y_mm);

KoiPose koi_pose_from(float x_nose_mm, float phase);
KoiPose koi_shadow_pose(const KoiPose &pose);
void koi_tail_point(const KoiPose &pose, float *x_mm, float *y_mm);

}  // namespace pond
