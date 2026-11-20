#pragma once
// Raylib-free game state for chess and battle mode: every turn is recorded as a token, so a game
// can be sent over the network and replayed to exactly the same state (see Game::encode/replay).
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "battle/battle.hpp"
#include "chess/chess.hpp"

namespace game {

// An ability turn is kept in moves() as from == to with this flag.
constexpr uint8_t kAbilityFlag = 0x80;
inline bool isAbility(const chess::Move& m) { return m.flags & kAbilityFlag; }

// Square of the piece a move captures (differs from `to` for en passant).
int capturedSquare(const chess::Move& m);
// The position after a capture whose attacker lost its battle: the attacker is gone, the
// defender keeps its square, and the turn passes.
chess::Position repelled(const chess::Position& p, const chess::Move& m);

using Status = chess::Status;

uint32_t seedFromCode(const std::string& code);  // FNV-1a of the room code
uint32_t battleSeed(uint32_t base, int ply);

class Game {
public:
    void reset(bool battleGame, uint32_t seedBase);

    const chess::Position& pos() const { return pos_; }
    const std::vector<chess::Move>& legal() const { return legal_; }
    Status status() const { return status_; }
    int reps() const { return reps_; }
    bool kingDead() const { return kingDead_; }
    bool battleGame() const { return battleGame_; }
    uint32_t seedBase() const { return seedBase_; }
    const battle::Units& units() const { return units_; }
    int hpAt(int s) const;
    const std::vector<chess::Move>& moves() const { return moves_; }
    const std::vector<chess::Position>& history() const { return history_; }  // before each move
    int ply() const { return static_cast<int>(moves_.size()); }
    bool over() const;
    int winner() const;  // -1 none, else the winning chess::Side value
    // Threefold-repetition draw; recorded as a final "=" token. False when no claim is possible.
    bool claimRepetition();
    chess::Move find(const std::string& uci) const;  // from = -1 when not in legal()

    bool startsBattle(const chess::Move& m) const { return battleGame_ && (m.flags & chess::CAPTURE); }
    void play(const chess::Move& m);  // a non-battle move
    bool canUseAbility(int s, int which = 0) const;
    std::vector<battle::AbilityEffect> useAbility(int s, int which = 0);

    const battle::Battle* pending() const { return pending_ ? &*pending_ : nullptr; }
    int pendingCommands() const { return static_cast<int>(pendingCmds_.size()); }  // 0 when none is pending
    const chess::Move& pendingMove() const { return pendingMove_; }
    const battle::Battle& begin(const chess::Move& m);
    std::vector<battle::Event> command(battle::Command c);  // no-op unless a battle is pending and not over
    battle::Command aiCommand();  // draws only from the battle's separate AI stream
    void finishBattle();
    void undo();  // one finished turn; no-op while a battle is pending

    std::string encode() const;  // space-separated tokens, incl. a pending battle
    // False at the first bad token; the valid prefix stays applied. With side >= 0, every action
    // from index `from` on (see actions()) must be made by that side, so a peer's record can't
    // move or fight for us.
    bool replay(const std::string& tokens, int from = 0, int side = -1);
    int actions() const;  // turns plus battle commands in the record
    std::string lastToken() const;

private:
    void refresh();
    void record(const chess::Move& m, const std::string& token);
    void advance(const chess::Move& m);
    void carryDamage(const chess::Move& m);
    bool owns(int s) const;
    bool applyToken(const std::string& t, const std::function<bool()>& allowed);
    int actor() const;  // side making the next action: the fighter on turn, else the side to move

    chess::Position pos_ = chess::Position::start();
    std::vector<chess::Move> legal_;
    Status status_ = Status::Playing;
    int reps_ = 1;
    bool kingDead_ = false, battleGame_ = false;
    uint32_t seedBase_ = 0;
    battle::Units units_{};
    std::vector<chess::Move> moves_;
    std::vector<chess::Position> history_;
    std::vector<battle::Units> unitHistory_;
    std::vector<std::string> tokens_;  // parallel to moves_
    std::optional<battle::Battle> pending_;
    chess::Move pendingMove_;
    std::string pendingCmds_;
    bool claimed_ = false;
};

}  // namespace game
