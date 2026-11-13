// Engine sanity checks: evaluation symmetry and a few positions with one clearly right move.
#include <cstdio>

#include "ai.hpp"

using namespace chess;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// Same position with the colours swapped and the board flipped; its evaluation must match.
Position mirrored(const Position& p) {
    Position m = p;
    for (int s = 0; s < 64; ++s) m.sq[(7 - rankOf(s)) * 8 + fileOf(s)] = static_cast<int8_t>(-p.sq[s]);
    m.side = p.side == White ? Black : White;
    m.castling = static_cast<uint8_t>(((p.castling & 3) << 2) | (p.castling >> 2));
    m.ep = p.ep < 0 ? -1 : static_cast<int8_t>((7 - rankOf(p.ep)) * 8 + fileOf(p.ep));
    return m;
}

Move think(const char* fen, int depth, int marginCp = 0) {
    Searcher s;
    s.start(Position::fromFen(fen), {}, SearchLimits{depth, 10000, marginCp});
    while (!s.step(10)) {}
    return s.best();
}

bool is(const Move& m, int from, int to) { return m.from == from && m.to == to; }

}  // namespace

int main() {
    expect(evaluate(Position::start()) == 0, "start position evaluates to 0");
    const Position middle = Position::fromFen("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 0 8");
    expect(evaluate(middle) == evaluate(mirrored(middle)), "evaluation is colour-symmetric");

    // Ra1-a8 is mate on the back rank.
    expect(is(think("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", 3), 0, 56), "finds mate in one");
    // Qd1xd5 wins an undefended queen.
    expect(is(think("4k3/8/8/3q4/8/8/8/3QK3 w - - 0 1", 3), 3, 35), "takes a hanging queen");
    // Same back-rank mate for black: Ra8-a1.
    expect(is(think("r5k1/8/8/8/8/8/5PPP/6K1 b - - 0 1", 3), 56, 0), "finds mate in one as black");

    // The easier levels' margin still never skips a mate or a free queen, but does vary the moves.
    bool alwaysMates = true, alwaysTakes = true;
    for (int i = 0; i < 20; ++i) {
        alwaysMates &= is(think("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", 3, 150), 0, 56);
        alwaysTakes &= is(think("4k3/8/8/3q4/8/8/8/3QK3 w - - 0 1", 3, 150), 3, 35);
    }
    expect(alwaysMates, "with a margin it still plays mate in one");
    expect(alwaysTakes, "with a margin it still takes a hanging queen");
    {
        const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        bool varied = false;
        const Move first = think(start, 3, 60);
        for (int i = 0; i < 20 && !varied; ++i) varied = !is(think(start, 3, 60), first.from, first.to);
        expect(varied, "with a margin it varies its moves");
    }

    // A time budget is respected even with no depth cap.
    {
        Searcher s;
        const auto t0 = std::chrono::steady_clock::now();
        s.start(middle, {}, SearchLimits{64, 200});
        while (!s.step(10)) {}
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("      200 ms budget: %lld ms, depth %d\n", static_cast<long long>(ms), s.depth());
        expect(ms < 400 && s.depth() >= 2, "stops near its time budget");
    }

    std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
