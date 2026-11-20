// Battle-mode rules: fights end, HP stays in range, skills cool down, and stronger pieces win more.
#include <algorithm>
#include <cstdio>

#include "battle/battle.hpp"
#include "chess/chess.hpp"
#include "check.hpp"

using namespace battle;

namespace {

// Plays a battle out with both sides on the AI; returns the winner, or -1 if it ran too long.
int fight(int a, int aHp, int d, int dHp, uint32_t seed, bool* hpInRange = nullptr) {
    Battle b(a, true, aHp, d, dHp, seed);
    for (int turns = 0; turns < 500 && !b.over(); ++turns) {
        for (const Event& e : b.act(b.aiChoice()))
            for (int i = 0; i < 2; ++i)
                if (hpInRange && (e.hp[i] < 0 || e.hp[i] > maxHp(b.fighter(i).type))) *hpInRange = false;
    }
    return b.over() ? b.winner() : -1;
}

constexpr int kBurn = static_cast<int>(Status::Burn), kStun = static_cast<int>(Status::Stun), kRegen = static_cast<int>(Status::Regen);
constexpr int kAtkDown = static_cast<int>(Status::AtkDown), kDefDown = static_cast<int>(Status::DefDown), kHaste = static_cast<int>(Status::Haste);
constexpr int kBleed = static_cast<int>(Status::Bleed);

// Both sides guard (no damage, no board effects) until the attacker's Ultimate is ready on its turn.
void chargeUp(Battle& b) {
    for (int i = 0; i < 40 && !(b.turn() == 0 && b.canUse(Command::Ultimate)); ++i) b.act(Command::Guard);
}

bool hasTag(const std::vector<Event>& evs, const char* tag, int target) {
    for (const Event& e : evs)
        if (e.tag && std::string(e.tag) == tag && e.target == target) return true;
    return false;
}

}  // namespace

int main() {
    bool allEnd = true, inRange = true;
    for (int a = chess::PAWN; a <= chess::KING; ++a)
        for (int d = chess::PAWN; d <= chess::KING; ++d)
            for (uint32_t seed = 0; seed < 50; ++seed)
                if (fight(a, maxHp(a), d, maxHp(d), seed, &inRange) < 0) allEnd = false;
    expect(allEnd, "every pairing ends within 500 turns");
    expect(inRange, "HP stays between 0 and max");

    int queenWins = 0, pawnVsWounded = 0;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        queenWins += fight(chess::PAWN, maxHp(chess::PAWN), chess::QUEEN, maxHp(chess::QUEEN), seed) == 1;
        pawnVsWounded += fight(chess::PAWN, maxHp(chess::PAWN), chess::QUEEN, 8, seed) == 0;
    }
    expect(queenWins > 180, "a fresh queen beats an attacking pawn most of the time");
    expect(pawnVsWounded > 100, "a pawn usually finishes off a badly wounded queen");

    Battle b(chess::ROOK, true, maxHp(chess::ROOK), chess::KNIGHT, maxHp(chess::KNIGHT), 1);
    expect(b.turn() == 0, "the capturing piece acts first");
    b.act(Command::Skill);
    b.act(Command::Attack);
    expect(!b.canUse(Command::Skill), "a skill cools down after use");
    b.act(Command::Attack);
    b.act(Command::Attack);
    b.act(Command::Attack);
    b.act(Command::Attack);
    expect(b.over() || b.canUse(Command::Skill), "the skill is ready again after the cooldown");

    Battle heal(chess::BISHOP, true, 10, chess::PAWN, maxHp(chess::PAWN), 2);
    heal.act(Command::Skill);
    expect(heal.fighter(0).hp == 10 + maxHp(chess::BISHOP) * 2 / 5, "Mend restores 40% of max HP");
    Battle capped(chess::BISHOP, true, maxHp(chess::BISHOP) - 3, chess::PAWN, maxHp(chess::PAWN), 2);
    capped.act(Command::Skill);
    expect(capped.fighter(0).hp == maxHp(chess::BISHOP), "healing never goes above max HP");

    Battle twoSkills(chess::QUEEN, true, maxHp(chess::QUEEN), chess::ROOK, maxHp(chess::ROOK), 3);
    twoSkills.act(Command::Skill);
    twoSkills.act(Command::Attack);
    expect(!twoSkills.canUse(Command::Skill) && twoSkills.canUse(Command::Skill2), "each skill has its own cooldown");

    Battle rally(chess::PAWN, true, 10, chess::ROOK, maxHp(chess::ROOK), 4);
    rally.act(Command::Skill2);
    expect(rally.fighter(0).hp == 10 + maxHp(chess::PAWN) * 15 / 100 && rally.fighter(0).empowered,
           "Rally heals 15% and empowers the next hit");

    Battle royal(chess::KING, true, 30, chess::PAWN, maxHp(chess::PAWN), 5);
    royal.act(Command::Skill2);
    expect(royal.fighter(0).hp == 30 + maxHp(chess::KING) / 5 && royal.fighter(0).guard == 1, "Royal Guard heals 20% and guards");

    int feintDodges = 0, drainOk = 0, drainHits = 0, weakenedOk = 0, weakenedHits = 0;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Battle feint(chess::KNIGHT, true, maxHp(chess::KNIGHT), chess::QUEEN, maxHp(chess::QUEEN), seed);
        feint.act(Command::Skill2);
        const auto reply = feint.act(Command::Attack);
        feintDodges += reply.size() > 1 && reply[1].kind == Event::Miss;

        Battle drain(chess::BISHOP, true, 10, chess::ROOK, maxHp(chess::ROOK), seed);
        drain.act(Command::Skill2);
        const int dealt = maxHp(chess::ROOK) - drain.fighter(1).hp;
        if (dealt > 0) {
            ++drainHits;
            drainOk += drain.fighter(0).hp == 10 + std::max(1, dealt / 2);
        }

        Battle dominate(chess::QUEEN, true, maxHp(chess::QUEEN), chess::ROOK, maxHp(chess::ROOK), seed);
        dominate.act(Command::Skill2);
        if (dominate.fighter(1).hp < maxHp(chess::ROOK)) {
            ++weakenedHits;
            const bool marked = dominate.fighter(1).status[static_cast<int>(Status::AtkDown)] > 0;
            dominate.act(Command::Attack);
            weakenedOk += marked && dominate.fighter(1).status[static_cast<int>(Status::AtkDown)] == 0;
        }
    }
    expect(feintDodges > 130, "Feint makes the next enemy hit miss most of the time");
    expect(drainHits > 150 && drainOk == drainHits, "Drain heals half the damage it deals");
    expect(weakenedHits > 150 && weakenedOk == weakenedHits, "Dominate weakens the target for its next action only");

    // Board buffs carried into a battle.
    Battle shielded(chess::PAWN, true, maxHp(chess::PAWN), chess::ROOK, maxHp(chess::ROOK), 6);
    shielded.prepare(0, 1, true);
    shielded.act(Command::Attack);
    expect(shielded.fighter(0).guard == 1, "a prepared guard on the attacker survives its first action");
    expect(!shielded.fighter(0).empowered, "a prepared empowered hit is used by the first attack");
    shielded.act(Command::Attack);
    shielded.act(Command::Attack);
    expect(shielded.fighter(0).guard == 0, "the prepared guard ends at the attacker's next turn");
    Battle fortified(chess::PAWN, true, maxHp(chess::PAWN), chess::ROOK, maxHp(chess::ROOK), 7);
    fortified.prepare(1, 2, false);
    const auto opening = fortified.act(Command::Attack);
    bool counter = false;
    for (const Event& e : opening) counter = counter || (e.kind == Event::Hit && e.actor == 1);
    expect(counter || opening.back().kind == Event::Miss, "a fortified defender strikes back at the first hit");

    // Board abilities.
    std::array<int8_t, 64> board{};
    Units units{};
    board[27] = chess::BISHOP;   // d4
    board[28] = chess::KNIGHT;   // e4, adjacent ally
    board[36] = -chess::ROOK;    // e5, adjacent enemy
    board[0] = chess::KING;      // a1, not adjacent
    units[28].dmg = 50;
    units[36].dmg = 40;
    units[0].dmg = 10;
    expect(abilityUseful(board, units, 27, 0), "Heal is useful next to a wounded ally");
    const auto healed = useAbility(board, units, 27, 0);
    expect(units[28].dmg == 50 - maxHp(chess::KNIGHT) / 4 && units[36].dmg == 40 && units[0].dmg == 10 && healed.size() == 1,
           "Heal restores 25% to adjacent allies only");
    expect(abilityWait(units[27], 2) == 3 && abilityWait(units[27], 6) == 1 && abilityWait(units[27], 8) == 0,
           "an ability is unavailable for three of the side's turns");
    expect(!abilityUseful(board, units, 27, 2), "an ability can't be used while cooling down");
    units[0].dmg = 5;
    useAbility(board, units, 0, 0);
    expect(units[0].dmg == 0, "Rest never heals above max HP");
    expect(!abilityUseful(board, units, 0, 100), "Rest has no effect on an unhurt king");
    board[35] = chess::QUEEN;  // d5, next to the bishop and the knight
    useAbility(board, units, 35, 0);
    expect((units[27].buffs & kEmpower) && (units[28].buffs & kEmpower) && !(units[36].buffs & kEmpower),
           "Inspire empowers adjacent allies only");
    board[8] = chess::PAWN;
    useAbility(board, units, 8, 0);
    expect(units[8].buffs == kShield && !abilityUseful(board, units, 8, 100), "Shield Up only stacks once");

    // ---- Status effects ----
    {
        // curse() burns the defender: one tick at the start of each of its turns, three in all.
        Battle burn(chess::ROOK, true, maxHp(chess::ROOK), chess::KNIGHT, maxHp(chess::KNIGHT), 11);
        burn.curse(1, kCurseBurn);
        int ticks = 0;
        bool lowered = true;
        for (int i = 0; i < 12; ++i) {
            const int before = burn.fighter(1).hp;
            const auto evs = burn.act(Command::Guard);
            if (burn.over()) break;
            if (!evs.empty() && evs[0].kind == Event::Tick) {
                ++ticks;
                lowered = lowered && evs[0].target == 1 && evs[0].amount == 3 && evs[0].hp[1] == before - 3;
            }
        }
        expect(ticks == 3 && lowered, "burn ticks 6% max HP at the afflicted fighter's turn start, three times");
        expect(burn.fighter(1).status[kBurn] == 0, "burn expires after its ticks");

        Battle first(chess::KNIGHT, true, maxHp(chess::KNIGHT), chess::ROOK, maxHp(chess::ROOK), 12);
        first.curse(0, kCurseBurn);
        const auto opening2 = first.act(Command::Guard);
        expect(!opening2.empty() && opening2[0].kind == Event::Tick && opening2[0].target == 0 && std::string(opening2[0].tag) == "BURN",
               "a cursed fighter burns on its very first turn");
    }
    {
        // Regen: 8% max HP at turn start, capped at max.
        bool regenOk = true, seen = false;
        for (uint32_t seed = 0; seed < 30; ++seed) {
            Battle r(chess::BISHOP, true, 20, chess::PAWN, maxHp(chess::PAWN), seed);
            chargeUp(r);
            r.act(Command::Ultimate);
            if (r.over()) continue;
            r.act(Command::Attack);  // the pawn wounds the bishop
            if (r.over()) continue;
            const int before = r.fighter(0).hp;
            const auto evs = r.act(Command::Guard);
            for (const Event& e : evs)
                if (e.tag && std::string(e.tag) == "REGEN" && e.kind == Event::Heal) {
                    seen = true;
                    regenOk = regenOk && e.amount == std::min(maxHp(chess::BISHOP) - before, maxHp(chess::BISHOP) * 8 / 100) &&
                              e.hp[0] <= maxHp(chess::BISHOP);
                }
        }
        expect(seen && regenOk, "Regen heals 8% max HP and never passes max");
    }
    {
        // Stun (Rook Ram): the stunned fighter loses exactly one action and can't be re-stunned straight after.
        int stuns = 0;
        bool ok = true;
        for (uint32_t seed = 0; seed < 100; ++seed) {
            Battle b(chess::ROOK, true, maxHp(chess::ROOK), chess::KING, maxHp(chess::KING), seed);
            b.act(Command::Skill2);
            if (b.fighter(1).status[kStun] == 0) continue;
            ++stuns;
            const int hpBefore = b.fighter(0).hp;
            const auto skipped = b.act(Command::Attack);
            ok = ok && hasTag(skipped, "STUNNED", 1) && b.fighter(0).hp == hpBefore && b.turn() == 0 && b.fighter(1).stunnedLast &&
                 b.fighter(1).status[kStun] == 0;
            b.act(Command::Attack);
            b.act(Command::Attack);  // the king's first free action clears stunnedLast
            ok = ok && !b.fighter(1).stunnedLast;
        }
        expect(stuns > 10 && ok, "Stun skips exactly one action and sets stunnedLast until the next free action");

        // Whatever the sources, a fighter is never skipped twice in a row.
        bool noLock = true;
        for (int a = chess::PAWN; a <= chess::KING; ++a)
            for (int d = chess::PAWN; d <= chess::KING; ++d)
                for (uint32_t seed = 0; seed < 30; ++seed) {
                    Battle b(a, true, maxHp(a), d, maxHp(d), seed);
                    bool skippedLast[2] = {false, false};
                    for (int t = 0; t < 500 && !b.over(); ++t) {
                        const int who = b.turn();
                        const auto evs = b.act(b.aiChoice());
                        const bool skipped = hasTag(evs, "STUNNED", who);
                        if (skipped && skippedLast[who]) noLock = false;
                        skippedLast[who] = skipped;
                    }
                }
        expect(noLock, "no stun-lock: never two skipped turns in a row");
    }

    // ---- Ultimates ----
    {
        Battle fresh(chess::QUEEN, true, maxHp(chess::QUEEN), chess::ROOK, maxHp(chess::ROOK), 21);
        expect(!fresh.canUse(Command::Ultimate), "the Ultimate is locked at 0 charge");
        const auto evs = fresh.act(Command::Ultimate);
        bool unleashed = false;
        for (const Event& e : evs) unleashed = unleashed || e.text.find("unleashes") != std::string::npos;
        expect(!unleashed && !evs.empty() && evs[0].text.find("attacks") != std::string::npos, "an unavailable Ultimate falls back to Attack");

        bool reached = false, resets = true, bounded = true;
        int uses = 0;
        for (int a = chess::PAWN; a <= chess::KING; ++a)
            for (uint32_t seed = 0; seed < 10; ++seed) {
                Battle b(a, true, maxHp(a), chess::KING, maxHp(chess::KING), seed);
                for (int t = 0; t < 500 && !b.over(); ++t) {
                    const int who = b.turn();
                    const auto evs2 = b.act(b.aiChoice());
                    bool used = false;
                    for (const Event& e : evs2) used = used || e.text.find("unleashes") != std::string::npos;
                    if (used) {
                        ++uses;
                        resets = resets && b.fighter(who).charge == 0;
                    }
                    for (int i = 0; i < 2; ++i) {
                        reached = reached || b.fighter(i).charge == kUltimateCharge;
                        bounded = bounded && b.fighter(i).charge >= 0 && b.fighter(i).charge <= kUltimateCharge;
                    }
                }
            }
        expect(reached && uses > 0, "charge reaches 100 in a normal fight");
        expect(resets && bounded, "using the Ultimate resets charge to 0 and charge stays within 0..100");
    }
    {
        bool through = true;
        int tries = 0;
        for (uint32_t seed = 0; seed < 40; ++seed) {
            Battle b(chess::PAWN, true, maxHp(chess::PAWN), chess::KNIGHT, maxHp(chess::KNIGHT), seed);
            chargeUp(b);
            b.act(Command::Guard);
            b.act(Command::Skill2);  // Feint
            const auto evs = b.act(Command::Ultimate);
            bool hitLanded = false, missed = false;
            for (const Event& e : evs) {
                hitLanded = hitLanded || (e.kind == Event::Hit && e.actor == 0);
                missed = missed || e.kind == Event::Miss;
            }
            ++tries;
            through = through && hitLanded && !missed && b.fighter(1).evading && (b.over() || b.fighter(1).status[kBleed] == 2);
        }
        expect(tries > 0 && through, "En Passant always hits through Feint and bleeds");
    }
    {
        Battle b(chess::KING, true, maxHp(chess::KING) - 3, chess::PAWN, maxHp(chess::PAWN), 31);
        chargeUp(b);
        b.curse(0, kCurseBurn | kCurseBleed | kCurseExposed | kCurseWeak);
        b.act(Command::Ultimate);
        const Fighter& k = b.fighter(0);
        expect(k.status[kBurn] == 0 && k.status[kBleed] == 0 && k.status[kStun] == 0 && k.status[kAtkDown] == 0 && k.status[kDefDown] == 0,
               "Divine Right clears Burn, Bleed, Stun, AtkDown and DefDown");
        expect(k.status[kRegen] == 3 && k.status[kHaste] == 2 && k.charge == 0, "Divine Right grants Regen 3 and Haste 2 and spends the charge");
    }

    // ---- Board strikes ----
    {
        std::array<int8_t, 64> bd{};
        Units un{};
        bd[0] = chess::ROOK;           // a1
        bd[3] = -chess::PAWN;          // d1: first on the rank
        bd[5] = -chess::KNIGHT;        // f1: shadowed
        bd[16] = chess::PAWN;          // a3: friendly blocker
        bd[24] = -chess::KING;         // a4: behind the friend
        un[3].dmg = 35;
        const auto fx = useAbility(bd, un, 0, 0, 1);
        expect(fx.size() == 1 && fx[0].square == 3 && fx[0].damage == 4 && un[3].dmg == maxHp(chess::PAWN) - 1,
               "Cannon hits only the first piece in a line and never takes a piece below 1 HP");
        expect(un[5].dmg == 0 && un[24].dmg == 0 && un[16].dmg == 0, "Cannon skips shadowed enemies and allies");
        expect(un[0].readyAt == 0 && un[0].readyAt2 == abilityReadyAt(0, 1) && abilityWait(un[0], 0, 1) == kStrikeCooldown + 1 &&
                   abilityWait(un[0], 0, 0) == 0,
               "the strike cooldown is independent of the support ability's");
        expect(abilityUseful(bd, un, 0, 0, 0) && !abilityUseful(bd, un, 0, 0, 1), "support stays ready while the strike cools down");
        un[0].readyAt = abilityReadyAt(0, 0);
        expect(abilityWait(un[0], 0, 0) == kAbilityCooldown + 1 && abilityWait(un[0], 8, 1) == 1, "each ability tracks its own readiness");
    }
    {
        std::array<int8_t, 64> bd{};
        Units un{};
        bd[28] = chess::PAWN;      // e4 white
        bd[35] = -chess::ROOK;     // d5 forward-left
        bd[37] = -chess::BISHOP;   // f5 forward-right
        bd[19] = -chess::KNIGHT;   // d3 behind: untouched
        bd[20] = -chess::KNIGHT;   // e3
        bd[36] = -chess::KNIGHT;   // e5 straight ahead: untouched
        const auto fx = useAbility(bd, un, 28, 0, 1);
        expect(fx.size() == 2 && un[35].dmg == 6 && un[37].dmg == 6 && (un[35].curses & kCurseBleed) && un[19].dmg == 0 && un[36].dmg == 0 &&
                   un[20].dmg == 0,
               "Pike Jab hits the two forward diagonals for white");
        std::array<int8_t, 64> bb{};
        Units ub{};
        bb[36] = -chess::PAWN;     // e5 black
        bb[27] = chess::ROOK;      // d4
        bb[29] = chess::QUEEN;     // f4
        bb[43] = chess::KNIGHT;    // d6 behind black: untouched
        bb[45] = chess::KNIGHT;    // f6
        useAbility(bb, ub, 36, 0, 1);
        expect(ub[27].dmg == 6 && ub[29].dmg == 6 && ub[43].dmg == 0 && ub[45].dmg == 0, "Pike Jab hits the two forward diagonals for black");
    }
    {
        std::array<int8_t, 64> bd{};
        Units un{};
        bd[27] = chess::QUEEN;
        bd[63] = -chess::PAWN;  // far away
        expect(!abilityUseful(bd, un, 27, 0, 1), "a strike with no enemy in reach is not useful");
        bd[29] = -chess::PAWN;  // two files away
        expect(abilityUseful(bd, un, 27, 0, 1), "Terror is useful against an enemy within two squares");
        useAbility(bd, un, 27, 0, 1);
        expect(un[29].curses == kCurseWeak && un[29].dmg == 0, "Terror only curses");
        un[27].readyAt2 = 0;
        expect(!abilityUseful(bd, un, 27, 0, 1), "a strike is useless once the curse is already present");
    }

    return failures == 0 ? 0 : 1;
}
