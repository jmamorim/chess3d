// Checks the repetition key: which positions count as "the same" (FIDE art. 9.2).
#include <cstdio>

#include "chess.hpp"

using namespace chess;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

Position play(Position p, int from, int to) {
    for (const Move& m : p.legalMoves())
        if (m.from == from && m.to == to) return p.apply(m);
    std::printf("FAIL  no legal move %d->%d\n", from, to);
    ++failures;
    return p;
}

}  // namespace

int main() {
    const Position start = Position::start();

    // Nf3 Nf6 Ng1 Ng8 returns to the start position.
    Position p = play(start, 6, 21);
    p = play(p, 62, 45);
    p = play(p, 21, 6);
    p = play(p, 45, 62);
    expect(p.key() == start.key(), "knight shuffle repeats the start position");
    expect(!(play(start, 6, 21).key() == start.key()), "a different placement gives a different key");

    // After 1.e4 the ep square is e3 but no black pawn can take, so it must not count.
    const Position e4 = Position::fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    const Position e4NoEp = Position::fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    expect(e4.key() == e4NoEp.key(), "ep square without a legal ep capture is ignored");

    // Black pawn on d4 can take e3 en passant, so the ep square matters.
    const Position epLive = Position::fromFen("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");
    const Position epGone = Position::fromFen("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1");
    expect(!(epLive.key() == epGone.key()), "ep square with a legal ep capture counts");

    expect(!(Position::fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1").key() ==
             Position::fromFen("r3k2r/8/8/8/8/8/8/R3K2R w Qkq - 0 1").key()),
           "different castling rights give different keys");
    expect(!(start.key() == Position::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1").key()),
           "side to move is part of the key");

    // UCI text, used by online play: every legal move round-trips, and garbage is rejected.
    const Position kiwi = Position::fromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    bool roundTrip = true;
    for (const Move& m : kiwi.legalMoves()) {
        const Move back = kiwi.parseUci(toUci(m));
        roundTrip &= back.from == m.from && back.to == m.to && back.promo == m.promo && back.flags == m.flags;
    }
    expect(roundTrip, "every legal move round-trips through UCI");
    expect(toUci(play(start, 12, 28).parseUci("e7e5")) == "e7e5", "parses a black reply");
    expect(start.parseUci("e2e5").from < 0 && start.parseUci("zz").from < 0, "rejects illegal or malformed UCI");
    const Position promo = Position::fromFen("8/4P3/8/8/8/8/k7/4K3 w - - 0 1");
    expect(promo.parseUci("e7e8n").promo == KNIGHT && promo.parseUci("e7e8").from < 0, "promotion needs its piece letter");

    std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
