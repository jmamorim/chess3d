// Battle-mode rules: fights end, HP stays in range, skills cool down, and stronger pieces win more.
#include <algorithm>
#include <cstdio>

#include "battle.hpp"
#include "chess.hpp"

using namespace battle;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

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
            const bool marked = dominate.fighter(1).weakened;
            dominate.act(Command::Attack);
            weakenedOk += marked && !dominate.fighter(1).weakened;
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

    return failures == 0 ? 0 : 1;
}
