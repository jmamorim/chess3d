#pragma once
#include <chrono>
#include <cstdint>
#include <vector>

#include "chess.hpp"

namespace chess {

struct SearchLimits {
    int maxDepth;
    int budgetMs;
};

// Alpha-beta search with an explicit stack instead of recursion, so it can stop at any node and
// carry on in the next step(). That lets a single-threaded caller (the WASM build) keep rendering
// while the engine thinks.
class Searcher {
public:
    // `history` holds the hashes of earlier game positions, so the engine sees repetitions.
    void start(const Position& root, std::vector<uint64_t> history, SearchLimits limits);
    // Searches for about `sliceMs`; returns true once best() is final.
    bool step(int sliceMs);
    void cancel() { running_ = false; }
    bool running() const { return running_; }
    Move best() const { return best_; }
    int depth() const { return completedDepth_; }

private:
    using Clock = std::chrono::steady_clock;

    struct Frame {
        Position pos;
        std::vector<Move> moves;
        int index;  // next move to search
        int depth, alpha, beta, ply;
        bool quiesce;
    };

    void enter(const Position& p, int depth, int alpha, int beta, int ply, bool quiesce);
    void returnValue(int value);
    void finishIteration();
    bool repeated(uint64_t h) const;
    void order(const Position& p, std::vector<Move>& moves, int ply) const;

    std::vector<Frame> stack_;    // stack_[0] is the root
    std::vector<uint64_t> path_;  // game history + current search line
    int result_ = 0;              // value a finished child hands to the frame below it
    bool haveResult_ = false;
    SearchLimits limits_{};
    Clock::time_point deadline_;
    bool running_ = false;
    Move killers_[64][2]{};
    Move iterBest_{}, best_{};
    int completedDepth_ = 0;
};

uint64_t hashOf(const Position& p);
int evaluate(const Position& p);  // centipawns, from the side to move's point of view

}  // namespace chess
