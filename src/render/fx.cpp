#include "render/fx.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr size_t kMaxShapes = 64;  // rings, beams, projectiles, orbits: a handful at a time

Color lerpColor(Color a, Color b, float t) {
    auto l = [t](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

Color faded(Color c, float k) { return {c.r, c.g, c.b, static_cast<unsigned char>(c.a * std::clamp(k, 0.0f, 1.0f))}; }

template <class T>
void capped(std::vector<T>& v, T item) {
    if (v.size() >= kMaxShapes) v.erase(v.begin());
    v.push_back(std::move(item));
}

}  // namespace

void Fx::clear() {
    particles_.clear();
    rings_.clear();
    beams_.clear();
    projs_.clear();
    orbits_.clear();
    timers_.clear();
}

Vector3 Fx::randomDir() {
    Vector3 d{rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)};
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return len < 1e-3f ? Vector3{0, 1, 0} : Vector3{d.x / len, d.y / len, d.z / len};
}

void Fx::spawn(const Particle& p) {
    if (particles_.size() >= kMaxParticles) particles_.erase(particles_.begin());
    particles_.push_back(p);
}

void Fx::sparks(Vector3 at, int n, Color c, float speed) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        const Vector3 d = randomDir();
        const float s = speed * rnd(0.4f, 1.0f);
        p.pos = at;
        p.vel = {d.x * s, std::fabs(d.y) * s * 0.8f + 1, d.z * s};
        p.gravity = 6;
        p.drag = 1.5f;
        p.size0 = rnd(0.05f, 0.1f);
        p.c0 = lerpColor(c, WHITE, 0.4f);
        p.c1 = faded(c, 0);
        p.life = rnd(0.35f, 0.7f);
        spawn(p);
    }
}

void Fx::embers(Vector3 at, int n, Color c, float spread) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = {at.x + rnd(-spread, spread), at.y + rnd(0, 0.3f), at.z + rnd(-spread, spread)};
        p.vel = {rnd(-0.4f, 0.4f), rnd(0.8f, 2.2f), rnd(-0.4f, 0.4f)};
        p.gravity = -0.5f;
        p.drag = 0.5f;
        p.size0 = rnd(0.04f, 0.09f);
        p.c0 = c;
        p.c1 = faded(lerpColor(c, {120, 30, 10, 255}, 0.6f), 0);
        p.life = rnd(0.6f, 1.2f);
        spawn(p);
    }
}

void Fx::dust(Vector3 at, int n, float spread) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = {at.x + rnd(-spread, spread) * 0.5f, at.y + 0.05f, at.z + rnd(-spread, spread) * 0.5f};
        p.vel = {rnd(-1, 1) * spread * 2, rnd(0.2f, 1.0f), rnd(-1, 1) * spread * 2};
        p.drag = 2.5f;
        p.size0 = rnd(0.12f, 0.22f);
        p.size1 = 0.32f;
        p.c0 = {190, 175, 150, 150};
        p.c1 = {160, 150, 135, 0};
        p.life = rnd(0.5f, 0.9f);
        p.glow = false;
        spawn(p);
    }
}

void Fx::blood(Vector3 at, int n) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        const Vector3 d = randomDir();
        p.pos = at;
        p.vel = {d.x * 2.5f, std::fabs(d.y) * 3 + 1, d.z * 2.5f};
        p.gravity = 12;
        p.size0 = rnd(0.05f, 0.1f);
        p.size1 = 0.04f;
        p.c0 = {200, 20, 30, 255};
        p.c1 = {110, 10, 20, 0};
        p.life = rnd(0.5f, 0.9f);
        p.glow = false;
        spawn(p);
    }
}

void Fx::motes(Vector3 at, int n, Color c, float radius, float rise) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        const float a = rnd(0, 6.2832f), r = rnd(0, radius);
        p.pos = {at.x + std::cos(a) * r, at.y + rnd(0, 0.4f), at.z + std::sin(a) * r};
        p.vel = {0, rise * rnd(0.6f, 1.2f), 0};
        p.drag = 0.3f;
        p.size0 = rnd(0.05f, 0.1f);
        p.size1 = 0.02f;
        p.c0 = c;
        p.c1 = faded(c, 0);
        p.life = rnd(0.7f, 1.3f);
        spawn(p);
    }
}

void Fx::shards(Vector3 at, int n, Color c, float speed) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        const Vector3 d = randomDir();
        const float s = speed * rnd(0.4f, 1.0f);
        p.pos = at;
        p.vel = {d.x * s, std::fabs(d.y) * s + 1, d.z * s};
        p.gravity = 11;
        p.drag = 0.4f;
        p.size0 = rnd(0.08f, 0.17f);
        p.size1 = 0.05f;
        p.c0 = c;
        p.c1 = faded(c, 0);
        p.life = rnd(0.7f, 1.1f);
        p.glow = false;
        spawn(p);
    }
}

void Fx::streaks(Vector3 at, int n, Color c, Vector3 dir) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = {at.x + rnd(-0.3f, 0.3f), at.y + rnd(0, 1.2f), at.z + rnd(-0.3f, 0.3f)};
        p.vel = {dir.x * 6, dir.y * 6, dir.z * 6};
        p.drag = 3;
        p.size0 = 0.07f;
        p.size1 = 0.01f;
        p.c0 = c;
        p.c1 = faded(c, 0);
        p.life = rnd(0.25f, 0.45f);
        spawn(p);
    }
}

void Fx::ring(Vector3 at, float r0, float r1, float life, Color c) { capped(rings_, Ring{at, r0, r1, life, 0, c}); }

void Fx::dome(Vector3 at, float r, float life, Color c) {
    ring(at, r * 0.3f, r, life, c);
    ring({at.x, at.y + r * 0.45f, at.z}, r * 0.25f, r * 0.85f, life, c);
    ring({at.x, at.y + r * 0.8f, at.z}, r * 0.1f, r * 0.5f, life, c);
}

void Fx::beam(Vector3 from, Vector3 to, float radius, float life, Color c, float grow) {
    capped(beams_, Beam{from, to, radius, life, 0, grow, c});
}

void Fx::projectile(Vector3 from, Vector3 to, float flight, float arc, Color c, std::function<void(Vector3)> onArrive, float size) {
    capped(projs_, Proj{from, to, from, std::max(flight, 0.05f), arc, 0, size, 0, c, std::move(onArrive)});
}

void Fx::orbit(Vector3 centre, float radius, Color c, int n, float seconds) { capped(orbits_, Orbit{centre, radius, seconds, 0, c, n}); }

void Fx::later(float delay, std::function<void()> fn) { timers_.push_back({delay, std::move(fn)}); }

void Fx::update(float dt) {
    time_ += dt;
    for (Particle& p : particles_) {
        p.age += dt;
        p.vel.y -= p.gravity * dt;
        const float keep = std::max(0.0f, 1 - p.drag * dt);
        p.vel = {p.vel.x * keep, p.vel.y * keep, p.vel.z * keep};
        p.pos = {p.pos.x + p.vel.x * dt, p.pos.y + p.vel.y * dt, p.pos.z + p.vel.z * dt};
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.age >= p.life; }), particles_.end());
    for (Ring& r : rings_) r.age += dt;
    rings_.erase(std::remove_if(rings_.begin(), rings_.end(), [](const Ring& r) { return r.age >= r.life; }), rings_.end());
    for (Beam& b : beams_) b.age += dt;
    beams_.erase(std::remove_if(beams_.begin(), beams_.end(), [](const Beam& b) { return b.age >= b.life; }), beams_.end());
    for (Orbit& o : orbits_) o.age += dt;
    orbits_.erase(std::remove_if(orbits_.begin(), orbits_.end(), [](const Orbit& o) { return o.age >= o.life; }), orbits_.end());

    // Callbacks may add more shapes or timers, so run them after the lists are done changing.
    std::vector<std::pair<std::function<void(Vector3)>, Vector3>> arrived;
    for (Proj& p : projs_) {
        p.age += dt;
        const float u = std::min(1.0f, p.age / p.flight);
        p.pos = {p.from.x + (p.to.x - p.from.x) * u, p.from.y + (p.to.y - p.from.y) * u + p.arc * 4 * u * (1 - u),
                 p.from.z + (p.to.z - p.from.z) * u};
        p.trail += dt * 70;
        for (; p.trail >= 1; p.trail -= 1) {
            Particle t;
            t.pos = p.pos;
            t.size0 = p.size * 0.9f;
            t.c0 = p.c;
            t.c1 = faded(p.c, 0);
            t.life = 0.3f;
            spawn(t);
        }
        if (u >= 1 && p.onArrive) arrived.emplace_back(std::move(p.onArrive), p.to);
    }
    projs_.erase(std::remove_if(projs_.begin(), projs_.end(), [](const Proj& p) { return p.age >= p.flight; }), projs_.end());
    for (auto& [fn, at] : arrived) fn(at);

    std::vector<std::function<void()>> due;
    for (Timer& t : timers_) t.left -= dt;
    for (Timer& t : timers_)
        if (t.left <= 0) due.push_back(std::move(t.fn));
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const Timer& t) { return t.left <= 0; }), timers_.end());
    for (auto& fn : due) fn();
}

void Fx::draw() const {
    auto cube = [](const Particle& p) {
        const float u = p.age / p.life;
        const float s = p.size0 + (p.size1 - p.size0) * u;
        DrawCube(p.pos, s, s, s, lerpColor(p.c0, p.c1, u));
    };
    for (const Particle& p : particles_)
        if (!p.glow) cube(p);

    // Additive things don't write depth, so they never hide each other.
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (const Particle& p : particles_)
        if (p.glow) cube(p);
    for (const Ring& r : rings_) {
        const float u = r.age / r.life;
        const float rad = r.r0 + (r.r1 - r.r0) * (1 - (1 - u) * (1 - u));
        const Color c = faded(r.c, 1 - u);
        for (float d : {-0.03f, 0.0f, 0.03f}) DrawCircle3D(r.pos, rad + d, {1, 0, 0}, 90, c);
    }
    for (const Beam& b : beams_) {
        const float u = b.age / b.life;
        Vector3 end = b.to;
        if (b.grow > 0) {
            const float g = std::min(1.0f, b.age / b.grow);
            end = {b.from.x + (b.to.x - b.from.x) * g, b.from.y + (b.to.y - b.from.y) * g, b.from.z + (b.to.z - b.from.z) * g};
        }
        const float r = b.radius * (1 - 0.6f * u);
        DrawCylinderEx(b.from, end, r, r, 6, faded(b.c, 1 - u * u));
        DrawCylinderEx(b.from, end, r * 0.45f, r * 0.45f, 6, faded(WHITE, 1 - u * u));
    }
    for (const Proj& p : projs_) DrawSphereEx(p.pos, p.size, 6, 6, p.c);
    for (const Orbit& o : orbits_)
        for (int k = 0; k < o.n; ++k) {
            const float a = time_ * 5 + k * 6.2832f / o.n;
            const Vector3 at{o.centre.x + std::cos(a) * o.radius, o.centre.y + std::sin(a * 2) * 0.05f, o.centre.z + std::sin(a) * o.radius};
            DrawCube(at, 0.12f, 0.12f, 0.12f, o.c);
            DrawCube(at, 0.04f, 0.2f, 0.04f, o.c);
        }
    EndBlendMode();
    rlEnableDepthMask();
}
