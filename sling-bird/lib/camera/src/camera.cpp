#include "camera.h"

void camera_init(Camera *cam, int screenW, int screenH) {
  cam->x = 0;
  cam->y = 0;
  cam->zoom = kCamOne;
  cam->shakeX = 0;
  cam->shakeY = 0;
  cam->screenW = screenW;
  cam->screenH = screenH;
}

void camera_snap(Camera *cam, int x, int y, int32_t zoom) {
  cam->x = x;
  cam->y = y;
  cam->zoom = zoom > 0 ? zoom : kCamOne;
}

void camera_set_shake(Camera *cam, int shakeX, int shakeY) {
  cam->shakeX = shakeX;
  cam->shakeY = shakeY;
}

void camera_ease(Camera *cam, int x, int y, int32_t zoom, int div) {
  if (div < 1) {
    div = 1;
  }
  const int dx = x - cam->x;
  const int dy = y - cam->y;
  if (dx != 0) {
    int step = dx / div;
    if (step == 0) {
      step = dx > 0 ? 1 : -1;
    }
    cam->x += step;
  }
  if (dy != 0) {
    int step = dy / div;
    if (step == 0) {
      step = dy > 0 ? 1 : -1;
    }
    cam->y += step;
  }
  if (zoom > 0) {
    const int32_t dz = zoom - cam->zoom;
    if (dz != 0) {
      int32_t step = dz / div;
      if (step == 0) {
        step = dz > 0 ? 1 : -1;
      }
      cam->zoom += step;
      if (cam->zoom < 1) {
        cam->zoom = 1;
      }
    }
  }
}

bool camera_near(const Camera *cam, int x, int y, int32_t zoom, int posTol, int32_t zoomTol) {
  const int dx = cam->x - x;
  const int dy = cam->y - y;
  const int32_t dz = cam->zoom - zoom;
  return dx > -posTol && dx < posTol && dy > -posTol && dy < posTol && dz > -zoomTol && dz < zoomTol;
}

void camera_world_to_screen(const Camera *cam, int wx, int wy, int *sx, int *sy) {
  const int32_t zoom = cam->zoom > 0 ? cam->zoom : kCamOne;
  *sx = static_cast<int>((static_cast<int64_t>(wx - cam->x) * zoom) / kCamOne) + cam->screenW / 2 +
        cam->shakeX;
  *sy = static_cast<int>((static_cast<int64_t>(wy - cam->y) * zoom) / kCamOne) + cam->screenH / 2 +
        cam->shakeY;
}

int camera_scale(const Camera *cam, int worldLen) {
  if (worldLen <= 0) {
    return 0;
  }
  const int32_t zoom = cam->zoom > 0 ? cam->zoom : kCamOne;
  int out = static_cast<int>((static_cast<int64_t>(worldLen) * zoom + kCamOne / 2) / kCamOne);
  if (out < 1) {
    out = 1;
  }
  return out;
}

int32_t camera_zoom(const Camera *cam) {
  return cam->zoom > 0 ? cam->zoom : kCamOne;
}
