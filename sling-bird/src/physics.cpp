#include "physics.h"

#include <string.h>

namespace {

constexpr int kMaxContacts = 24;
constexpr int32_t kMaxSpeed = 280 * kOne;
constexpr int32_t kMaxSpin = 2 * kOne;

struct Axis {
  int32_t x;
  int32_t y;
};

struct Contact {
  int a;
  int b;
  int32_t nx;
  int32_t ny;
  int32_t pen;
  int32_t px;
  int32_t py;
};

uint32_t isqrt64(uint64_t x) {
  uint64_t op = x;
  uint64_t res = 0;
  uint64_t one = 1ULL << 62;
  while (one > op) {
    one >>= 2;
  }
  while (one != 0) {
    if (op >= res + one) {
      op -= res + one;
      res = (res >> 1) + one;
    } else {
      res >>= 1;
    }
    one >>= 2;
  }
  return static_cast<uint32_t>(res);
}

int32_t len2(int32_t x, int32_t y) {
  const uint64_t d2 = static_cast<uint64_t>(static_cast<int64_t>(x) * x) +
                      static_cast<uint64_t>(static_cast<int64_t>(y) * y);
  return static_cast<int32_t>(isqrt64(d2));
}

int32_t dot(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  return fxMul(ax, bx) + fxMul(ay, by);
}

void box_axes(const Body &b, Axis *u, Axis *v) {
  const int32_t c = fxCos(b.ang);
  const int32_t s = fxSin(b.ang);
  u->x = c;
  u->y = -s;
  v->x = s;
  v->y = c;
}

int mass_of(const Body &b) {
  const int area = b.shape == Shape::Circle ? b.hw * b.hw * 3 : (b.hw * 2) * (b.hh * 2);
  int dens = 3;
  if (b.mat == Mat::Stone) {
    dens = 8;
  } else if (b.mat == Mat::Glass) {
    dens = 1;
  } else if (b.mat == Mat::Bird) {
    dens = 2;
  }
  int mass = area * dens / 16;
  if (mass < 1) {
    mass = 1;
  }
  if (mass > 48) {
    mass = 48;
  }
  return mass;
}

void inertia(Body *b) {
  const int mass = mass_of(*b);
  b->invMass = kOne / mass;
  int r2 = b->hw * b->hw + b->hh * b->hh;
  if (b->shape == Shape::Circle) {
    r2 = b->hw * b->hw;
  }
  if (r2 < 1) {
    r2 = 1;
  }
  const int factor = b->shape == Shape::Circle ? 2 : 3;
  const int64_t num = static_cast<int64_t>(factor) * kOne * kOne;
  int32_t inv = static_cast<int32_t>(num / (mass * r2));
  if (inv > (1 << 28)) {
    inv = 1 << 28;
  }
  b->invI = inv;
}

int32_t mat_bounce(Mat mat) {
  if (mat == Mat::Stone) {
    return 2500;
  }
  if (mat == Mat::Wood) {
    return 7000;
  }
  if (mat == Mat::Glass) {
    return 3500;
  }
  if (mat == Mat::Pig) {
    return 9000;
  }
  return 14000;
}

int32_t mat_friction(Mat mat) {
  if (mat == Mat::Stone) {
    return 50000;
  }
  if (mat == Mat::Glass) {
    return 12000;
  }
  if (mat == Mat::Bird) {
    return 18000;
  }
  if (mat == Mat::Pig) {
    return 28000;
  }
  return 36000;
}

void note_impact(Body *body, int approach, bool fromBody) {
  if (approach > body->impact) {
    body->impact = approach;
  }
  if (fromBody && approach > 30) {
    body->struck = true;
  }
}

void apply_impulse(Body *b, int32_t jx, int32_t jy, int32_t px, int32_t py) {
  if (!b->alive || b->invMass == 0) {
    return;
  }
  b->vx += fxMul(jx, b->invMass);
  b->vy += fxMul(jy, b->invMass);
  const int rx = static_cast<int>((px - b->x) >> 16);
  const int ry = static_cast<int>((py - b->y) >> 16);
  const int32_t tau = static_cast<int32_t>(((static_cast<int64_t>(rx) * jy - static_cast<int64_t>(ry) * jx) >> 16));
  const int32_t rad = fxMul(tau, b->invI);
  b->angVel += fxMul(rad, kInvTwoPi);
}

int32_t support_bottom(const Body &b) {
  if (b.shape == Shape::Circle) {
    return b.y + (static_cast<int32_t>(b.hw) << 16);
  }
  Axis u;
  Axis v;
  box_axes(b, &u, &v);
  return b.y + fxMul(static_cast<int32_t>(b.hw) << 16, fxAbs(u.y)) +
         fxMul(static_cast<int32_t>(b.hh) << 16, fxAbs(v.y));
}

bool add_contact(Contact *contacts, int *count, const Contact &c) {
  if (*count >= kMaxContacts) {
    return false;
  }
  contacts[*count] = c;
  *count += 1;
  return true;
}

void contact_circle(World *world, Contact *contacts, int *count, int ia, int ib) {
  Body &a = world->bodies[ia];
  Body &b = world->bodies[ib];
  const int32_t dx = b.x - a.x;
  const int32_t dy = b.y - a.y;
  const int32_t dist = len2(dx, dy);
  const int32_t rad = (static_cast<int32_t>(a.hw) + b.hw) << 16;
  if (dist >= rad && dist > 0) {
    return;
  }
  Contact c;
  c.a = ia;
  c.b = ib;
  c.pen = rad - dist;
  if (dist < 256) {
    c.nx = -kOne;
    c.ny = 0;
  } else {
    c.nx = -fxDiv(dx, dist);
    c.ny = -fxDiv(dy, dist);
  }
  c.px = a.x - fxMul(c.nx, static_cast<int32_t>(a.hw) << 16);
  c.py = a.y - fxMul(c.ny, static_cast<int32_t>(a.hw) << 16);
  const int32_t vn = dot(a.vx - b.vx, a.vy - b.vy, c.nx, c.ny);
  if (vn < 0) {
    const int approach = static_cast<int>((-vn) >> 16);
    note_impact(&a, approach, true);
    note_impact(&b, approach, true);
  }
  a.supported = true;
  b.supported = true;
  add_contact(contacts, count, c);
}

void contact_circle_box(World *world, Contact *contacts, int *count, int ic, int ibox) {
  Body &circle = world->bodies[ic];
  Body &box = world->bodies[ibox];
  Axis u;
  Axis v;
  box_axes(box, &u, &v);
  const int32_t dx = circle.x - box.x;
  const int32_t dy = circle.y - box.y;
  const int32_t lx = dot(dx, dy, u.x, u.y);
  const int32_t ly = dot(dx, dy, v.x, v.y);
  const int32_t hx = static_cast<int32_t>(box.hw) << 16;
  const int32_t hy = static_cast<int32_t>(box.hh) << 16;
  int32_t clx = lx;
  int32_t cly = ly;
  if (clx > hx) {
    clx = hx;
  } else if (clx < -hx) {
    clx = -hx;
  }
  if (cly > hy) {
    cly = hy;
  } else if (cly < -hy) {
    cly = -hy;
  }

  Contact c;
  c.a = ic;
  c.b = ibox;
  if (lx == clx && ly == cly) {
    const int32_t px = hx - fxAbs(lx);
    const int32_t py = hy - fxAbs(ly);
    if (px < py) {
      c.nx = lx > 0 ? u.x : -u.x;
      c.ny = lx > 0 ? u.y : -u.y;
      c.pen = px + (static_cast<int32_t>(circle.hw) << 16);
    } else {
      c.nx = ly > 0 ? v.x : -v.x;
      c.ny = ly > 0 ? v.y : -v.y;
      c.pen = py + (static_cast<int32_t>(circle.hw) << 16);
    }
  } else {
    const int32_t ox = lx - clx;
    const int32_t oy = ly - cly;
    const int32_t wx = fxMul(u.x, ox) + fxMul(v.x, oy);
    const int32_t wy = fxMul(u.y, ox) + fxMul(v.y, oy);
    const int32_t dist = len2(wx, wy);
    const int32_t rad = static_cast<int32_t>(circle.hw) << 16;
    if (dist >= rad) {
      return;
    }
    if (dist < 256) {
      c.nx = u.x;
      c.ny = u.y;
    } else {
      c.nx = fxDiv(wx, dist);
      c.ny = fxDiv(wy, dist);
    }
    c.pen = rad - dist;
  }
  c.px = circle.x - fxMul(c.nx, static_cast<int32_t>(circle.hw) << 16);
  c.py = circle.y - fxMul(c.ny, static_cast<int32_t>(circle.hw) << 16);
  const int32_t vn = dot(circle.vx - box.vx, circle.vy - box.vy, c.nx, c.ny);
  if (vn < 0) {
    const int approach = static_cast<int>((-vn) >> 16);
    note_impact(&circle, approach, true);
    note_impact(&box, approach, true);
  }
  circle.supported = true;
  box.supported = true;
  add_contact(contacts, count, c);
}

int32_t proj_box(const Body &b, const Axis &u, const Axis &v, int32_t nx, int32_t ny) {
  const int32_t ru = fxAbs(dot(u.x, u.y, nx, ny));
  const int32_t rv = fxAbs(dot(v.x, v.y, nx, ny));
  return fxMul(static_cast<int32_t>(b.hw) << 16, ru) + fxMul(static_cast<int32_t>(b.hh) << 16, rv);
}

void try_axis(const Body &a, const Body &b, const Axis &ua, const Axis &va, const Axis &ub,
              const Axis &vb, int32_t ax, int32_t ay, int32_t *bestPen, int32_t *bnx, int32_t *bny) {
  const int32_t dist = dot(b.x - a.x, b.y - a.y, ax, ay);
  int32_t nx = ax;
  int32_t ny = ay;
  int32_t sep = dist;
  if (sep < 0) {
    nx = -ax;
    ny = -ay;
    sep = -sep;
  }
  const int32_t span = proj_box(a, ua, va, nx, ny) + proj_box(b, ub, vb, nx, ny);
  const int32_t pen = span - sep;
  if (pen < 0) {
    *bestPen = -1;
    return;
  }
  if (pen < *bestPen) {
    *bestPen = pen;
    *bnx = -nx;
    *bny = -ny;
  }
}

void contact_boxes(World *world, Contact *contacts, int *count, int ia, int ib) {
  Body &a = world->bodies[ia];
  Body &b = world->bodies[ib];
  Axis ua;
  Axis va;
  Axis ub;
  Axis vb;
  box_axes(a, &ua, &va);
  box_axes(b, &ub, &vb);
  int32_t best = 0x7FFFFFFF;
  int32_t nx = kOne;
  int32_t ny = 0;
  try_axis(a, b, ua, va, ub, vb, ua.x, ua.y, &best, &nx, &ny);
  if (best < 0) {
    return;
  }
  try_axis(a, b, ua, va, ub, vb, va.x, va.y, &best, &nx, &ny);
  if (best < 0) {
    return;
  }
  try_axis(a, b, ua, va, ub, vb, ub.x, ub.y, &best, &nx, &ny);
  if (best < 0) {
    return;
  }
  try_axis(a, b, ua, va, ub, vb, vb.x, vb.y, &best, &nx, &ny);
  if (best < 0) {
    return;
  }
  Contact c;
  c.a = ia;
  c.b = ib;
  c.nx = nx;
  c.ny = ny;
  c.pen = best;
  c.px = (a.x + b.x) >> 1;
  c.py = (a.y + b.y) >> 1;
  const int32_t vn = dot(a.vx - b.vx, a.vy - b.vy, c.nx, c.ny);
  if (vn < 0) {
    const int approach = static_cast<int>((-vn) >> 16);
    note_impact(&a, approach, true);
    note_impact(&b, approach, true);
  }
  a.supported = true;
  b.supported = true;
  add_contact(contacts, count, c);
}

void contact_ground(World *world, Contact *contacts, int *count, int i) {
  Body &b = world->bodies[i];
  const int32_t bottom = support_bottom(b);
  const int32_t pen = bottom - world->ground;
  if (pen <= 0) {
    return;
  }
  Contact c;
  c.a = i;
  c.b = -1;
  c.nx = 0;
  c.ny = -kOne;
  c.pen = pen;
  c.px = b.x;
  c.py = world->ground;
  const int32_t vn = dot(b.vx, b.vy, c.nx, c.ny);
  if (vn < 0) {
    note_impact(&b, static_cast<int>((-vn) >> 16), false);
  }
  b.supported = true;
  add_contact(contacts, count, c);
}

void resolve(World *world, Contact *c, bool correct) {
  Body &a = world->bodies[c->a];
  Body *b = c->b >= 0 ? &world->bodies[c->b] : nullptr;
  const int32_t invB = b != nullptr ? b->invMass : 0;
  const int32_t invSum = a.invMass + invB;
  if (invSum == 0) {
    return;
  }
  if (correct && c->pen > kOne) {
    int32_t fix = c->pen - kOne;
    if (fix > (6 << 16)) {
      fix = 6 << 16;
    }
    const int32_t corr = fxMul(fix, 42000);
    const int32_t shareA = fxDiv(a.invMass, invSum);
    const int32_t corrA = fxMul(corr, shareA);
    a.x += fxMul(c->nx, corrA);
    a.y += fxMul(c->ny, corrA);
    if (b != nullptr) {
      const int32_t corrB = fxMul(corr, fxDiv(invB, invSum));
      b->x -= fxMul(c->nx, corrB);
      b->y -= fxMul(c->ny, corrB);
    }
  }

  const int32_t bvx = b != nullptr ? b->vx : 0;
  const int32_t bvy = b != nullptr ? b->vy : 0;
  const int32_t rvx = a.vx - bvx;
  const int32_t rvy = a.vy - bvy;
  const int32_t vn = dot(rvx, rvy, c->nx, c->ny);
  const int32_t e = b != nullptr ? (mat_bounce(a.mat) < mat_bounce(b->mat) ? mat_bounce(a.mat) : mat_bounce(b->mat))
                                 : 8000;
  int32_t j = 0;
  if (vn < 0) {
    j = fxDiv(fxMul(-(kOne + e), vn), invSum);
    const int32_t jx = fxMul(j, c->nx);
    const int32_t jy = fxMul(j, c->ny);
    apply_impulse(&a, jx, jy, c->px, c->py);
    if (b != nullptr) {
      apply_impulse(b, -jx, -jy, c->px, c->py);
    }
  }

  const int32_t tx = -c->ny;
  const int32_t ty = c->nx;
  const int32_t avx = a.vx - (b != nullptr ? b->vx : 0);
  const int32_t avy = a.vy - (b != nullptr ? b->vy : 0);
  const int32_t vt = dot(avx, avy, tx, ty);
  int32_t jt = fxDiv(-vt, invSum);
  const int32_t fa = mat_friction(a.mat);
  const int32_t fb = b != nullptr ? mat_friction(b->mat) : 46000;
  const int32_t fr = (fa + fb) / 2;
  const int32_t jabs = j < 0 ? -j : j;
  int32_t maxF = fxMul(fr, jabs);
  if (maxF < (8 << 16) && fxAbs(vt) < (12 << 16)) {
    maxF = 8 << 16;
  }
  if (jt > maxF) {
    jt = maxF;
  } else if (jt < -maxF) {
    jt = -maxF;
  }
  const int32_t fx = fxMul(jt, tx);
  const int32_t fy = fxMul(jt, ty);
  apply_impulse(&a, fx, fy, c->px, c->py);
  if (b != nullptr) {
    apply_impulse(b, -fx, -fy, c->px, c->py);
  }
}

void clamp_motion(Body *b) {
  if (b->vx > kMaxSpeed) {
    b->vx = kMaxSpeed;
  } else if (b->vx < -kMaxSpeed) {
    b->vx = -kMaxSpeed;
  }
  if (b->vy > kMaxSpeed) {
    b->vy = kMaxSpeed;
  } else if (b->vy < -kMaxSpeed) {
    b->vy = -kMaxSpeed;
  }
  if (b->angVel > kMaxSpin) {
    b->angVel = kMaxSpin;
  } else if (b->angVel < -kMaxSpin) {
    b->angVel = -kMaxSpin;
  }
  if (fxAbs(b->vx) < (40 << 16) && fxAbs(b->vy) < (40 << 16)) {
    b->vx = fxMul(b->vx, 60000);
    b->vy = fxMul(b->vy, 60000);
    b->angVel = fxMul(b->angVel, 60000);
  }
}

bool circle_hits(const World *world, int self, int32_t x, int32_t y, int radius) {
  const int32_t rad = static_cast<int32_t>(radius) << 16;
  for (int i = 0; i < kMaxBodies; ++i) {
    const Body &o = world->bodies[i];
    if (i == self || !o.alive) {
      continue;
    }
    if (o.shape == Shape::Circle) {
      const int32_t dist = len2(x - o.x, y - o.y);
      if (dist < rad + (static_cast<int32_t>(o.hw) << 16)) {
        return true;
      }
      continue;
    }
    Axis u;
    Axis v;
    box_axes(o, &u, &v);
    const int32_t lx = dot(x - o.x, y - o.y, u.x, u.y);
    const int32_t ly = dot(x - o.x, y - o.y, v.x, v.y);
    const int32_t hx = (static_cast<int32_t>(o.hw) << 16) + rad;
    const int32_t hy = (static_cast<int32_t>(o.hh) << 16) + rad;
    if (fxAbs(lx) <= hx && fxAbs(ly) <= hy) {
      return true;
    }
  }
  return false;
}

void integrate(World *world, Body *b, int index) {
  b->vy += fxMul(kGravity, kDt);
  const int32_t ox = b->x;
  const int32_t oy = b->y;
  b->x += fxMul(b->vx, kDt);
  b->y += fxMul(b->vy, kDt);
  b->ang += fxMul(b->angVel, kDt);
  if (b->ang >= kOne || b->ang < 0) {
    b->ang %= kOne;
    if (b->ang < 0) {
      b->ang += kOne;
    }
  }
  if (b->mat == Mat::Bird) {
    const int32_t mx = ox + ((b->x - ox) >> 1);
    const int32_t my = oy + ((b->y - oy) >> 1);
    if (circle_hits(world, index, mx, my, b->hw)) {
      b->x = mx;
      b->y = my;
    }
  }
  clamp_motion(b);
}

void update_sleep(Body *b) {
  if (b->asleep) {
    return;
  }
  const int32_t sp = len2(b->vx, b->vy);
  const bool slow = sp < (14 << 16) && fxAbs(b->angVel) < (kOne / 8);
  if (b->supported && slow) {
    if (b->quiet < 250) {
      b->quiet++;
    }
  } else {
    b->quiet = 0;
  }
  if (b->quiet > 6) {
    b->asleep = true;
    b->vx = 0;
    b->vy = 0;
    b->angVel = 0;
  }
}

}  // namespace

void world_clear(World *world, int groundY) {
  memset(world, 0, sizeof(*world));
  world->ground = static_cast<int32_t>(groundY) << 16;
}

int world_add(World *world, const Spec &spec) {
  for (int i = 0; i < kMaxBodies; ++i) {
    if (world->bodies[i].alive) {
      continue;
    }
    Body &b = world->bodies[i];
    memset(&b, 0, sizeof(b));
    b.alive = true;
    b.asleep = spec.asleep;
    b.shape = spec.shape;
    b.mat = spec.mat;
    b.look = spec.look;
    b.x = static_cast<int32_t>(spec.x) << 16;
    b.y = static_cast<int32_t>(spec.y) << 16;
    b.ang = spec.ang;
    b.hw = static_cast<int16_t>(spec.hw);
    b.hh = static_cast<int16_t>(spec.hh);
    b.hp = static_cast<uint8_t>(spec.hp);
    inertia(&b);
    return i;
  }
  return -1;
}

void world_wake(World *world) {
  for (int i = 0; i < kMaxBodies; ++i) {
    Body &b = world->bodies[i];
    if (!b.alive) {
      continue;
    }
    b.asleep = false;
    b.quiet = 0;
  }
}

bool world_asleep(const World *world) {
  for (int i = 0; i < kMaxBodies; ++i) {
    const Body &b = world->bodies[i];
    if (b.alive && !b.asleep) {
      return false;
    }
  }
  return true;
}

void world_step(World *world) {
  for (int i = 0; i < kMaxBodies; ++i) {
    world->bodies[i].supported = false;
    world->bodies[i].struck = false;
  }

  for (int i = 0; i < kMaxBodies; ++i) {
    Body &b = world->bodies[i];
    if (!b.alive || b.asleep) {
      continue;
    }
    integrate(world, &b, i);
  }

  Contact contacts[kMaxContacts];
  int count = 0;
  for (int i = 0; i < kMaxBodies; ++i) {
    Body &a = world->bodies[i];
    if (!a.alive) {
      continue;
    }
    if (!a.asleep) {
      contact_ground(world, contacts, &count, i);
    }
    for (int j = i + 1; j < kMaxBodies; ++j) {
      Body &b = world->bodies[j];
      if (!b.alive || (a.asleep && b.asleep)) {
        continue;
      }
      if (a.shape == Shape::Circle && b.shape == Shape::Circle) {
        contact_circle(world, contacts, &count, i, j);
      } else if (a.shape == Shape::Circle && b.shape == Shape::Box) {
        contact_circle_box(world, contacts, &count, i, j);
      } else if (a.shape == Shape::Box && b.shape == Shape::Circle) {
        contact_circle_box(world, contacts, &count, j, i);
      } else {
        contact_boxes(world, contacts, &count, i, j);
      }
    }
  }

  for (int iter = 0; iter < 4; ++iter) {
    for (int n = 0; n < count; ++n) {
      resolve(world, &contacts[n], iter == 0);
    }
  }

  bool hard = false;
  for (int i = 0; i < kMaxBodies; ++i) {
    if (world->bodies[i].alive && world->bodies[i].struck) {
      hard = true;
    }
  }
  if (hard) {
    world_wake(world);
  }

  for (int i = 0; i < kMaxBodies; ++i) {
    Body &b = world->bodies[i];
    if (!b.alive) {
      continue;
    }
    clamp_motion(&b);
    update_sleep(&b);
  }
}
