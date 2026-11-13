#include "ai.hpp"

#include <algorithm>

namespace chess {
namespace {

constexpr int kInf = 1000000;
constexpr int kMate = 100000;
constexpr int kMaxPly = 60;
constexpr int kValue[7] = {0, 100, 320, 330, 500, 900, 0};

// Piece-square tables, written from white's side with rank 8 on top (as a board diagram reads).
// Values follow the "Simplified Evaluation Function" on the Chess Programming Wiki.
// clang-format off
constexpr int8_t kPst[7][64] = {
    {},
    {  // pawn
          0,   0,   0,   0,   0,   0,   0,   0,
         50,  50,  50,  50,  50,  50,  50,  50,
         10,  10,  20,  30,  30,  20,  10,  10,
          5,   5,  10,  25,  25,  10,   5,   5,
          0,   0,   0,  20,  20,   0,   0,   0,
          5,  -5, -10,   0,   0, -10,  -5,   5,
          5,  10,  10, -20, -20,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0,
    },
    {  // knight
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20,   0,   0,   0,   0, -20, -40,
        -30,   0,  10,  15,  15,  10,   0, -30,
        -30,   5,  15,  20,  20,  15,   5, -30,
        -30,   0,  15,  20,  20,  15,   0, -30,
        -30,   5,  10,  15,  15,  10,   5, -30,
        -40, -20,   0,   5,   5,   0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50,
    },
    {  // bishop
        -20, -10, -10, -10, -10, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,  10,  10,   5,   0, -10,
        -10,   5,   5,  10,  10,   5,   5, -10,
        -10,   0,  10,  10,  10,  10,   0, -10,
        -10,  10,  10,  10,  10,  10,  10, -10,
        -10,   5,   0,   0,   0,   0,   5, -10,
        -20, -10, -10, -10, -10, -10, -10, -20,
    },
    {  // rook
          0,   0,   0,   0,   0,   0,   0,   0,
          5,  10,  10,  10,  10,  10,  10,   5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
          0,   0,   0,   5,   5,   0,   0,   0,
    },
    {  // queen
        -20, -10, -10,  -5,  -5, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,   5,   5,   5,   0, -10,
         -5,   0,   5,   5,   5,   5,   0,  -5,
          0,   0,   5,   5,   5,   5,   0,  -5,
        -10,   5,   5,   5,   5,   5,   0, -10,
        -10,   0,   5,   0,   0,   0,   0, -10,
        -20, -10, -10,  -5,  -5, -10, -10, -20,
    },
    {  // king, middlegame
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
         20,  20,   0,   0,   0,   0,  20,  20,
         20,  30,  10,   0,   0,  10,  30,  20,
    },
};
constexpr int8_t kKingEnd[64] = {
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10,   0,   0, -10, -20, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50,
};
// clang-format on

// Table index for a piece of side `s` on square `sq` (tables are stored rank 8 first).
int pstIndex(int sq, Side s) { return s == White ? (7 - rankOf(sq)) * 8 + fileOf(sq) : sq; }

int moveScore(const Position& p, const Move& m) {
    if (m.promo) return 20000 + kValue[m.promo];
    if (m.flags & CAPTURE) {
        const int victim = (m.flags & EN_PASSANT) ? PAWN : typeOf(p.sq[m.to]);
        return 10000 + kValue[victim] * 10 - kValue[typeOf(p.sq[m.from])] / 10;
    }
    return 0;
}

}  // namespace

uint64_t hashOf(const Position& p) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    auto mix = [&h](uint8_t b) { h = (h ^ b) * 1099511628211ull; };
    for (int8_t v : p.sq) mix(static_cast<uint8_t>(v));
    mix(p.side);
    mix(p.castling);
    mix(static_cast<uint8_t>(p.ep));
    return h;
}

int evaluate(const Position& p) {
    int material = 0, kingMg[2] = {}, kingEg[2] = {}, nonPawn = 0;
    for (int s = 0; s < 64; ++s) {
        const int8_t v = p.sq[s];
        if (v == EMPTY) continue;
        const int t = typeOf(v);
        const Side side = sideOf(v);
        const int i = pstIndex(s, side);
        if (t == KING) {
            kingMg[side] = kPst[KING][i];
            kingEg[side] = kKingEnd[i];
            continue;
        }
        if (t != PAWN) nonPawn += kValue[t];
        material += (side == White ? 1 : -1) * (kValue[t] + kPst[t][i]);
    }
    // Blend the king tables from middlegame to endgame as the non-pawn material comes off.
    const int full = 2 * (2 * 320 + 2 * 330 + 2 * 500 + 900);
    const int phase = std::min(nonPawn, full);
    const int king = ((kingMg[White] - kingMg[Black]) * phase + (kingEg[White] - kingEg[Black]) * (full - phase)) / full;
    const int score = material + king;
    return p.side == White ? score : -score;
}

void Searcher::start(const Position& root, std::vector<uint64_t> history, SearchLimits limits) {
    path_ = std::move(history);
    path_.push_back(hashOf(root));
    limits_ = limits;
    deadline_ = Clock::now() + std::chrono::milliseconds(limits.budgetMs);
    for (auto& k : killers_) k[0] = k[1] = Move{};
    stack_.clear();
    haveResult_ = false;
    completedDepth_ = 0;
    rootScores_.clear();
    lastRootScores_.clear();
    rootBest_ = -kInf;

    Frame rootFrame{root, root.legalMoves(), 0, 1, -kInf, kInf, 0, false};
    order(root, rootFrame.moves, 0);
    best_ = rootFrame.moves.empty() ? Move{} : rootFrame.moves.front();
    iterBest_ = best_;
    // Nothing to think about with zero or one legal move.
    running_ = rootFrame.moves.size() > 1;
    if (running_) stack_.push_back(std::move(rootFrame));
}

bool Searcher::step(int sliceMs) {
    const auto sliceEnd = Clock::now() + std::chrono::milliseconds(sliceMs);
    for (unsigned n = 0; running_; ++n) {
        if ((n & 255) == 0) {
            const auto now = Clock::now();
            if (now >= deadline_) {
                // The unfinished iteration searched the previous best move first, so any move it
                // has already preferred is at least as good a choice.
                if (stack_.front().index > 0) best_ = iterBest_;
                running_ = false;
                pickWithinMargin();
                break;
            }
            if (now >= sliceEnd) break;
        }

        if (haveResult_) {
            // A child just returned `result_` to the frame on top of the stack.
            haveResult_ = false;
            path_.pop_back();
            Frame& f = stack_.back();
            const int score = -result_;
            const Move& m = f.moves[f.index];
            if (score >= f.beta) {
                if (!f.quiesce && !(m.flags & CAPTURE) && !m.promo) {
                    killers_[f.ply][1] = killers_[f.ply][0];
                    killers_[f.ply][0] = m;
                }
                returnValue(f.beta);
                continue;
            }
            if (stack_.size() == 1 && limits_.marginCp > 0) {
                // Keep the window open margin below the best, so near-best moves get exact scores.
                if (score > f.alpha) rootScores_.emplace_back(m, score);
                if (score > rootBest_) {
                    rootBest_ = score;
                    iterBest_ = m;
                    f.alpha = std::max(f.alpha, rootBest_ - limits_.marginCp);
                }
            } else if (score > f.alpha) {
                f.alpha = score;
                if (stack_.size() == 1) {
                    iterBest_ = m;
                    rootBest_ = score;
                }
            }
            ++f.index;
            continue;
        }

        Frame& f = stack_.back();
        if (f.index == static_cast<int>(f.moves.size())) {
            if (stack_.size() == 1) finishIteration();
            else returnValue(f.alpha);
            continue;
        }
        const Position child = f.pos.apply(f.moves[f.index]);
        path_.push_back(hashOf(child));
        enter(child, f.depth - 1, -f.beta, -f.alpha, f.ply + 1, f.quiesce);
    }
    if (!running_) {
        stack_.clear();
        haveResult_ = false;
    }
    return !running_;
}

void Searcher::returnValue(int value) {
    stack_.pop_back();
    result_ = value;
    haveResult_ = true;
}

void Searcher::enter(const Position& p, int depth, int alpha, int beta, int ply, bool quiesce) {
    auto leaf = [this](int value) {
        result_ = value;
        haveResult_ = true;
    };
    if (!quiesce) {
        if (repeated(path_.back())) return leaf(0);
        const bool check = p.inCheck(p.side);
        if (check) ++depth;  // look one ply further when in check, so mates in the line are found
        if (depth <= 0 || ply >= kMaxPly) {
            quiesce = true;
        } else {
            Frame f{p, p.legalMoves(), 0, depth, alpha, beta, ply, false};
            if (f.moves.empty()) return leaf(check ? -kMate + ply : 0);
            order(p, f.moves, ply);
            stack_.push_back(std::move(f));
            return;
        }
    }
    // Quiescence: the side to move may stand pat or try captures and promotions only.
    const int standPat = evaluate(p);
    if (standPat >= beta || ply >= kMaxPly) return leaf(standPat);
    Frame f{p, p.legalMoves(), 0, 0, std::max(alpha, standPat), beta, ply, true};
    f.moves.erase(std::remove_if(f.moves.begin(), f.moves.end(),
                                 [](const Move& m) { return !(m.flags & CAPTURE) && !m.promo; }),
                  f.moves.end());
    if (f.moves.empty()) return leaf(f.alpha);
    order(p, f.moves, kMaxPly);
    stack_.push_back(std::move(f));
}

void Searcher::finishIteration() {
    Frame& root = stack_.front();
    best_ = iterBest_;
    completedDepth_ = root.depth;
    const bool mateFound = rootBest_ > kMate - kMaxPly;
    lastRootScores_.swap(rootScores_);
    lastRootBest_ = rootBest_;
    rootScores_.clear();
    rootBest_ = -kInf;
    if (root.depth >= limits_.maxDepth || mateFound || Clock::now() >= deadline_) {
        running_ = false;
        pickWithinMargin();
        return;
    }
    // Search the best move first next time, so its score sets a tight bound early.
    std::stable_partition(root.moves.begin(), root.moves.end(), [&](const Move& m) {
        return m.from == best_.from && m.to == best_.to && m.promo == best_.promo;
    });
    root.index = 0;
    ++root.depth;
    root.alpha = -kInf;
}

// Picks among the last completed iteration's moves within the margin of its best, weighting each
// by how close it came. A found mate is always played.
void Searcher::pickWithinMargin() {
    if (limits_.marginCp <= 0 || lastRootScores_.empty() || lastRootBest_ > kMate - kMaxPly) return;
    std::vector<Move> moves;
    std::vector<int> weights;
    for (const auto& [move, score] : lastRootScores_) {
        const int behind = lastRootBest_ - score;
        if (behind > limits_.marginCp) continue;  // scored before a better move raised the bar
        moves.push_back(move);
        weights.push_back(limits_.marginCp - behind + 1);
    }
    std::discrete_distribution<size_t> pick(weights.begin(), weights.end());
    best_ = moves[pick(rng_)];
}

bool Searcher::repeated(uint64_t h) const {
    // The last entry is the position itself; a single earlier occurrence is scored as a draw.
    for (size_t i = 0; i + 1 < path_.size(); ++i)
        if (path_[i] == h) return true;
    return false;
}

void Searcher::order(const Position& p, std::vector<Move>& moves, int ply) const {
    auto score = [&](const Move& m) {
        int s = moveScore(p, m);
        if (s == 0 && ply < kMaxPly) {
            for (const Move& k : killers_[ply])
                if (k.from == m.from && k.to == m.to) s = 5000;
        }
        return s;
    };
    std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) { return score(a) > score(b); });
}

}  // namespace chess
