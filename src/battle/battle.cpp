#include "battle/battle.hpp"

#include <algorithm>
#include <cmath>

namespace battle {

namespace {

constexpr Stats kStats[7] = {
    {"?", 1, 1, 1, 1, "-", "-", "-", "-", "-", "-"},
    {"Pawn", 40, 10, 4, 5, "Valiant Strike", "2.2x power, 35% to miss", "Rally", "heal 15%, next hit +60%",
     "En Passant", "2x, ignores defense, never misses"},
    {"Knight", 60, 13, 6, 9, "Leap Strike", "1.3x, ignores defense, bleeds", "Feint", "75% dodge vs the next hit",
     "Fork", "two piercing hits, may stun"},
    {"Bishop", 55, 12, 5, 7, "Mend", "heal 40% of max HP", "Drain", "1.0x hit, heal half the damage",
     "Holy Fire", "1.4x, burns; you regenerate"},
    {"Rook", 80, 14, 9, 3, "Bulwark", "block 75% of next hit, strike back", "Ram", "1.8x power, 20% miss, may stun",
     "Siege", "2.4x, ignores defense, exposes"},
    {"Queen", 95, 17, 8, 7, "Royal Storm", "three hits at 0.6x power", "Dominate", "1.2x hit, enemy's next hits halved",
     "Checkmate", "four hits, weakens and exposes"},
    {"King", 100, 12, 10, 4, "Decree", "1.5x power, exposes the enemy", "Royal Guard", "heal 20% and guard",
     "Divine Right", "cleanse, heal 30%, regen, haste"},
};

constexpr float kCritChance = 0.12f;
constexpr float kCritMult = 1.6f;
constexpr int kFatigueAfter = 12;  // turns before fatigue sets in
constexpr float kFatiguePerTurn = 0.15f;
constexpr float kRallyMult = 1.6f;
constexpr float kFeintMiss = 0.75f;
constexpr float kAtkDownMult = 0.5f;
constexpr float kDefDownMult = 1.35f;
constexpr float kHasteMiss = 0.20f;
constexpr int kBurnPercent = 6, kBleedPercent = 10, kRegenPercent = 8;

const char* const kStatusNames[kStatusCount] = {"BURN", "BLEED", "REGEN", "STUNNED", "WEAKENED", "EXPOSED", "HASTE"};
const char* const kStatusText[kStatusCount] = {"is set ablaze!",
                                               "is bleeding!",
                                               "begins to regenerate!",
                                               "is stunned - it will lose its next action!",
                                               "is weakened - its hits are halved!",
                                               "is exposed - it takes more damage!",
                                               "is hasted - it is harder to hit!"};

}  // namespace

const Stats& statsOf(int type) { return kStats[std::clamp(type, 0, 6)]; }

const char* statusName(Status s) { return kStatusNames[std::clamp(static_cast<int>(s), 0, kStatusCount - 1)]; }

const char* curseName(uint8_t bit) {
    switch (bit) {
        case kCurseBurn: return "BURN";
        case kCurseBleed: return "BLEED";
        case kCurseExposed: return "EXPOSED";
        case kCurseWeak: return "WEAKENED";
        default: return "?";
    }
}

Battle::Battle(int attackerType, bool attackerWhite, int attackerHp, int defenderType, int defenderHp, uint32_t seed)
    : rng_(seed), aiRng_(seed ^ 0xA5A5A5A5u) {
    f_[0].type = attackerType;
    f_[0].white = attackerWhite;
    f_[0].hp = std::clamp(attackerHp, 1, maxHp(attackerType));
    f_[1].type = defenderType;
    f_[1].white = !attackerWhite;
    f_[1].hp = std::clamp(defenderHp, 1, maxHp(defenderType));
    // The capturing piece always strikes first; speed only affects dodging.
}

void Battle::curse(int i, uint8_t curses) {
    auto set = [&](Status s, int turns) { f_[i].status[static_cast<int>(s)] = std::max(f_[i].status[static_cast<int>(s)], turns); };
    if (curses & kCurseBurn) set(Status::Burn, 3);
    if (curses & kCurseBleed) set(Status::Bleed, 2);
    if (curses & kCurseExposed) set(Status::DefDown, 2);
    if (curses & kCurseWeak) set(Status::AtkDown, 2);
}

bool Battle::applyStatus(std::vector<Event>& out, int actor, int target, Status s, int turns) {
    Fighter& t = f_[target];
    if (t.hp <= 0) return false;
    if (s == Status::Stun && t.stunnedLast) return false;  // no stun-lock
    int& left = t.status[static_cast<int>(s)];
    left = std::max(left, turns);
    const int k = static_cast<int>(s);
    push(out, {Event::Status, actor, target, 0, false, {}, label(target) + " " + kStatusText[k], kStatusNames[k]});
    return true;
}

std::string Battle::label(int i) const {
    return std::string(f_[i].white ? "White " : "Black ") + statsOf(f_[i].type).name;
}

void Battle::push(std::vector<Event>& out, Event e) const {
    e.hp[0] = f_[0].hp;
    e.hp[1] = f_[1].hp;
    e.move = move_;
    out.push_back(std::move(e));
}

int Battle::hit(std::vector<Event>& out, int actor, float power, bool pierce, float missChance, bool sure) {
    const int target = 1 - actor;
    if (f_[target].hp <= 0) return 0;
    const Stats& a = statsOf(f_[actor].type);
    const Stats& d = statsOf(f_[target].type);
    if (f_[actor].empowered) {
        power *= kRallyMult;
        f_[actor].empowered = false;
    }
    if (f_[actor].status[static_cast<int>(Status::AtkDown)] > 0) power *= kAtkDownMult;
    if (!sure) {
        // Faster targets dodge a little more often.
        missChance += std::max(0, d.spd - a.spd) * 0.02f;
        if (f_[target].status[static_cast<int>(Status::Haste)] > 0) missChance += kHasteMiss;
        if (f_[target].evading) {
            missChance = std::max(missChance, kFeintMiss);
            f_[target].evading = false;
        }
        if (roll() < missChance) {
            push(out, {Event::Miss, actor, target, 0, false, {}, label(target) + " dodges!"});
            return 0;
        }
    }
    const bool crit = roll() < kCritChance;
    float raw = a.atk * power * (0.9f + 0.2f * roll());
    // Fatigue: long fights hit ever harder, so two healers cannot stall forever.
    raw *= 1.0f + kFatiguePerTurn * std::max(0, turns_ - kFatigueAfter);
    if (crit) raw *= kCritMult;
    else if (!pierce) raw -= d.def * 0.5f;
    if (f_[target].status[static_cast<int>(Status::DefDown)] > 0) raw *= kDefDownMult;
    const int guard = sure ? 0 : f_[target].guard;
    float taken = raw;
    if (guard == 1) taken *= 0.5f;
    if (guard == 2) taken *= 0.25f;
    const int dmg = std::min(f_[target].hp, std::max(1, static_cast<int>(std::lround(taken))));
    f_[target].hp -= dmg;
    gain(actor, 3 * dmg);
    gain(target, 2 * dmg);
    std::string text = (crit ? "Critical! " : "") + label(target) + " takes " + std::to_string(dmg) + " damage";
    push(out, {Event::Hit, actor, target, dmg, crit, {}, text + (f_[target].hp == 0 ? " and falls!" : "!")});
    if (guard == 2) {
        f_[target].guard = 0;  // Bulwark answers one blow
        if (f_[target].hp > 0) {
            const int back = std::max(1, static_cast<int>(std::lround(std::max(raw, 1.0f) * 0.5f)));
            const int lost = std::min(back, f_[actor].hp);
            f_[actor].hp -= lost;
            gain(target, 3 * lost);
            gain(actor, 2 * lost);
            push(out, {Event::Hit, target, actor, back, false, {},
                       label(target) + " strikes back for " + std::to_string(back) + (f_[actor].hp == 0 ? " - KO!" : "!")});
        }
    }
    return dmg;
}

std::vector<Event> Battle::act(Command c) {
    std::vector<Event> out;
    if (over()) return out;
    move_ = -1;
    const int me = turn_;
    Fighter& self = f_[me];
    auto st_ = [&](Fighter& f, Status s) -> int& { return f.status[static_cast<int>(s)]; };
    const std::string who = label(me);
    // Guard and Feint last until the fighter's own next turn. The attacker's first turn keeps a guard
    // from prepare(), so it still covers the defender's first reply.
    if (turns_ > 0) self.guard = 0;
    self.evading = false;

    // Damage over time and regeneration; not affected by fatigue, guard or crits.
    for (Status s : {Status::Burn, Status::Bleed}) {
        if (st_(self, s) <= 0) continue;
        const int percent = s == Status::Burn ? kBurnPercent : kBleedPercent;
        const int dmg = std::min(self.hp, std::max(1, maxHp(self.type) * percent / 100));
        self.hp -= dmg;
        gain(me, 2 * dmg);
        push(out, {Event::Tick, me, me, dmg, false, {},
                   who + " takes " + std::to_string(dmg) + (s == Status::Burn ? " burn" : " bleed") + " damage" +
                       (self.hp == 0 ? " and falls!" : "!"),
                   statusName(s)});
        if (self.hp <= 0) {
            turn_ = 1 - turn_;
            ++turns_;
            return out;
        }
    }
    if (st_(self, Status::Regen) > 0) {
        const int amount = std::min(maxHp(self.type) - self.hp, std::max(1, maxHp(self.type) * kRegenPercent / 100));
        self.hp += amount;
        if (amount > 0)
            push(out, {Event::Heal, me, me, amount, false, {}, who + " regenerates " + std::to_string(amount) + " HP.", "REGEN"});
    }
    for (Status s : {Status::Burn, Status::Bleed, Status::Regen, Status::DefDown, Status::Haste})
        if (st_(self, s) > 0) --st_(self, s);

    auto endTurn = [&](Command used) {
        for (int k = 0; k < 2; ++k) {
            const bool usedSkill = (k == 0 && used == Command::Skill) || (k == 1 && used == Command::Skill2);
            if (usedSkill) self.cooldown[k] = kCooldown;
            else if (self.cooldown[k] > 0) --self.cooldown[k];
        }
        if (used != Command::Ultimate) gain(me, 10);
        // AtkDown counts the afflicted fighter's own actions, so Dominate covers exactly the next one.
        if (st_(self, Status::AtkDown) > 0) --st_(self, Status::AtkDown);
        turn_ = 1 - turn_;
        ++turns_;
        move_ = -1;
        if (turns_ == kFatigueAfter + 1 && !over()) push(out, {Event::Announce, me, me, 0, false, {}, "Both fighters tire - every blow hits harder!"});
    };

    if (st_(self, Status::Stun) > 0) {
        st_(self, Status::Stun) = 0;
        self.stunnedLast = true;
        push(out, {Event::Status, me, me, 0, false, {}, who + " is stunned and can't act!", "STUNNED"});
        endTurn(Command::Attack);
        return out;
    }
    self.stunnedLast = false;

    if (!canUse(c)) c = Command::Attack;
    move_ = static_cast<int8_t>(c);
    const Stats& st = statsOf(self.type);
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
                case 2:
                    if (hit(out, me, 1.3f, true, 0.05f) > 0) applyStatus(out, me, 1 - me, Status::Bleed, 2);
                    break;
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
                default:
                    if (hit(out, me, 1.5f, false, 0.05f) > 0) applyStatus(out, me, 1 - me, Status::DefDown, 2);
                    break;
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
                case 4:
                    if (hit(out, me, 1.8f, false, 0.20f) > 0 && roll() < 0.35f) applyStatus(out, me, 1 - me, Status::Stun, 1);
                    break;
                case 5:
                    if (hit(out, me, 1.2f, false, 0.05f) > 0) applyStatus(out, me, 1 - me, Status::AtkDown, 1);
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
        case Command::Ultimate:
            push(out, {Event::Announce, me, 1 - me, 0, false, {}, who + " unleashes " + st.ultimate + "!"});
            switch (self.type) {
                case 1:
                    if (hit(out, me, 2.0f, true, 0.0f, true) > 0) applyStatus(out, me, 1 - me, Status::Bleed, 2);
                    break;
                case 2: {
                    int landed = 0;
                    for (int i = 0; i < 2; ++i) landed += hit(out, me, 1.0f, true, 0.05f) > 0;
                    if (landed == 2) applyStatus(out, me, 1 - me, Status::Stun, 1);
                    break;
                }
                case 3:
                    if (hit(out, me, 1.4f, false, 0.05f) > 0) applyStatus(out, me, 1 - me, Status::Burn, 3);
                    applyStatus(out, me, me, Status::Regen, 3);
                    break;
                case 4:
                    if (hit(out, me, 2.4f, true, 0.10f) > 0) applyStatus(out, me, 1 - me, Status::DefDown, 3);
                    break;
                case 5: {
                    int landed = 0;
                    for (int i = 0; i < 4; ++i) landed += hit(out, me, 0.7f, false, 0.05f) > 0;
                    if (landed > 0) {
                        applyStatus(out, me, 1 - me, Status::AtkDown, 2);
                        applyStatus(out, me, 1 - me, Status::DefDown, 2);
                    }
                    break;
                }
                default: {
                    for (Status s : {Status::Burn, Status::Bleed, Status::Stun, Status::AtkDown, Status::DefDown}) st_(self, s) = 0;
                    const int amount = heal(30);
                    push(out, {Event::Heal, me, me, amount, false, {}, who + " is cleansed and recovers " + std::to_string(amount) + " HP.", "CLEANSED"});
                    applyStatus(out, me, me, Status::Regen, 3);
                    applyStatus(out, me, me, Status::Haste, 2);
                    break;
                }
            }
            self.charge = 0;  // after the hits, so their charge gain doesn't carry over
            break;
    }
    endTurn(c);
    return out;
}

Command Battle::aiChoice() {
    const Fighter& self = f_[turn_];
    const float hpFrac = static_cast<float>(self.hp) / maxHp(self.type);
    auto has = [&](Status s) { return self.status[static_cast<int>(s)] > 0; };
    const bool dot = has(Status::Burn) || has(Status::Bleed);
    if (canUse(Command::Ultimate)) {
        const bool debuffed = dot || has(Status::AtkDown) || has(Status::DefDown);
        if (self.type != 6 || hpFrac < 0.6f || debuffed) return Command::Ultimate;
    }
    const bool ready1 = canUse(Command::Skill), ready2 = canUse(Command::Skill2);
    switch (self.type) {
        case 1:  // Pawn: rally when hurt, otherwise gamble on the strike
            if (ready2 && (!self.empowered || dot) && hpFrac < 0.6f) return Command::Skill2;
            if (ready1 && (self.empowered || unit(aiRng_) < 0.5f)) return Command::Skill;
            break;
        case 2:  // Knight: leap in, feint now and then
            if (ready1) return Command::Skill;
            if (ready2 && unit(aiRng_) < 0.5f) return Command::Skill2;
            break;
        case 3:  // Bishop: heal when hurt, otherwise drain
            if (ready1 && (hpFrac < 0.5f || (dot && hpFrac < 0.6f))) return Command::Skill;
            if (ready2) return Command::Skill2;
            break;
        case 4:  // Rook: ram or brace
            if (ready2 && unit(aiRng_) < 0.5f) return Command::Skill2;
            if (ready1 && unit(aiRng_) < 0.5f) return Command::Skill;
            break;
        case 5:  // Queen: storm, then dominate
            if (ready1) return Command::Skill;
            if (ready2) return Command::Skill2;
            break;
        default:  // King: guard when hurt, otherwise decree
            if (ready2 && (hpFrac < 0.5f || (dot && hpFrac < 0.6f))) return Command::Skill2;
            if (ready1) return Command::Skill;
            break;
    }
    if (hpFrac < 0.25f && unit(aiRng_) < 0.3f) return Command::Guard;
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

constexpr Ability kStrikes[7] = {
    {"-", "-"},
    {"Pike Jab", "forward diagonals: 6 dmg, bleed"},
    {"Harass", "knight-jump foes: 4 dmg, exposed"},
    {"Smite", "first foe per diagonal: 10, burn"},
    {"Cannon", "first foe per line: 12 dmg"},
    {"Terror", "foes within 2 squares: weakened"},
    {"Condemn", "adjacent foes: 8 dmg, burn"},
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

const Ability& abilityOf(int type, int which) {
    const int t = std::clamp(type, 0, 6);
    return which ? kStrikes[t] : kAbilities[t];
}

namespace {

struct Hit {
    int square;
    int amount;
    uint8_t curse;
};

// Enemies a strike from s would reach, before cooldown and caps are considered.
std::vector<Hit> strikeTargets(const std::array<int8_t, 64>& board, int s) {
    std::vector<Hit> out;
    const bool white = board[s] > 0;
    const int r0 = s / 8, f0 = s % 8;
    auto enemy = [&](int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8 && board[r * 8 + f] != 0 && (board[r * 8 + f] > 0) != white; };
    auto add = [&](int r, int f, int amount, uint8_t curse) { out.push_back({r * 8 + f, amount, curse}); };
    // First piece met along each direction; only enemies are hit.
    auto walk = [&](const int (&dirs)[4][2], int amount, uint8_t curse) {
        for (const auto& d : dirs)
            for (int r = r0 + d[0], f = f0 + d[1]; r >= 0 && r < 8 && f >= 0 && f < 8; r += d[0], f += d[1]) {
                if (board[r * 8 + f] == 0) continue;
                if (enemy(r, f)) add(r, f, amount, curse);
                break;
            }
    };
    static const int kDiag[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    static const int kLine[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static const int kJump[8][2] = {{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
    switch (typeAt(board, s)) {
        case 1:  // white pawns advance towards higher ranks
            for (int df : {-1, 1})
                if (enemy(r0 + (white ? 1 : -1), f0 + df)) add(r0 + (white ? 1 : -1), f0 + df, 6, kCurseBleed);
            break;
        case 2:
            for (const auto& j : kJump)
                if (enemy(r0 + j[0], f0 + j[1])) add(r0 + j[0], f0 + j[1], 4, kCurseExposed);
            break;
        case 3: walk(kDiag, 10, kCurseBurn); break;
        case 4: walk(kLine, 12, 0); break;
        case 5:
            for (int dr = -2; dr <= 2; ++dr)
                for (int df = -2; df <= 2; ++df)
                    if ((dr || df) && enemy(r0 + dr, f0 + df)) add(r0 + dr, f0 + df, 0, kCurseWeak);
            break;
        case 6:
            for (int dr = -1; dr <= 1; ++dr)
                for (int df = -1; df <= 1; ++df)
                    if ((dr || df) && enemy(r0 + dr, f0 + df)) add(r0 + dr, f0 + df, 8, kCurseBurn);
            break;
        default: break;
    }
    return out;
}

// Board damage never kills: a piece keeps at least 1 HP.
int strikeDamage(const std::array<int8_t, 64>& board, const Units& units, const Hit& h) {
    const int cap = maxHp(typeAt(board, h.square)) - 1;
    return std::max(0, std::min(cap, units[h.square].dmg + h.amount) - units[h.square].dmg);
}

}  // namespace

bool abilityUseful(const std::array<int8_t, 64>& board, const Units& units, int s, int ply, int which) {
    if (board[s] == 0 || abilityWait(units[s], ply, which) > 0) return false;
    if (which) {
        for (const Hit& h : strikeTargets(board, s))
            if (strikeDamage(board, units, h) > 0 || (h.curse & ~units[h.square].curses)) return true;
        return false;
    }
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

std::vector<AbilityEffect> useAbility(const std::array<int8_t, 64>& board, Units& units, int s, int ply, int which) {
    std::vector<AbilityEffect> out;
    if (which) {
        for (const Hit& h : strikeTargets(board, s)) {
            const int dmg = strikeDamage(board, units, h);
            const uint8_t fresh = h.curse & ~units[h.square].curses;
            units[h.square].dmg += dmg;
            units[h.square].curses |= h.curse;
            if (dmg > 0 || fresh) out.push_back({h.square, 0, 0, dmg, fresh});
        }
        units[s].readyAt2 = abilityReadyAt(ply, 1);
        return out;
    }
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
