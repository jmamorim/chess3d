// Battle-mode rules: fights end, HP stays in range, skills cool down, and stronger pieces win more.
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

    return failures == 0 ? 0 : 1;
}
