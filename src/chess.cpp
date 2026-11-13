#include "chess.hpp"

#include <cctype>
#include <sstream>

namespace chess {
namespace {

constexpr int kKnight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int kKing[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int kRookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int kBishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

bool onBoard(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
int sign(Side c) { return c == White ? 1 : -1; }

}  // namespace

Position Position::start() {
    return fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

Position Position::fromFen(const std::string& fen) {
    Position p;
    p.sq.fill(EMPTY);
    std::istringstream in(fen);
    std::string board, side = "w", castle = "-", ep = "-";
    in >> board >> side >> castle >> ep;

    int f = 0, r = 7;
    for (char c : board) {
        if (c == '/') { --r; f = 0; continue; }
        if (std::isdigit(static_cast<unsigned char>(c))) { f += c - '0'; continue; }
        int8_t t = EMPTY;
        switch (std::tolower(static_cast<unsigned char>(c))) {
            case 'p': t = PAWN; break;
            case 'n': t = KNIGHT; break;
            case 'b': t = BISHOP; break;
            case 'r': t = ROOK; break;
            case 'q': t = QUEEN; break;
            case 'k': t = KING; break;
        }
        p.sq[r * 8 + f] = std::isupper(static_cast<unsigned char>(c)) ? t : static_cast<int8_t>(-t);
        ++f;
    }
    p.side = side == "b" ? Black : White;
    p.castling = 0;
    for (char c : castle) {
        if (c == 'K') p.castling |= 1;
        if (c == 'Q') p.castling |= 2;
        if (c == 'k') p.castling |= 4;
        if (c == 'q') p.castling |= 8;
    }
    p.ep = ep == "-" ? -1 : static_cast<int8_t>((ep[1] - '1') * 8 + (ep[0] - 'a'));
    return p;
}

bool Position::attacked(int s, Side by) const {
    const int f = fileOf(s), r = rankOf(s), sg = sign(by);
    auto is = [&](int ff, int rr, int type) { return onBoard(ff, rr) && sq[rr * 8 + ff] == sg * type; };

    // A pawn of colour `by` attacks diagonally forward, so look one rank "behind" s.
    if (is(f - 1, r - sg, PAWN) || is(f + 1, r - sg, PAWN)) return true;
    for (auto& d : kKnight) if (is(f + d[0], r + d[1], KNIGHT)) return true;
    for (auto& d : kKing) if (is(f + d[0], r + d[1], KING)) return true;

    auto slide = [&](const int (&dirs)[4][2], int type) {
        for (auto& d : dirs) {
            for (int ff = f + d[0], rr = r + d[1]; onBoard(ff, rr); ff += d[0], rr += d[1]) {
                int8_t p = sq[rr * 8 + ff];
                if (p == EMPTY) continue;
                if (p == sg * type || p == sg * QUEEN) return true;
                break;
            }
        }
        return false;
    };
    return slide(kRookDirs, ROOK) || slide(kBishopDirs, BISHOP);
}

int Position::kingSquare(Side c) const {
    for (int s = 0; s < 64; ++s)
        if (sq[s] == sign(c) * KING) return s;
    return -1;
}

bool Position::inCheck(Side c) const {
    int k = kingSquare(c);
    return k >= 0 && attacked(k, c == White ? Black : White);
}

void Position::pseudoMoves(std::vector<Move>& out) const {
    const int sg = sign(side);
    const Side enemy = side == White ? Black : White;
    auto add = [&](int from, int to, uint8_t flags = 0, int8_t promo = EMPTY) {
        out.push_back(Move{static_cast<int8_t>(from), static_cast<int8_t>(to), promo, flags});
    };
    auto isEnemy = [&](int s) { return sq[s] != EMPTY && sideOf(sq[s]) == enemy; };

    for (int s = 0; s < 64; ++s) {
        int8_t p = sq[s];
        if (p == EMPTY || sideOf(p) != side) continue;
        const int f = fileOf(s), r = rankOf(s);

        switch (typeOf(p)) {
            case PAWN: {
                const int startRank = side == White ? 1 : 6;
                const int lastRank = side == White ? 7 : 0;
                auto pawnTo = [&](int to, uint8_t flags) {
                    if (rankOf(to) == lastRank) {
                        for (int8_t pr : {QUEEN, ROOK, BISHOP, KNIGHT}) add(s, to, flags, pr);
                    } else {
                        add(s, to, flags);
                    }
                };
                int one = s + 8 * sg;
                if (onBoard(f, r + sg) && sq[one] == EMPTY) {
                    pawnTo(one, 0);
                    int two = s + 16 * sg;
                    if (r == startRank && sq[two] == EMPTY) add(s, two, DOUBLE_PUSH);
                }
                for (int df : {-1, 1}) {
                    if (!onBoard(f + df, r + sg)) continue;
                    int to = (r + sg) * 8 + f + df;
                    if (isEnemy(to)) pawnTo(to, CAPTURE);
                    else if (to == ep) add(s, to, CAPTURE | EN_PASSANT);
                }
                break;
            }
            case KNIGHT:
            case KING: {
                auto& steps = typeOf(p) == KNIGHT ? kKnight : kKing;
                for (auto& d : steps) {
                    int ff = f + d[0], rr = r + d[1];
                    if (!onBoard(ff, rr)) continue;
                    int to = rr * 8 + ff;
                    if (sq[to] == EMPTY) add(s, to);
                    else if (isEnemy(to)) add(s, to, CAPTURE);
                }
                break;
            }
            default: {  // sliders
                const int t = typeOf(p);
                auto slide = [&](const int (&dirs)[4][2]) {
                    for (auto& d : dirs) {
                        for (int ff = f + d[0], rr = r + d[1]; onBoard(ff, rr); ff += d[0], rr += d[1]) {
                            int to = rr * 8 + ff;
                            if (sq[to] == EMPTY) { add(s, to); continue; }
                            if (isEnemy(to)) add(s, to, CAPTURE);
                            break;
                        }
                    }
                };
                if (t == ROOK || t == QUEEN) slide(kRookDirs);
                if (t == BISHOP || t == QUEEN) slide(kBishopDirs);
            }
        }
    }

    // Castling: king and rook unmoved (rights), path empty, king not passing through check.
    const int home = side == White ? 4 : 60;
    const uint8_t kRight = side == White ? 1 : 4, qRight = side == White ? 2 : 8;
    if (sq[home] == sg * KING && !attacked(home, enemy)) {
        if ((castling & kRight) && sq[home + 3] == sg * ROOK && sq[home + 1] == EMPTY && sq[home + 2] == EMPTY &&
            !attacked(home + 1, enemy) && !attacked(home + 2, enemy))
            add(home, home + 2, CASTLE);
        if ((castling & qRight) && sq[home - 4] == sg * ROOK && sq[home - 1] == EMPTY && sq[home - 2] == EMPTY &&
            sq[home - 3] == EMPTY && !attacked(home - 1, enemy) && !attacked(home - 2, enemy))
            add(home, home - 2, CASTLE);
    }
}

Position Position::apply(const Move& m) const {
    Position n = *this;
    const int8_t piece = sq[m.from];
    n.sq[m.to] = m.promo ? static_cast<int8_t>(sign(side) * m.promo) : piece;
    n.sq[m.from] = EMPTY;

    if (m.flags & EN_PASSANT) n.sq[rankOf(m.from) * 8 + fileOf(m.to)] = EMPTY;
    if (m.flags & CASTLE) {
        const bool kingside = m.to > m.from;
        const int rookFrom = kingside ? m.from + 3 : m.from - 4;
        const int rookTo = kingside ? m.from + 1 : m.from - 1;
        n.sq[rookTo] = n.sq[rookFrom];
        n.sq[rookFrom] = EMPTY;
    }

    // Any move touching a king or rook home square removes the matching rights.
    auto clear = [&](int s) {
        switch (s) {
            case 4: n.castling &= ~3; break;
            case 7: n.castling &= ~1; break;
            case 0: n.castling &= ~2; break;
            case 60: n.castling &= ~12; break;
            case 63: n.castling &= ~4; break;
            case 56: n.castling &= ~8; break;
        }
    };
    clear(m.from);
    clear(m.to);

    n.ep = (m.flags & DOUBLE_PUSH) ? static_cast<int8_t>((m.from + m.to) / 2) : -1;
    n.side = side == White ? Black : White;
    return n;
}

std::vector<Move> Position::legalMoves() const {
    std::vector<Move> pseudo, legal;
    pseudo.reserve(64);
    pseudoMoves(pseudo);
    for (const Move& m : pseudo)
        if (!apply(m).inCheck(side)) legal.push_back(m);
    return legal;
}

std::string toUci(const Move& m) {
    std::string s = {static_cast<char>('a' + fileOf(m.from)), static_cast<char>('1' + rankOf(m.from)),
                     static_cast<char>('a' + fileOf(m.to)), static_cast<char>('1' + rankOf(m.to))};
    if (m.promo) s += " pnbrqk"[m.promo];
    return s;
}

Move Position::parseUci(const std::string& uci) const {
    for (const Move& m : legalMoves())
        if (toUci(m) == uci) return m;
    return Move{};
}

PositionKey Position::key() const {
    int8_t epKey = -1;
    if (ep >= 0)
        for (const Move& m : legalMoves())
            if (m.flags & EN_PASSANT) { epKey = ep; break; }
    return PositionKey{sq, static_cast<uint8_t>(side), castling, epKey};
}

}  // namespace chess
