#include "battle.hpp"

#include <algorithm>
#include <cmath>

namespace battle {

namespace {

constexpr Stats kStats[7] = {
    {"?", 1, 1, 1, 1, "-", "-", "-", "-"},
    {"Pawn", 40, 10, 4, 5, "Valiant Strike", "2.2x power, 35% to miss", "Rally", "heal 15%, next hit +60%"},
    {"Knight", 60, 13, 6, 9, "Leap Strike", "1.3x power, ignores defense", "Feint", "75% dodge vs the next hit"},
    {"Bishop", 55, 12, 5, 7, "Mend", "heal 40% of max HP", "Drain", "1.0x hit, heal half the damage"},
    {"Rook", 80, 14, 9, 3, "Bulwark", "block 75% of next hit, strike back", "Ram", "1.8x power, 20% to miss"},
    {"Queen", 95, 17, 8, 7, "Royal Storm", "three hits at 0.6x power", "Dominate", "1.2x hit, enemy's next hits halved"},
    {"King", 100, 12, 10, 4, "Decree", "1.5x power", "Royal Guard", "heal 20% and guard"},
};

constexpr float kCritChance = 0.12f;
constexpr float kCritMult = 1.6f;
constexpr int kFatigueAfter = 12;  // turns before fatigue sets in
constexpr float kFatiguePerTurn = 0.15f;
constexpr float kRallyMult = 1.6f;
constexpr float kFeintMiss = 0.75f;
constexpr float kWeakenedMult = 0.5f;

}  // namespace

const Stats& statsOf(int type) { return kStats[std::clamp(type, 0, 6)]; }

Battle::Battle(int attackerType, bool attackerWhite, int attackerHp, int defenderType, int defenderHp, uint32_t seed)
    : rng_(seed) {
    f_[0].type = attackerType;
    f_[0].white = attackerWhite;
    f_[0].hp = std::clamp(attackerHp, 1, maxHp(attackerType));
    f_[1].type = defenderType;
    f_[1].white = !attackerWhite;
    f_[1].hp = std::clamp(defenderHp, 1, maxHp(defenderType));
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

int Battle::hit(std::vector<Event>& out, int actor, float power, bool pierce, float missChance) {
    const int target = 1 - actor;
    if (f_[target].hp <= 0) return 0;
    const Stats& a = statsOf(f_[actor].type);
    const Stats& d = statsOf(f_[target].type);
    if (f_[actor].empowered) {
        power *= kRallyMult;
        f_[actor].empowered = false;
    }
    if (f_[actor].weakened) power *= kWeakenedMult;
    // Faster targets dodge a little more often.
    missChance += std::max(0, d.spd - a.spd) * 0.02f;
    if (f_[target].evading) {
        missChance = std::max(missChance, kFeintMiss);
        f_[target].evading = false;
    }
    if (roll() < missChance) {
        push(out, {Event::Miss, actor, target, 0, false, {}, label(target) + " dodges!"});
        return 0;
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
    const int dmg = std::min(f_[target].hp, std::max(1, static_cast<int>(std::lround(taken))));
    f_[target].hp -= dmg;
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
    return dmg;
}

std::vector<Event> Battle::act(Command c) {
    std::vector<Event> out;
    if (over()) return out;
    const int me = turn_;
    Fighter& self = f_[me];
    Fighter& foe = f_[1 - me];
    // Guard and Feint last until the fighter's own next turn. The attacker's first turn keeps a guard
    // from prepare(), so it still covers the defender's first reply.
    if (turns_ > 0) self.guard = 0;
    self.evading = false;
    if (!canUse(c)) c = Command::Attack;
    const Stats& st = statsOf(self.type);
    const std::string who = label(me);
    auto heal = [&](int percent) {
        const int amount = std::min(maxHp(self.type) - self.hp, maxHp(self.type) * percent / 100);
        self.hp += amount;
        return amount;
    };

    switch (c) {
        case Command::Attack:
            push(out, {Event::Announce, me, 1 - me, 0, false, {}, who + " attacks!"});
            hit(out, me, 1.0f, false, 0.05f);
            break;
        case Command::Guard: {
            self.guard = 1;
            const int amount = std::min(maxHp(self.type) - self.hp, std::max(1, maxHp(self.type) / 20));
            self.hp += amount;
            push(out, {Event::Guard, me, me, amount, false, {},
                       who + " guards" + (amount > 0 ? " and recovers " + std::to_string(amount) + " HP." : "."), "GUARD"});
            break;
        }
        case Command::Skill:
            push(out, {Event::Announce, me, 1 - me, 0, false, {}, who + " uses " + st.skill + "!"});
            switch (self.type) {
                case 1: hit(out, me, 2.2f, false, 0.35f); break;
                case 2: hit(out, me, 1.3f, true, 0.05f); break;
                case 3: {
                    const int amount = heal(40);
                    push(out, {Event::Heal, me, me, amount, false, {}, who + " recovers " + std::to_string(amount) + " HP."});
                    break;
                }
                case 4:
                    self.guard = 2;
                    push(out, {Event::Guard, me, me, 0, false, {}, who + " braces behind its walls.", "BULWARK"});
                    break;
                case 5:
                    for (int i = 0; i < 3; ++i) hit(out, me, 0.6f, false, 0.05f);
                    break;
                default: hit(out, me, 1.5f, false, 0.05f); break;
            }
            break;
        case Command::Skill2:
            push(out, {Event::Announce, me, 1 - me, 0, false, {}, who + " uses " + st.skill2 + "!"});
            switch (self.type) {
                case 1: {
                    const int amount = heal(15);
                    self.empowered = true;
                    push(out, {Event::Heal, me, me, amount, false, {}, who + " rallies - its next hit is stronger!", "EMPOWERED"});
                    break;
                }
                case 2:
                    self.evading = true;
                    push(out, {Event::Guard, me, me, 0, false, {}, who + " feints, ready to dodge.", "EVADE"});
                    break;
                case 3: {
                    const int dealt = hit(out, me, 1.0f, false, 0.05f);
                    if (dealt > 0 && self.hp > 0) {
                        const int amount = std::min(maxHp(self.type) - self.hp, std::max(1, dealt / 2));
                        self.hp += amount;
                        push(out, {Event::Heal, me, me, amount, false, {}, who + " drains " + std::to_string(amount) + " HP."});
                    }
                    break;
                }
                case 4: hit(out, me, 1.8f, false, 0.20f); break;
                case 5:
                    if (hit(out, me, 1.2f, false, 0.05f) > 0 && foe.hp > 0) {
                        foe.weakened = true;
                        push(out, {Event::Status, me, 1 - me, 0, false, {}, label(1 - me) + " is weakened - its next hits are halved!",
                                   "WEAKENED"});
                    }
                    break;
                default: {
                    const int amount = heal(20);
                    self.guard = 1;
                    push(out, {Event::Heal, me, me, amount, false, {}, who + " raises the royal guard and recovers " + std::to_string(amount) + " HP.",
                               "GUARD"});
                    break;
                }
            }
            break;
    }
    self.weakened = false;  // Dominate covers only the weakened fighter's next action
    for (int k = 0; k < 2; ++k) {
        const bool used = (k == 0 && c == Command::Skill) || (k == 1 && c == Command::Skill2);
        if (used) self.cooldown[k] = kCooldown;
        else if (self.cooldown[k] > 0) --self.cooldown[k];
    }
    turn_ = 1 - turn_;
    ++turns_;
    if (turns_ == kFatigueAfter + 1 && !over()) push(out, {Event::Announce, me, me, 0, false, {}, "Both fighters tire - every blow hits harder!"});
    return out;
}

Command Battle::aiChoice() {
    const Fighter& self = f_[turn_];
    const float hpFrac = static_cast<float>(self.hp) / maxHp(self.type);
    const bool ready1 = canUse(Command::Skill), ready2 = canUse(Command::Skill2);
    switch (self.type) {
        case 1:  // Pawn: rally when hurt, otherwise gamble on the strike
            if (ready2 && !self.empowered && hpFrac < 0.6f) return Command::Skill2;
            if (ready1 && (self.empowered || roll() < 0.5f)) return Command::Skill;
            break;
        case 2:  // Knight: leap in, feint now and then
            if (ready1) return Command::Skill;
            if (ready2 && roll() < 0.5f) return Command::Skill2;
            break;
        case 3:  // Bishop: heal when hurt, otherwise drain
            if (ready1 && hpFrac < 0.5f) return Command::Skill;
            if (ready2) return Command::Skill2;
            break;
        case 4:  // Rook: ram or brace
            if (ready2 && roll() < 0.5f) return Command::Skill2;
            if (ready1 && roll() < 0.5f) return Command::Skill;
            break;
        case 5:  // Queen: storm, then dominate
            if (ready1) return Command::Skill;
            if (ready2) return Command::Skill2;
            break;
        default:  // King: guard when hurt, otherwise decree
            if (ready2 && hpFrac < 0.5f) return Command::Skill2;
            if (ready1) return Command::Skill;
            break;
    }
    if (hpFrac < 0.25f && roll() < 0.3f) return Command::Guard;
    return Command::Attack;
}

namespace {

constexpr Ability kAbilities[7] = {
    {"-", "-"},
    {"Shield Up", "enter the next battle guarded"},
    {"Battle Cry", "next battle: first hit +60%"},
    {"Heal", "adjacent allies regain 25% HP"},
    {"Fortify", "start the next battle with Bulwark"},
    {"Inspire", "adjacent allies: next hit +60%"},
    {"Rest", "heal 30% of max HP"},
};

int typeAt(const std::array<int8_t, 64>& board, int s) { return board[s] < 0 ? -board[s] : board[s]; }

// Allies on the up to eight squares around s.
std::vector<int> adjacentAllies(const std::array<int8_t, 64>& board, int s) {
    std::vector<int> out;
    for (int dr = -1; dr <= 1; ++dr)
        for (int df = -1; df <= 1; ++df) {
            const int r = s / 8 + dr, f = s % 8 + df;
            if ((dr == 0 && df == 0) || r < 0 || r > 7 || f < 0 || f > 7) continue;
            const int t = r * 8 + f;
            if (board[t] != 0 && (board[t] > 0) == (board[s] > 0)) out.push_back(t);
        }
    return out;
}

int healAmount(const std::array<int8_t, 64>& board, const Units& units, int s, int percent) {
    return std::min(units[s].dmg, maxHp(typeAt(board, s)) * percent / 100);
}

}  // namespace

const Ability& abilityOf(int type) { return kAbilities[std::clamp(type, 0, 6)]; }

bool abilityUseful(const std::array<int8_t, 64>& board, const Units& units, int s, int ply) {
    if (board[s] == 0 || abilityWait(units[s], ply) > 0) return false;
    const Unit& u = units[s];
    switch (typeAt(board, s)) {
        case 1: return !(u.buffs & kShield);
        case 2: return !(u.buffs & kEmpower);
        case 3:
            for (int t : adjacentAllies(board, s))
                if (units[t].dmg > 0) return true;
            return false;
        case 4: return !(u.buffs & kBulwark);
        case 5:
            for (int t : adjacentAllies(board, s))
                if (!(units[t].buffs & kEmpower)) return true;
            return false;
        case 6: return u.dmg > 0;
        default: return false;
    }
}

std::vector<AbilityEffect> useAbility(const std::array<int8_t, 64>& board, Units& units, int s, int ply) {
    std::vector<AbilityEffect> out;
    auto heal = [&](int t, int percent) {
        const int amount = healAmount(board, units, t, percent);
        if (amount <= 0) return;
        units[t].dmg -= amount;
        out.push_back({t, amount, 0});
    };
    auto buff = [&](int t, uint8_t b) {
        if (units[t].buffs & b) return;
        units[t].buffs |= b;
        out.push_back({t, 0, b});
    };
    switch (typeAt(board, s)) {
        case 1: buff(s, kShield); break;
        case 2: buff(s, kEmpower); break;
        case 3:
            for (int t : adjacentAllies(board, s)) heal(t, 25);
            break;
        case 4: buff(s, kBulwark); break;
        case 5:
            for (int t : adjacentAllies(board, s)) buff(t, kEmpower);
            break;
        case 6: heal(s, 30); break;
        default: return out;
    }
    units[s].readyAt = abilityReadyAt(ply);
    return out;
}

}  // namespace battle
