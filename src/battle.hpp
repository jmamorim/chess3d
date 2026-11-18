#pragma once
// Turn-based battle fought when one piece captures another in battle mode. No raylib dependency,
// so it is tested headless; main.cpp only animates the events it returns.
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

}  // namespace battle
