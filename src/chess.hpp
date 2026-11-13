#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chess {

// Pieces are signed: positive = white, negative = black.
enum PieceType : int8_t { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum Side : uint8_t { White = 0, Black = 1 };

enum MoveFlag : uint8_t { CAPTURE = 1, EN_PASSANT = 2, CASTLE = 4, DOUBLE_PUSH = 8 };

struct Move {
    int8_t from = -1, to = -1;
    int8_t promo = EMPTY;  // piece type for promotions
    uint8_t flags = 0;
};

enum class Status { Playing, Check, Checkmate, Stalemate };

// Square index = rank * 8 + file; rank 0 is white's back rank, file 0 is the a-file.
inline int fileOf(int s) { return s & 7; }
inline int rankOf(int s) { return s >> 3; }
inline int typeOf(int8_t p) { return p < 0 ? -p : p; }
inline Side sideOf(int8_t p) { return p > 0 ? White : Black; }

struct Position {
    std::array<int8_t, 64> sq{};
    Side side = White;
    uint8_t castling = 0b1111;  // 1 = white O-O, 2 = white O-O-O, 4 = black O-O, 8 = black O-O-O
    int8_t ep = -1;             // en-passant target square, or -1

    static Position start();
    static Position fromFen(const std::string& fen);

    std::vector<Move> legalMoves() const;
    Position apply(const Move& m) const;
    bool attacked(int square, Side by) const;
    bool inCheck(Side c) const;
    int kingSquare(Side c) const;

private:
    void pseudoMoves(std::vector<Move>& out) const;
};

}  // namespace chess
