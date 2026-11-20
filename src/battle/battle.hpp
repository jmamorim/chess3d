#pragma once
// Turn-based battle fought when one piece captures another in battle mode. No raylib dependency,
// so it is tested headless; the app only animates the events it returns.
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace battle {

enum class Command { Attack, Skill, Skill2, Guard, Ultimate };

// Multi-turn effects; `turns left` live in Fighter::status.
enum class Status : uint8_t { Burn, Bleed, Regen, Stun, AtkDown, DefDown, Haste, Count };
constexpr int kStatusCount = 7;
const char* statusName(Status s);

constexpr int kUltimateCharge = 100;

struct Stats {
    const char* name;
    int hp, atk, def, spd;
    const char* skill;
    const char* skillInfo;
    const char* skill2;
    const char* skill2Info;
    const char* ultimate;
    const char* ultimateInfo;
};
// Indexed by chess::PieceType (PAWN..KING).
const Stats& statsOf(int type);
inline int maxHp(int type) { return statsOf(type).hp; }

struct Fighter {
    int type = 0;
    bool white = true;
    int hp = 0;
    int cooldown[2] = {0, 0};  // own turns until Skill / Skill2 is ready again
    int guard = 0;             // 0 none, 1 Guard (half damage), 2 Bulwark (quarter damage + counter)
    bool empowered = false;    // Rally: the next hit attempt is stronger
    bool evading = false;      // Feint: the next enemy hit attempt before this fighter's turn likely misses
    int status[kStatusCount] = {};  // turns left per Status, 0 = none
    int charge = 0;                 // 0..kUltimateCharge; full unlocks the Ultimate
    bool stunnedLast = false;       // a stun just skipped this fighter's turn: it can't be stunned again yet
};

struct Event {
    enum Kind { Announce, Hit, Miss, Heal, Guard, Status, Tick } kind = Announce;
    int actor = 0;   // 0 = attacker, 1 = defender
    int target = 0;  // who the number applies to
    int amount = 0;
    bool crit = false;
    int hp[2] = {0, 0};  // both fighters' HP once this event has happened
    std::string text;
    const char* tag = nullptr;  // short label shown over `target` (GUARD, WEAKENED, ...)
    // The Command (as an index) whose action produced this event; -1 for start-of-turn ticks, regen,
    // stun and fatigue. Only the animation reads it.
    int8_t move = -1;
};

class Battle {
public:
    // hp values are the pieces' current (possibly wounded) HP.
    Battle(int attackerType, bool attackerWhite, int attackerHp, int defenderType, int defenderHp, uint32_t seed);

    const Fighter& fighter(int i) const { return f_[i]; }
    int turn() const { return turn_; }  // whose command is next
    bool over() const { return f_[0].hp <= 0 || f_[1].hp <= 0; }
    int winner() const { return f_[0].hp > 0 ? 0 : 1; }
    bool canUse(Command c) const {
        if (c == Command::Skill) return f_[turn_].cooldown[0] == 0;
        if (c == Command::Skill2) return f_[turn_].cooldown[1] == 0;
        if (c == Command::Ultimate) return f_[turn_].charge >= kUltimateCharge;
        return true;
    }

    // Applies a board buff before the first action: guard 1 (Shield Up) or 2 (Fortify), and an
    // empowered first hit (Battle Cry, Inspire). A guard set here lasts until the fighter's own next turn.
    void prepare(int i, int guard, bool empowered) {
        if (guard > f_[i].guard) f_[i].guard = guard;
        f_[i].empowered = f_[i].empowered || empowered;
    }
    // Carries board strikes into the battle (Curse bits); like prepare(), call before the first act.
    void curse(int i, uint8_t curses);
    // The fighter whose turn it is carries out `c` (a skill falls back to Attack while cooling down).
    std::vector<Event> act(Command c);
    // A simple opponent: heals or guards when hurt, otherwise uses its skills when they are ready.
    Command aiChoice();

    static constexpr int kCooldown = 2;

private:
    Fighter f_[2];
    int turn_ = 0;
    int turns_ = 0;  // actions taken so far
    int8_t move_ = -1;  // stamped on pushed events: the command being carried out, -1 outside one
    std::mt19937 rng_;
    std::mt19937 aiRng_;  // separate stream so asking the AI never shifts the dice (replays stay exact)

    // Not std::uniform_real_distribution: its output is implementation-defined, mt19937's is not.
    static float unit(std::mt19937& g) { return (g() >> 8) * (1.0f / 16777216.0f); }
    float roll() { return unit(rng_); }
    std::string label(int i) const;
    // Returns the damage dealt (0 on a miss).
    // `sure` never misses and ignores guard, evade and Haste.
    int hit(std::vector<Event>& out, int actor, float power, bool pierce, float missChance, bool sure = false);
    // Returns false when nothing was applied (dead target, or a stun-lock guard).
    bool applyStatus(std::vector<Event>& out, int actor, int target, Status s, int turns);
    void gain(int i, int amount) { f_[i].charge = std::clamp(f_[i].charge + amount, 0, kUltimateCharge); }
    void push(std::vector<Event>& out, Event e) const;
};

// ---- Board abilities ----
// Instead of moving, a piece may use its ability; that takes the whole turn. Buffs wait on the
// piece until its next battle (attacking or defending), which uses them up.
enum Buff : uint8_t { kShield = 1, kBulwark = 2, kEmpower = 4 };

// A piece's battle state on the board. It moves with the piece.
struct Unit {
    int dmg = 0;      // HP lost so far
    int readyAt = 0;  // ply from which the ability can be used again
    uint8_t buffs = 0;
    int readyAt2 = 0;     // same, for the strike (ability index 1)
    uint8_t curses = 0;
};
// Curses a board strike leaves on a piece until its next battle.
enum Curse : uint8_t { kCurseBurn = 1, kCurseBleed = 2, kCurseExposed = 4, kCurseWeak = 8 };
const char* curseName(uint8_t bit);

using Units = std::array<Unit, 64>;

struct Ability {
    const char* name;
    const char* info;
};
const Ability& abilityOf(int type, int which = 0);  // which 0 = support, 1 = strike

// Own turns an ability is unavailable after use.
constexpr int kAbilityCooldown = 3;
constexpr int kStrikeCooldown = 4;
inline int abilityReadyAt(int ply, int which = 0) { return ply + 2 * ((which ? kStrikeCooldown : kAbilityCooldown) + 1); }
// Own turns left before the ability is ready (0 = ready).
inline int abilityWait(const Unit& u, int ply, int which = 0) {
    const int at = which ? u.readyAt2 : u.readyAt;
    return at > ply ? (at - ply + 1) / 2 : 0;
}

// board: signed piece codes as in chess::Position::sq (positive = white, |code| = piece type).
// True when the piece on s is off cooldown and its ability would change something.
bool abilityUseful(const std::array<int8_t, 64>& board, const Units& units, int s, int ply, int which = 0);

struct AbilityEffect {
    int square;
    int healed;     // HP restored (0 if none)
    uint8_t buff;   // buff added (0 if none)
    int damage = 0;      // board HP removed (strikes never kill)
    uint8_t curse = 0;   // curse bits newly added
};
// Uses the ability of the piece on s and starts its cooldown. Returns what changed, per square.
std::vector<AbilityEffect> useAbility(const std::array<int8_t, 64>& board, Units& units, int s, int ply, int which = 0);

}  // namespace battle
