#pragma once

#include <stdint.h>

// Reusable 2D camera. Zoom is Q16.16: kCamOne means 1 world pixel = 1 screen pixel.
constexpr int32_t kCamOne = 65536;

struct Camera {
  int x;
  int y;
  int32_t zoom;
  int shakeX;
  int shakeY;
  int screenW;
  int screenH;
};

void camera_init(Camera *cam, int screenW, int screenH);
void camera_snap(Camera *cam, int x, int y, int32_t zoom);
void camera_set_shake(Camera *cam, int shakeX, int shakeY);

// Move toward a target by (target - cur) / div. Larger div = slower.
void camera_ease(Camera *cam, int x, int y, int32_t zoom, int div);

// True when the camera is within posTol world px and zoomTol of the target.
bool camera_near(const Camera *cam, int x, int y, int32_t zoom, int posTol, int32_t zoomTol);

void camera_world_to_screen(const Camera *cam, int wx, int wy, int *sx, int *sy);

// World length to screen pixels. Returns at least 1 when worldLen > 0.
int camera_scale(const Camera *cam, int worldLen);

int32_t camera_zoom(const Camera *cam);
