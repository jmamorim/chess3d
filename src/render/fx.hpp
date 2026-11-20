#pragma once
// A small 3D particle system for the arena and the board: cheap cubes (glowy ones additive), plus
// shockwave rings, beams, projectiles with trails, orbiting stars and delayed callbacks. Purely
// cosmetic: it has its own random stream, so it never touches the game's dice.
#include <raylib.h>

#include <cstdint>
#include <functional>
#include <vector>

class Fx {
public:
    static constexpr size_t kMaxParticles = 800;  // beyond this the oldest are dropped

    struct Particle {
        Vector3 pos{}, vel{};
        float gravity = 0;  // downward acceleration, units/s^2
        float drag = 0;     // fraction of velocity lost per second
        float size0 = 0.1f, size1 = 0;
        Color c0 = WHITE, c1 = {255, 255, 255, 0};  // alpha fades from c0.a to c1.a
        float life = 1, age = 0;
        bool glow = true;  // drawn additively
    };

    void clear();
    void update(float dt);
    void draw() const;  // call inside BeginMode3D

    void spawn(const Particle& p);
    // Presets, all around `at`.
    void sparks(Vector3 at, int n, Color c, float speed = 4);
    void embers(Vector3 at, int n, Color c = {255, 140, 40, 255}, float spread = 0.3f);
    void dust(Vector3 at, int n, float spread = 0.5f);
    void blood(Vector3 at, int n);
    void motes(Vector3 at, int n, Color c, float radius = 0.5f, float rise = 1.2f);
    void shards(Vector3 at, int n, Color c, float speed = 4);  // solid, not glowy
    void streaks(Vector3 at, int n, Color c, Vector3 dir);     // fast thin trails along dir

    void ring(Vector3 at, float r0, float r1, float life, Color c);  // flat expanding circle
    void dome(Vector3 at, float r, float life, Color c);             // stacked rings, a hemisphere outline
    // A cylinder from `from` to `to`; with grow > 0 it extends from `from` over that many seconds.
    void beam(Vector3 from, Vector3 to, float radius, float life, Color c, float grow = 0);
    // A glowing ball that flies from `from` to `to` over `flight` seconds, then calls onArrive(to).
    void projectile(Vector3 from, Vector3 to, float flight, float arc, Color c, std::function<void(Vector3)> onArrive = {},
                    float size = 0.12f);
    void orbit(Vector3 centre, float radius, Color c, int n = 3, float seconds = 0.05f);  // stars circling centre
    void later(float delay, std::function<void()> fn);

private:
    struct Ring {
        Vector3 pos;
        float r0, r1, life, age;
        Color c;
    };
    struct Beam {
        Vector3 from, to;
        float radius, life, age, grow;
        Color c;
    };
    struct Proj {
        Vector3 from, to, pos;
        float flight, arc, age, size, trail;
        Color c;
        std::function<void(Vector3)> onArrive;
    };
    struct Orbit {
        Vector3 centre;
        float radius, life, age;
        Color c;
        int n;
    };
    struct Timer {
        float left;
        std::function<void()> fn;
    };

    float rnd() {  // 0..1
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return (seed_ & 0xFFFFFF) * (1.0f / 16777216.0f);
    }
    float rnd(float lo, float hi) { return lo + (hi - lo) * rnd(); }
    Vector3 randomDir();

    std::vector<Particle> particles_;
    std::vector<Ring> rings_;
    std::vector<Beam> beams_;
    std::vector<Proj> projs_;
    std::vector<Orbit> orbits_;
    std::vector<Timer> timers_;
    float time_ = 0;
    uint32_t seed_ = 2463534242u;
};
