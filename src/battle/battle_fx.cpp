// App members that give every battle ability its own animation: how each fighter moves during an
// event (eventPose), the particles an event spawns when it starts (startEvent), the loops that show
// ongoing status effects, and the board-ability effects. Everything here is cosmetic; the timing
// model (events of ~eventSeconds, damage shown at 45%) lives in battle_scene.cpp.
#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app.hpp"

namespace {

using K = battle::Event;

constexpr float kImpact = 0.45f;  // fraction of an event at which its damage is shown
constexpr float kGap = 3.8f;      // distance between the two fighters
constexpr float kReach = kGap - 0.9f;

const Color kGoldFx{255, 210, 80, 255};
const Color kVioletFx{175, 95, 255, 255};
const Color kDarkViolet{110, 40, 170, 255};
const Color kStone{130, 128, 140, 255};
const Color kFire{255, 150, 50, 255};
const Color kBlueFx{110, 190, 255, 255};
const Color kWisp{200, 200, 215, 170};

// Cosmetic jitter in -1..1 that leaves raylib's random stream alone.
float jitter() {
    static uint32_t s = 88172645u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s & 0xFFFF) / 32768.0f - 1;
}

float ease(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}
// 0 before a, 1 after b.
float seg(float t, float a, float b) { return ease((t - a) / (b - a)); }
// A bump between a and b.
float arch(float t, float a, float b) {
    const float u = (t - a) / (b - a);
    return u <= 0 || u >= 1 ? 0.0f : sinf(PI * u);
}
// Forward travel: wind back, charge to `reach` by b, come home by the end.
float dash(float t, float wind, float a, float b, float reach) {
    if (t < a) return -wind * ease(t / a);
    if (t < b) return -wind + (reach + wind) * ease((t - a) / (b - a));
    return reach * (1 - ease((t - b) / (1 - b)));
}
// Ballistic jump to `reach` landing at kImpact, then a quick hop home; returns x, writes the height.
float leap(float t, float reach, float height, float& y) {
    if (t < kImpact) {
        const float u = t / kImpact;
        y = 4 * height * u * (1 - u);
        return reach * u;
    }
    y = 0.2f * arch(t, kImpact, 0.75f);
    return reach * (1 - seg(t, kImpact, 1));
}

}  // namespace

App::Beat App::beat(size_t idx) const {
    const BattleScene& b = *battle_;
    const K& e = b.queue[idx];
    auto counter = [&](size_t j) {
        return j > 0 && b.queue[j].kind == K::Hit && b.queue[j - 1].kind == K::Hit && b.queue[j - 1].actor != b.queue[j].actor;
    };
    Beat r{fight().fighter(e.actor).type, e.move, 0, counter(idx)};
    if (r.counter) r.move = 0;  // a Bulwark answer is a plain blow, whatever hit it
    for (size_t j = idx; j-- > 0;) {
        const K& p = b.queue[j];
        if ((p.kind != K::Hit && p.kind != K::Miss) || p.actor != e.actor || p.move != e.move || counter(j)) break;
        ++r.hitIdx;
    }
    return r;
}

float App::runElapsed(size_t idx, float t) const {
    const BattleScene& b = *battle_;
    if (b.runOf[idx] < 0) return 0;
    float sum = 0;
    for (int j = b.runs[b.runOf[idx]].first; j < static_cast<int>(idx); ++j) sum += b.dur[j];
    return sum + t * b.dur[idx];
}

Vector3 App::fighterMid(int i) { return {fighterX(i), pieceTop(fight().fighter(i).type) * kFighterScale * 0.5f, 0}; }

void App::shakeArena(float amount) {
    if (battle_) battle_->shake = fmaxf(battle_->shake, amount);
}

// ---- Poses ----

App::Pose App::eventPose(int i, size_t idx, float t) {
    const BattleScene& b = *battle_;
    const K& e = b.queue[idx];
    const Beat bt = beat(idx);
    const float dir = i == 0 ? 1.0f : -1.0f;
    Pose p;
    const bool self = e.actor == i && e.target == i;

    if (e.actor == i && (e.kind == K::Hit || e.kind == K::Miss)) {
        float y = 0;
        switch (bt.type * 10 + bt.move) {
            case 11:  // Valiant Strike: lean back, then dash in
                p.off.x = dir * dash(t, 0.45f, 0.3f, kImpact, 2.2f);
                p.tilt = dir * 0.45f * seg(t, 0, 0.3f) * (1 - seg(t, 0.3f, 0.42f)) - dir * 0.3f * arch(t, 0.35f, 0.8f);
                break;
            case 21:  // Leap Strike
                p.off.x = dir * leap(t, kReach - 0.1f, 2.0f, y);
                p.off.y = y;
                p.tilt = -dir * 0.4f * arch(t, 0, 0.5f);
                break;
            case 24:  // Fork: a leap per hit
                p.off.x = dir * leap(t, kReach - 0.3f, 1.6f, y);
                p.off.y = y;
                p.tilt = -dir * 0.4f * arch(t, 0, 0.5f);
                break;
            case 14: {  // En Passant: dash off sideways, vanish, reappear behind the enemy and pierce through
                const float behind = kGap + 1.4f;
                if (t < 0.22f) {
                    p.off.z = -2.6f * ease(t / 0.22f);
                    p.alpha = 1 - seg(t, 0.12f, 0.22f);
                    p.flash = arch(t, 0.1f, 0.3f);
                } else if (t < 0.3f) {
                    p.alpha = 0;
                } else if (t < 0.55f) {
                    p.off.x = dir * (behind - (behind - (kGap - 1.0f)) * seg(t, 0.3f, 0.55f));
                    p.alpha = seg(t, 0.3f, 0.36f);
                    p.flash = 0.8f * (1 - seg(t, 0.3f, 0.4f));
                    p.tilt = -dir * 0.3f;
                } else {
                    p.off.x = dir * (kGap - 1.0f) * (1 - seg(t, 0.55f, 1));
                }
                p.ghosts = t < 0.6f ? 3 : 0;
                break;
            }
            case 42:  // Ram: a long charge
                p.off.x = dir * dash(t, 0.4f, 0.2f, kImpact, kReach);
                p.tilt = -dir * 0.2f * arch(t, 0.2f, 0.7f);
                p.ghosts = t > 0.2f && t < 0.5f ? 2 : 0;
                break;
            case 44:  // Siege: recoil with each shot of the volley
                p.off.x = -dir * (0.2f * arch(t, 0.0f, 0.2f) + 0.2f * arch(t, 0.12f, 0.32f) + 0.35f * arch(t, 0.24f, 0.6f));
                break;
            case 51:  // Royal Storm: spin in place
                p.yaw = 2 * PI * seg(t, 0, 0.85f);
                p.off.y = 0.15f * arch(t, 0, 1);
                p.scale = 1 + 0.1f * arch(t, 0, 1);
                break;
            case 52:  // Dominate
                p.off.x = dir * 0.5f * arch(t, 0, 0.6f);
                p.off.y = 0.2f * arch(t, 0, 0.7f);
                break;
            case 61:  // Decree
                p.off.y = 0.4f * arch(t, 0, 0.7f);
                p.tilt = -dir * 0.2f * arch(t, 0.3f, 0.7f);
                break;
            case 32:  // Drain
                p.off.x = dir * 0.5f * arch(t, 0, 0.6f);
                break;
            case 34:  // Holy Fire
                p.off.y = 0.55f * arch(t, 0, 0.85f);
                break;
            default:  // plain Attack, and a Bulwark counter-strike
                p.off.x = dir * sinf(PI * fminf(1.0f, t * 1.6f)) * 1.5f;
                p.tilt = -dir * 0.25f * arch(t, 0.1f, 0.5f);
        }
    }
    if (e.actor == i && e.kind == K::Announce && !(bt.move == 4 && (bt.type == 5 || bt.type == 6))) p.off.y += sinf(PI * t) * 0.4f;

    // Self-targeted moves.
    if (self && (e.kind == K::Heal || e.kind == K::Guard)) {
        p.tint = e.kind == K::Heal ? kHeal : SKYBLUE;
        p.tintAmt = 0.6f * sinf(PI * t);
        switch (bt.type * 10 + bt.move) {
            case 12: p.off.y = 0.45f * fabsf(sinf(2 * PI * t)); break;  // Rally: two hops
            case 22:                                                   // Feint: side-step with afterimages
                p.off.z = 1.3f * arch(t, 0, 0.6f);
                p.ghosts = t < 0.7f ? 3 : 0;
                break;
            case 31: p.off.y = 0.15f * arch(t, 0, 1); break;
            case 41: p.scale = 1 + 0.07f * arch(t, 0, 0.8f); break;
            case 62: p.off.y = 0.2f * arch(t, 0, 0.8f); break;
            default:
                if (bt.move == 3) p.scale = 1 - 0.1f * arch(t, 0, 1);  // Guard: crouch
        }
    }

    if (e.target == i && e.actor != i) {
        if (e.kind == K::Hit && t > 0.4f) {
            const float u = (t - 0.4f) / 0.6f;
            p.off.x += sinf(u * 45) * 0.1f * (1 - u);
            p.tint = kCheck;
            p.tintAmt = 0.7f * (1 - u);
            if (e.crit) p.flash = fmaxf(0.0f, 1 - u * 2.5f);
            const float away = -dir;  // the actor is on the other side: knock back along its attack
            switch (bt.type * 10 + bt.move) {
                case 11: p.off.x += away * 0.35f * sinf(PI * u); break;
                case 42: p.off.x += away * 0.8f * sinf(PI * u); break;
                case 44: p.off.x += away * 0.6f * sinf(PI * u); break;
                case 52: p.scale = 1 - 0.3f * arch(t, 0.3f, 0.9f); break;
                case 34:
                case 61:
                    p.tint = kGoldFx;
                    break;
            }
        }
        if (e.kind == K::Miss && t > 0.3f) p.off.z += sinf(PI * (t - 0.3f) / 0.7f) * 0.7f;
    }
    if (e.target == i && e.kind == K::Tick) {
        p.tint = kCurse;
        p.tintAmt = 0.6f * sinf(PI * t);
    }

    // The Ultimate's user rises (Checkmate, Divine Right) for the whole run.
    const int run = b.runOf[idx];
    if (run >= 0 && b.queue[b.runs[run].first].actor == i && (bt.type == 5 || bt.type == 6) && (self || e.actor == i)) {
        const float el = runElapsed(idx, t), total = b.runs[run].total;
        p.off.y += (bt.type == 5 ? 1.3f : 1.2f) * seg(el, 0, 0.5f) * (1 - seg(el, total - 0.45f, total));
    }
    return p;
}

// ---- Effects ----

void App::startEvent() {
    BattleScene& b = *battle_;
    const size_t idx = b.next;
    const K& e = b.queue[idx];
    const Beat bt = beat(idx);
    const float D = b.dur[idx];
    const float dirA = e.actor == 0 ? 1.0f : -1.0f;
    const Vector3 A = fighterMid(e.actor), T = fighterMid(e.target);
    const Vector3 Af{A.x, 0, 0}, Tf{T.x, 0, 0};
    const float topT = pieceTop(fight().fighter(e.target).type) * kFighterScale;
    const bool hit = e.kind == K::Hit, crit = e.crit, self = e.actor == e.target;
    const int key = bt.type * 10 + bt.move;
    const int run = b.runOf[idx];
    const bool lastOfRun = run >= 0 && static_cast<int>(idx) == b.runs[run].lastHit;
    const char* tag = e.tag ? e.tag : "";
    auto when = [&](float frac, std::function<void()> fn) { arenaFx_.later(frac * D, std::move(fn)); };
    auto every = [&](float from, float to, float step, std::function<void(float)> fn) {
        for (float f = from; f <= to + 1e-4f; f += step) when(f, [fn, f] { fn(f); });
    };
    auto shake = [&](float amount, float frac) { when(frac, [this, amount] { shakeArena(amount); }); };

    if (e.kind == K::Announce) {
        if (bt.move == 4) {  // an Ultimate gathers power first
            arenaFx_.ring(Af, 1.8f, 0.3f, 0.9f * D, kGoldFx);
            every(0.0f, 0.8f, 0.2f, [=](float) { arenaFx_.motes(Af, 4, kGoldFx, 0.8f, 1.8f); });
            if (bt.type == 5 || bt.type == 6) arenaFx_.dust(Af, 12, 0.8f);
        } else {
            when(0.3f, [=] { arenaFx_.dust(Af, 5, 0.3f); });
        }
        return;
    }

    if (e.kind == K::Hit || e.kind == K::Miss) {
        // Damage lands at kImpact; a miss gets a whiff of dust at the same moment instead.
        auto impact = [&](std::function<void()> fx) {
            when(kImpact, [=] {
                if (hit) {
                    fx();
                    if (crit) {
                        arenaFx_.sparks(T, 40, WHITE, 7);
                        arenaFx_.ring(Tf, 0.2f, 2.2f, 0.45f, WHITE);
                        if (battle_) battle_->flash = fmaxf(battle_->flash, 0.35f);
                    }
                } else {
                    arenaFx_.dust(Tf, 8, 0.6f);
                }
            });
        };
        // A projectile launched at `from` (fraction of the event) that lands at kImpact.
        auto bolt = [&](float from, Vector3 start, Color c, float arc, float size, std::function<void(Vector3)> arrive) {
            when(from, [=] {
                arenaFx_.projectile(start, T, (kImpact - from) * D, arc, c, arrive, size);
            });
        };
        switch (key) {
            case 11:  // Valiant Strike
                impact([=] {
                    arenaFx_.sparks(T, 45, kGoldFx, 6);
                    arenaFx_.ring(Tf, 0.3f, 2.0f, 0.5f, kGoldFx);
                });
                shake(0.2f, kImpact);
                break;
            case 21:  // Leap Strike: a landing slam and bleeding
                when(kImpact, [=] {
                    arenaFx_.dust({Tf.x - dirA * 0.9f, 0, 0}, 14, 0.8f);
                    arenaFx_.ring({Tf.x - dirA * 0.9f, 0.02f, 0}, 0.2f, 1.6f, 0.4f, {200, 190, 170, 255});
                    shakeArena(0.3f);
                });
                impact([=] { arenaFx_.blood(T, 32); });
                break;
            case 24: {  // Fork: each landing leaves two prongs of sparks
                when(kImpact, [=] {
                    const Vector3 L{Tf.x - dirA * 0.8f, 0.1f, 0};
                    for (float side : {-1.0f, 1.0f}) arenaFx_.beam(L, {L.x + dirA * 1.7f, 0.5f, side * 0.9f}, 0.06f, 0.4f, kGoldFx, 0.1f);
                    arenaFx_.dust({L.x, 0, 0}, 8, 0.6f);
                    shakeArena(0.25f);
                });
                impact([=] { arenaFx_.sparks(T, 24, kGoldFx, 6); });
                break;
            }
            case 14:  // En Passant: a line of light along the piercing run
                when(0.3f, [=] {
                    const float y = T.y;
                    arenaFx_.beam({Tf.x + dirA * (1.4f), y, 0}, {Tf.x - dirA * 1.0f, y, 0}, 0.09f, 0.5f, kGoldFx, 0.2f);
                });
                impact([=] {
                    arenaFx_.sparks(T, 50, WHITE, 7);
                    arenaFx_.ring(Tf, 0.2f, 1.8f, 0.45f, kGoldFx);
                });
                shake(0.35f, kImpact);
                break;
            case 32:  // Drain: red-violet motes stream from the target to the bishop
                impact([=] { arenaFx_.sparks(T, 14, {200, 40, 120, 255}, 4); });
                every(0.2f, 0.9f, 0.1f, [=](float) {
                    arenaFx_.projectile({T.x, T.y + 0.3f, 0}, A, 0.45f, 0.5f, {190, 50, 140, 255}, nullptr, 0.07f);
                });
                break;
            case 42:  // Ram: dust behind the charge, then a heavy shockwave
                for (float f = 0.2f; f <= 0.44f; f += 0.04f) {
                    const float x = Af.x + eventPose(e.actor, idx, f).off.x;  // sampled now: the queue may move on before it plays
                    when(f, [=] { arenaFx_.dust({x, 0, 0}, 3, 0.4f); });
                }
                impact([=] {
                    arenaFx_.ring(Tf, 0.3f, 3.2f, 0.6f, {230, 230, 235, 255});
                    arenaFx_.dust(Tf, 30, 1.2f);
                    arenaFx_.sparks(T, 16, WHITE, 5);
                });
                when(kImpact + 0.12f, [=] { arenaFx_.ring(Tf, 0.2f, 2.2f, 0.5f, kStone); });
                shake(0.3f, kImpact);
                break;
            case 44: {  // Siege: a volley of three cannonballs, the last one lands big
                const Vector3 M{A.x + dirA * 0.7f, A.y + 0.2f, 0};
                for (int k = 0; k < 3; ++k) {
                    const float from = 0.02f + 0.12f * k, flight = (0.19f + 0.12f * k - from) * D;
                    const bool last = k == 2;
                    when(from, [=] {
                        arenaFx_.dust(M, 4, 0.3f);
                        arenaFx_.sparks(M, 8, kFire, 4);
                        arenaFx_.projectile(M, T, flight, 0.6f, kFire, [=](Vector3 at) {
                            arenaFx_.sparks(at, last && hit ? 55 : 14, kFire, last ? 7 : 4);
                            arenaFx_.dust({at.x, 0, 0}, last && hit ? 28 : 8, 0.8f);
                            if (last && hit) {
                                arenaFx_.shards(at, 26, {120, 100, 90, 255}, 6);
                                arenaFx_.ring({at.x, 0.02f, 0}, 0.3f, 2.8f, 0.55f, kFire);
                                shakeArena(0.5f);
                            }
                        }, 0.2f);
                    });
                }
                break;
            }
            case 51:  // Royal Storm: a violet bolt per hit
                bolt(0.1f, {A.x + dirA * 0.3f, A.y * 1.7f, 0}, kVioletFx, 0.3f, 0.16f, [=](Vector3 at) {
                    if (hit) {
                        arenaFx_.sparks(at, 24, kVioletFx, 5);
                        arenaFx_.ring({at.x, 0.02f, 0}, 0.2f, 1.2f, 0.35f, kVioletFx);
                    } else {
                        arenaFx_.dust({at.x, 0, 0}, 6, 0.5f);
                    }
                });
                break;
            case 52:  // Dominate: dark rings pulse out to the enemy
                for (int k = 0; k < 4; ++k)
                    when(0.05f + 0.1f * k, [=] {
                        const float x = Af.x + (Tf.x - Af.x) * k / 3.0f;
                        arenaFx_.ring({x, 0.02f, 0}, 0.2f, 1.0f, 0.4f, kDarkViolet);
                    });
                impact([=] {
                    arenaFx_.sparks(T, 20, kDarkViolet, 4);
                    arenaFx_.ring(Tf, 0.2f, 1.6f, 0.5f, kDarkViolet);
                });
                break;
            case 61:  // Decree: a beam of gold from above
                when(0.25f, [=] { arenaFx_.beam({Tf.x, 7, 0}, {Tf.x, 0, 0}, 0.35f, 0.5f * D, kGoldFx, 0.15f * D); });
                impact([=] {
                    arenaFx_.sparks(T, 32, kGoldFx, 6);
                    arenaFx_.ring(Tf, 0.2f, 1.5f, 0.45f, kGoldFx);
                });
                break;
            case 34:  // Holy Fire: a white-gold pillar of fire
                when(0.2f, [=] { arenaFx_.beam({Tf.x, 6, 0}, {Tf.x, 0, 0}, 0.65f, 0.9f * D, {255, 235, 170, 255}, 0.2f * D); });
                every(0.3f, 1.0f, 0.1f, [=](float) {
                    if (hit) arenaFx_.embers(Tf, 8, {255, 215, 120, 255}, 0.5f);
                });
                impact([=] { arenaFx_.sparks(T, 30, {255, 235, 170, 255}, 5); });
                break;
            case 54:  // Checkmate: a bolt from above per hit, the last with a crown shockwave
                when(0.25f, [=] {
                    const float dx = (bt.hitIdx % 2 ? 0.35f : -0.35f) + (bt.hitIdx / 2) * 0.1f;
                    arenaFx_.beam({Tf.x + dx, 7, 0}, {Tf.x + dx, 0, 0}, 0.2f, 0.4f * D, kVioletFx, 0.12f * D);
                });
                impact([=] {
                    arenaFx_.sparks(T, 26, {220, 190, 255, 255}, 6);
                    arenaFx_.ring(Tf, 0.2f, 1.2f, 0.35f, kVioletFx);
                    if (lastOfRun) {
                        arenaFx_.ring({Tf.x, 0.02f, 0}, 0.3f, 2.8f, 0.8f, kGoldFx);
                        for (int k = 0; k < 5; ++k) {
                            const float a = k * 2 * PI / 5;
                            const Vector3 base{Tf.x + cosf(a) * 0.9f, 0, sinf(a) * 0.9f};
                            arenaFx_.beam(base, {base.x, k % 2 ? 1.0f : 1.6f, base.z}, 0.1f, 0.7f, kGoldFx, 0.15f);
                        }
                        arenaFx_.sparks(T, 40, kGoldFx, 8);
                        shakeArena(0.6f);
                    }
                });
                break;
            default:  // Attack, and a Bulwark counter-strike
                impact([=] { arenaFx_.sparks(T, 18, {255, 235, 170, 255}, 5); });
        }
        if (lastOfRun && hit && key != 54) shake(0.55f, kImpact);
        return;
    }

    if (e.kind == K::Tick) {
        if (std::strcmp(tag, "BURN") == 0) arenaFx_.embers(Af, 16, kFire, 0.35f);
        else arenaFx_.blood(A, 10);
        return;
    }

    if (e.kind == K::Status) {
        const Vector3 head{T.x, topT + 0.2f, 0};
        if (std::strcmp(tag, "BURN") == 0) {
            arenaFx_.embers(Tf, 24, kFire, 0.4f);
        } else if (std::strcmp(tag, "BLEED") == 0) {
            arenaFx_.blood(T, 14);
        } else if (std::strcmp(tag, "STUNNED") == 0) {
            arenaFx_.orbit(head, 0.5f, kGoldFx, 3, 1.4f);
            arenaFx_.sparks(head, 8, kGoldFx, 3);
        } else if (std::strcmp(tag, "WEAKENED") == 0) {
            arenaFx_.motes({T.x, topT * 0.8f, 0}, 16, {80, 50, 110, 220}, 0.45f, -0.8f);
        } else if (std::strcmp(tag, "EXPOSED") == 0) {
            arenaFx_.shards(T, 20, {235, 70, 60, 255}, 4.5f);
            arenaFx_.sparks(T, 10, {255, 220, 200, 255}, 4);
        } else if (std::strcmp(tag, "REGEN") == 0) {
            arenaFx_.motes(Tf, 16, kHeal, 0.5f, 1.3f);
        } else if (std::strcmp(tag, "HASTE") == 0) {
            arenaFx_.streaks(Tf, 14, WHITE, {T.x < 0 ? -1.0f : 1.0f, 0, 0});
        }
        return;
    }

    // Heal and Guard: the fighter's own moves.
    if (!self) return;
    if (std::strcmp(tag, "CLEANSED") == 0) {  // Divine Right
        if (battle_) battle_->flash = 0.55f;
        arenaFx_.ring({Af.x, 0.02f, 0}, 0.3f, 3.0f, 0.8f, kGoldFx);
        arenaFx_.motes(Af, 26, WHITE, 0.8f, 2.2f);
        return;
    }
    switch (key) {
        case 12:  // Rally
            every(0.05f, 0.85f, 0.1f, [=](float) { arenaFx_.motes(Af, 4, kGoldFx, 0.5f, 1.8f); });
            arenaFx_.ring(Af, 0.3f, 1.2f, 0.5f * D, kGoldFx);
            return;
        case 22:  // Feint: grey wisps
            every(0.0f, 0.5f, 0.15f, [=](float) { arenaFx_.motes(A, 5, kWisp, 0.6f, 0.7f); });
            return;
        case 31:  // Mend: a green pillar settles on the bishop
            arenaFx_.beam({Af.x, 5, 0}, Af, 0.5f, 0.8f * D, kHeal, 0.2f * D);
            every(0.1f, 0.9f, 0.1f, [=](float) { arenaFx_.motes(Af, 5, kHeal, 0.5f, 1.6f); });
            return;
        case 32:  // the heal that follows Drain
            arenaFx_.motes(Af, 12, kHeal, 0.5f, 1.4f);
            return;
        case 41:  // Bulwark: stone shards rise into a ring wall
            for (int k = 0; k < 14; ++k) {
                const float a = k * 2 * PI / 14;
                when(0.5f * k / 14.0f, [=] {
                    Fx::Particle p;
                    p.pos = {Af.x + cosf(a) * 0.95f, 0.05f, sinf(a) * 0.95f};
                    p.vel = {0, 3.4f, 0};
                    p.drag = 4;
                    p.size0 = 0.22f;
                    p.size1 = 0.2f;
                    p.c0 = kStone;
                    p.c1 = {kStone.r, kStone.g, kStone.b, 0};
                    p.life = 1.3f * D;
                    p.glow = false;
                    arenaFx_.spawn(p);
                });
            }
            arenaFx_.dust(Af, 14, 0.9f);
            return;
        case 62:  // Royal Guard: a gold dome and green motes
            arenaFx_.dome({Af.x, 0.02f, 0}, 1.3f, 0.9f, kGoldFx);
            every(0.1f, 0.7f, 0.15f, [=](float) { arenaFx_.motes(Af, 4, kHeal, 0.5f, 1.4f); });
            return;
    }
    if (e.kind == K::Guard && bt.move == 3) {
        arenaFx_.ring({Af.x, 0.02f, 0}, 0.3f, 1.5f, 0.6f, kBlueFx);
        when(0.2f, [=] { arenaFx_.ring({Af.x, 0.02f, 0}, 0.3f, 1.2f, 0.5f, kBlueFx); });
    } else if (e.kind == K::Heal) {
        arenaFx_.motes(Af, 10, kHeal, 0.45f, 1.1f);  // Regen
    }
}

// Light loops for effects a fighter is carrying.
void App::ambientFx(float dt) {
    BattleScene& b = *battle_;
    for (int i = 0; i < 2; ++i) {
        const battle::Fighter& f = fight().fighter(i);
        b.ambientT[i] += dt;
        if (b.ambientT[i] < 0.09f) continue;
        b.ambientT[i] = 0;
        if (f.hp <= 0) continue;
        const float top = pieceTop(f.type) * kFighterScale;
        const Vector3 foot{fighterX(i), 0, 0}, body{fighterX(i), top * 0.5f, 0};
        const float facing = i == 0 ? 1.0f : -1.0f;
        auto has = [&](battle::Status s) { return f.status[static_cast<int>(s)] > 0; };
        using S = battle::Status;
        if (has(S::Burn)) arenaFx_.embers(foot, 1, kFire, 0.3f);
        if (has(S::Bleed)) {
            Fx::Particle p;
            p.pos = {body.x + jitter() * 0.3f, top * 0.6f, 0.2f};
            p.gravity = 9;
            p.size0 = 0.06f;
            p.c0 = {200, 20, 30, 255};
            p.c1 = {200, 20, 30, 0};
            p.life = 0.5f;
            p.glow = false;
            arenaFx_.spawn(p);
        }
        if (has(S::Regen)) arenaFx_.motes(foot, 1, kHeal, 0.4f, 1.0f);
        if (has(S::Stun)) arenaFx_.orbit({foot.x, top + 0.2f, 0}, 0.5f, kGoldFx, 3, 0.12f);
        if (has(S::AtkDown)) arenaFx_.motes({body.x, top * 0.9f, 0}, 1, {80, 50, 110, 220}, 0.35f, -0.7f);
        if (has(S::DefDown) && jitter() > 0) {
            Fx::Particle p;
            p.pos = {body.x + jitter() * 0.25f, top * (0.55f + jitter() * 0.35f), 0.25f};
            p.size0 = 0.14f;
            p.c0 = {255, 70, 50, 230};
            p.c1 = {255, 70, 50, 0};
            p.life = 0.15f;
            arenaFx_.spawn(p);
        }
        if (has(S::Haste)) arenaFx_.streaks(foot, 1, WHITE, {-facing, 0, 0});
    }
}

// ---- Board abilities ----

void App::boardAbilityFx(int s, int which, int type, const std::vector<battle::AbilityEffect>& effects) {
    const Vector3 c = squareCenter(s);
    const Vector3 flat{c.x, 0.02f, c.z}, body{c.x, 0.6f, c.z};
    const Color red{230, 60, 50, 255};
    auto bodyOf = [](int sq) {
        const Vector3 v = squareCenter(sq);
        return Vector3{v.x, 0.6f, v.z};
    };
    if (which == 0) {
        switch (type) {
            case 1:  // Shield Up
                boardFx_.dome(flat, 0.8f, 0.9f, kBlueFx);
                boardFx_.motes(c, 6, kBlueFx, 0.4f, 1.0f);
                break;
            case 2:  // Battle Cry
                for (int k = 0; k < 3; ++k) boardFx_.later(0.18f * k, [this, flat, red] { boardFx_.ring(flat, 0.2f, 1.7f, 0.5f, red); });
                boardFx_.sparks(body, 14, red, 4);
                break;
            case 3:  // Heal: a green pulse to each healed neighbour
                boardFx_.ring(flat, 0.2f, 0.8f, 0.5f, kHeal);
                for (const battle::AbilityEffect& e : effects) {
                    if (e.healed <= 0) continue;
                    boardFx_.projectile(body, bodyOf(e.square), 0.4f, 0.5f, kHeal, [this](Vector3 at) {
                        boardFx_.motes({at.x, 0, at.z}, 8, kHeal, 0.3f, 1.2f);
                        boardFx_.ring({at.x, 0.02f, at.z}, 0.1f, 0.6f, 0.4f, kHeal);
                    });
                }
                break;
            case 4:  // Fortify: a ring of stone
                for (int k = 0; k < 10; ++k) {
                    const float a = k * 2 * PI / 10;
                    boardFx_.later(0.04f * k, [this, c, a] {
                        Fx::Particle p;
                        p.pos = {c.x + cosf(a) * 0.5f, 0.05f, c.z + sinf(a) * 0.5f};
                        p.vel = {0, 2.4f, 0};
                        p.drag = 4;
                        p.size0 = 0.14f;
                        p.c0 = kStone;
                        p.c1 = {kStone.r, kStone.g, kStone.b, 0};
                        p.life = 1.0f;
                        p.glow = false;
                        boardFx_.spawn(p);
                    });
                }
                boardFx_.dust(c, 10, 0.6f);
                break;
            case 5:  // Inspire: gold motes fly to each inspired neighbour
                boardFx_.ring(flat, 0.2f, 0.8f, 0.5f, kGoldFx);
                for (const battle::AbilityEffect& e : effects)
                    boardFx_.projectile(body, bodyOf(e.square), 0.45f, 0.6f, kGoldFx, [this](Vector3 at) {
                        boardFx_.motes({at.x, 0, at.z}, 8, kGoldFx, 0.3f, 1.2f);
                        boardFx_.ring({at.x, 0.02f, at.z}, 0.1f, 0.6f, 0.4f, kGoldFx);
                    }, 0.09f);
                break;
            case 6:  // Rest
                boardFx_.motes(c, 16, kHeal, 0.35f, 1.0f);
                boardFx_.ring(flat, 0.2f, 0.7f, 0.6f, kHeal);
                break;
        }
        return;
    }
    if (type == 5) {  // Terror: a dark ring over radius 2, reaching each target as it passes
        boardFx_.ring(flat, 0.3f, 2.6f, 0.9f, kDarkViolet);
        boardFx_.later(0.2f, [this, flat] { boardFx_.ring(flat, 0.3f, 2.2f, 0.7f, kVioletFx); });
    }
    for (const battle::AbilityEffect& e : effects) {
        const Vector3 tb = bodyOf(e.square);
        const float dist = Vector3Distance(body, tb);
        switch (type) {
            case 1:  // Pike Jab: a quick stab
                boardFx_.projectile(body, tb, 0.14f, 0, {255, 240, 200, 255}, [this](Vector3 at) { boardFx_.sparks(at, 12, WHITE, 3.5f); }, 0.09f);
                break;
            case 2:  // Harass: arcing darts
                boardFx_.projectile(body, tb, 0.45f, 1.0f, {200, 220, 255, 255}, [this](Vector3 at) { boardFx_.sparks(at, 10, kBlueFx, 3); }, 0.08f);
                break;
            case 3:  // Smite: a ray of light along the diagonal
                boardFx_.beam(body, tb, 0.07f, 0.45f, kGoldFx, 0.12f);
                boardFx_.later(0.12f, [this, tb] {
                    boardFx_.sparks(tb, 16, kGoldFx, 4);
                    boardFx_.embers(tb, 8, kFire, 0.2f);
                });
                break;
            case 4:  // Cannon
                boardFx_.projectile(body, tb, 0.25f + 0.04f * dist, 0.5f, kFire, [this](Vector3 at) {
                    boardFx_.sparks(at, 20, kFire, 5);
                    boardFx_.dust({at.x, 0, at.z}, 8, 0.5f);
                    boardFx_.shards(at, 6, kStone, 4);
                }, 0.15f);
                break;
            case 5:
                boardFx_.later(0.9f * dist / 2.6f, [this, tb] {
                    boardFx_.motes({tb.x, 0, tb.z}, 8, {90, 50, 130, 230}, 0.3f, 0.8f);
                    boardFx_.sparks(tb, 8, kDarkViolet, 3);
                });
                break;
            case 6:  // Condemn
                boardFx_.later(0.1f, [this, tb] {
                    boardFx_.embers(tb, 16, kFire, 0.25f);
                    boardFx_.sparks(tb, 10, kFire, 4);
                    boardFx_.ring({tb.x, 0.02f, tb.z}, 0.1f, 0.7f, 0.4f, kFire);
                });
                break;
        }
    }
    if (type == 6) boardFx_.embers(c, 10, kFire, 0.4f);
}
