#include "battle.hpp"

#include <algorithm>
#include <cmath>

namespace battle {

namespace {

constexpr Stats kStats[7] = {
    {"?", 1, 1, 1, 1, "-", "-"},
    {"Pawn", 40, 10, 4, 5, "Valiant Strike", "2.2x power, 35% to miss"},
    {"Knight", 60, 13, 6, 9, "Leap Strike", "1.3x power, ignores defense"},
    {"Bishop", 55, 12, 5, 7, "Mend", "heal 40% of max HP"},
    {"Rook", 80, 14, 9, 3, "Bulwark", "block 75% of next hit, strike back"},
    {"Queen", 95, 17, 8, 7, "Royal Storm", "three hits at 0.6x power"},
    {"King", 100, 12, 10, 4, "Decree", "1.5x power"},
};

constexpr float kCritChance = 0.12f;
constexpr float kCritMult = 1.6f;
constexpr int kFatigueAfter = 12;  // turns before fatigue sets in
constexpr float kFatiguePerTurn = 0.15f;

}  // namespace

const Stats& statsOf(int type) { return kStats[std::clamp(type, 0, 6)]; }

Battle::Battle(int attackerType, bool attackerWhite, int attackerHp, int defenderType, int defenderHp, uint32_t seed)
    : rng_(seed) {
    f_[0] = {attackerType, attackerWhite, std::clamp(attackerHp, 1, maxHp(attackerType))};
    f_[1] = {defenderType, !attackerWhite, std::clamp(defenderHp, 1, maxHp(defenderType))};
    // The capturing piece always strikes first; speed only affects dodging.
}

std::string Battle::label(int i) const {
    return std::string(f_[i].white ? "White " : "Black ") + statsOf(f_[i].type).name;
}

void Battle::push(std::vector<Event>& out, Event e) const {
    e.hp[0] = f_[0].hp;
    e.hp[1] = f_[1].hp;
    out.push_back(std::move(e));
}

void Battle::hit(std::vector<Event>& out, int actor, float power, bool pierce, float missChance) {
    const int target = 1 - actor;
    if (f_[target].hp <= 0) return;
    const Stats& a = statsOf(f_[actor].type);
    const Stats& d = statsOf(f_[target].type);
    // Faster targets dodge a little more often.
    missChance += std::max(0, d.spd - a.spd) * 0.02f;
    if (roll() < missChance) {
        push(out, {Event::Miss, actor, target, 0, false, {}, label(target) + " dodges!"});
        return;
    }
    const bool crit = roll() < kCritChance;
    float raw = a.atk * power * (0.9f + 0.2f * roll());
    // Fatigue: long fights hit ever harder, so two healers cannot stall forever.
    raw *= 1.0f + kFatiguePerTurn * std::max(0, turns_ - kFatigueAfter);
    if (crit) raw *= kCritMult;
    else if (!pierce) raw -= d.def * 0.5f;
    const int guard = f_[target].guard;
    float taken = raw;
    if (guard == 1) taken *= 0.5f;
    if (guard == 2) taken *= 0.25f;
    const int dmg = std::max(1, static_cast<int>(std::lround(taken)));
    f_[target].hp = std::max(0, f_[target].hp - dmg);
    std::string text = (crit ? "Critical! " : "") + label(target) + " takes " + std::to_string(dmg) + " damage";
    push(out, {Event::Hit, actor, target, dmg, crit, {}, text + (f_[target].hp == 0 ? " and falls!" : "!")});
    if (guard == 2) {
        f_[target].guard = 0;  // Bulwark answers one blow
        if (f_[target].hp > 0) {
            const int back = std::max(1, static_cast<int>(std::lround(std::max(raw, 1.0f) * 0.5f)));
            f_[actor].hp = std::max(0, f_[actor].hp - back);
            push(out, {Event::Hit, target, actor, back, false, {},
                       label(target) + " strikes back for " + std::to_string(back) + (f_[actor].hp == 0 ? " - KO!" : "!")});
        }
    }
}

std::vector<Event> Battle::act(Command c) {
    std::vector<Event> out;
    if (over()) return out;
    const int me = turn_;
    Fighter& self = f_[me];
    self.guard = 0;  // a guard lasts until the guarder's next turn
    if (c == Command::Skill && self.cooldown > 0) c = Command::Attack;
    const Stats& st = statsOf(self.type);
    const std::string who = label(me);

    switch (c) {
        case Command::Attack:
            push(out, {Event::Announce, me, 1 - me, 0, false, {}, who + " attacks!"});
            hit(out, me, 1.0f, false, 0.05f);
            break;
        case Command::Guard: {
            self.guard = 1;
            const int heal = std::min(maxHp(self.type) - self.hp, std::max(1, maxHp(self.type) / 20));
            self.hp += heal;
            push(out, {Event::Guard, me, me, heal, false, {}, who + " guards" + (heal > 0 ? " and recovers " + std::to_string(heal) + " HP." : ".")});
            break;
        }
        case Command::Skill:
            push(out, {Event::Announce, me, 1 - me, 0, false, {}, who + " uses " + st.skill + "!"});
            switch (self.type) {
                case 1: hit(out, me, 2.2f, false, 0.35f); break;
                case 2: hit(out, me, 1.3f, true, 0.05f); break;
                case 3: {
                    const int heal = std::min(maxHp(self.type) - self.hp, maxHp(self.type) * 2 / 5);
                    self.hp += heal;
                    push(out, {Event::Heal, me, me, heal, false, {}, who + " recovers " + std::to_string(heal) + " HP."});
                    break;
                }
                case 4:
                    self.guard = 2;
                    push(out, {Event::Guard, me, me, 0, false, {}, who + " braces behind its walls."});
                    break;
                case 5:
                    for (int i = 0; i < 3; ++i) hit(out, me, 0.6f, false, 0.05f);
                    break;
                default: hit(out, me, 1.5f, false, 0.05f); break;
            }
            break;
    }
    if (c == Command::Skill) self.cooldown = kCooldown;
    else if (self.cooldown > 0) --self.cooldown;
    turn_ = 1 - turn_;
    ++turns_;
    if (turns_ == kFatigueAfter + 1 && !over()) push(out, {Event::Announce, me, me, 0, false, {}, "Both fighters tire - every blow hits harder!"});
    return out;
}

Command Battle::aiChoice() {
    const Fighter& self = f_[turn_];
    const float hpFrac = static_cast<float>(self.hp) / maxHp(self.type);
    const bool ready = canUse(Command::Skill);
    switch (self.type) {
        case 3:  // Bishop: heal when hurt
            if (ready && hpFrac < 0.5f) return Command::Skill;
            break;
        case 4:  // Rook: brace about half the time
            if (ready && roll() < 0.5f) return Command::Skill;
            break;
        case 1:  // Pawn: the strike is a gamble
            if (ready && roll() < 0.5f) return Command::Skill;
            break;
        default:
            if (ready) return Command::Skill;
            break;
    }
    if (hpFrac < 0.25f && roll() < 0.3f) return Command::Guard;
    return Command::Attack;
}

}  // namespace battle
