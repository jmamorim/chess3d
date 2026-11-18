#pragma once
// Turn-based battle fought when one piece captures another in battle mode. No raylib dependency,
// so it is tested headless; main.cpp only animates the events it returns.
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace battle {

enum class Command { Attack, Skill, Skill2, Guard };

struct Stats {
    const char* name;
    int hp, atk, def, spd;
    const char* skill;
    const char* skillInfo;
    const char* skill2;
    const char* skill2Info;
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
    bool weakened = false;     // Dominate: this fighter's hits during its next action are halved
};

struct Event {
    enum Kind { Announce, Hit, Miss, Heal, Guard, Status } kind = Announce;
    int actor = 0;   // 0 = attacker, 1 = defender
    int target = 0;  // who the number applies to
    int amount = 0;
    bool crit = false;
    int hp[2] = {0, 0};  // both fighters' HP once this event has happened
    std::string text;
    const char* tag = nullptr;  // short label shown over `target` (GUARD, WEAKENED, ...)
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
        return true;
    }

    // Applies a board buff before the first action: guard 1 (Shield Up) or 2 (Fortify), and an
    // empowered first hit (Battle Cry, Inspire). A guard set here lasts until the fighter's own next turn.
    void prepare(int i, int guard, bool empowered) {
        if (guard > f_[i].guard) f_[i].guard = guard;
        f_[i].empowered = f_[i].empowered || empowered;
    }
    // The fighter whose turn it is carries out `c` (a skill falls back to Attack while cooling down).
    std::vector<Event> act(Command c);
    // A simple opponent: heals or guards when hurt, otherwise uses its skills when they are ready.
    Command aiChoice();

    static constexpr int kCooldown = 2;

private:
    Fighter f_[2];
    int turn_ = 0;
    int turns_ = 0;  // actions taken so far
    std::mt19937 rng_;

    float roll() { return std::uniform_real_distribution<float>(0, 1)(rng_); }
    std::string label(int i) const;
    // Returns the damage dealt (0 on a miss).
    int hit(std::vector<Event>& out, int actor, float power, bool pierce, float missChance);
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
};
using Units = std::array<Unit, 64>;

struct Ability {
    const char* name;
    const char* info;
};
const Ability& abilityOf(int type);

// Own turns an ability is unavailable after use.
constexpr int kAbilityCooldown = 3;
inline int abilityReadyAt(int ply) { return ply + 2 * (kAbilityCooldown + 1); }
// Own turns left before the ability is ready (0 = ready).
inline int abilityWait(const Unit& u, int ply) { return u.readyAt > ply ? (u.readyAt - ply + 1) / 2 : 0; }

// board: signed piece codes as in chess::Position::sq (positive = white, |code| = piece type).
// True when the piece on s is off cooldown and its ability would change something.
bool abilityUseful(const std::array<int8_t, 64>& board, const Units& units, int s, int ply);

struct AbilityEffect {
    int square;
    int healed;     // HP restored (0 if none)
    uint8_t buff;   // buff added (0 if none)
};
// Uses the ability of the piece on s and starts its cooldown. Returns what changed, per square.
std::vector<AbilityEffect> useAbility(const std::array<int8_t, 64>& board, Units& units, int s, int ply);

}  // namespace battle
