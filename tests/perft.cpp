// Move-generator check against published perft node counts
// (https://www.chessprogramming.org/Perft_Results).
#include "chess.hpp"

#include <cstdint>
#include <cstdio>

using chess::Position;

static uint64_t perft(const Position& p, int depth) {
    if (depth == 0) return 1;
    auto moves = p.legalMoves();
    if (depth == 1) return moves.size();
    uint64_t n = 0;
    for (const auto& m : moves) n += perft(p.apply(m), depth - 1);
    return n;
}

int main() {
    struct Case { const char* name; const char* fen; int depth; uint64_t expected; };
    const Case cases[] = {
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 3, 97862},
        {"pos3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 4, 43238},
        {"pos4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467},
        {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
    };
    int failures = 0;
    for (const auto& c : cases) {
        uint64_t got = perft(Position::fromFen(c.fen), c.depth);
        bool ok = got == c.expected;
        failures += !ok;
        std::printf("%-9s depth %d: %llu (expected %llu) %s\n", c.name, c.depth, (unsigned long long)got,
                    (unsigned long long)c.expected, ok ? "ok" : "FAIL");
    }
    return failures == 0 ? 0 : 1;
}
