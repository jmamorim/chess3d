#include "game/game.hpp"

#include <sstream>

namespace game {

using namespace chess;

int capturedSquare(const Move& m) { return m.flags & EN_PASSANT ? rankOf(m.from) * 8 + fileOf(m.to) : m.to; }

Position repelled(const Position& p, const Move& m) {
    Position r = p;
    r.sq[m.from] = EMPTY;
    switch (m.from) {  // a rook that dies on its home square takes its castling right with it
        case 7: r.castling &= ~1; break;
        case 0: r.castling &= ~2; break;
        case 63: r.castling &= ~4; break;
        case 56: r.castling &= ~8; break;
    }
    r.side = p.side == White ? Black : White;
    r.ep = -1;
    return r;
}

uint32_t seedFromCode(const std::string& code) {
    uint32_t h = 2166136261u;
    for (unsigned char c : code) h = (h ^ c) * 16777619u;
    return h;
}

uint32_t battleSeed(uint32_t base, int ply) {
    uint32_t z = (base ^ static_cast<uint32_t>(ply) * 0x9E3779B9u) + 0x9E3779B9u;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

void Game::reset(bool battleGame, uint32_t seedBase) {
    battleGame_ = battleGame;
    seedBase_ = seedBase;
    pos_ = Position::start();
    history_.clear();
    moves_.clear();
    tokens_.clear();
    unitHistory_.clear();
    units_.fill({});
    pending_.reset();
    pendingCmds_.clear();
    claimed_ = false;
    refresh();
}

void Game::refresh() {
    legal_ = battleGame_ ? pos_.pseudoLegalMoves() : pos_.legalMoves();
    const bool check = pos_.inCheck(pos_.side);
    // In a battle game an attacked king can still fight, so no position is checkmate.
    status_ = legal_.empty() ? (check && !battleGame_ ? Status::Checkmate : Status::Stalemate)
                             : (check ? Status::Check : Status::Playing);
    kingDead_ = pos_.kingSquare(White) < 0 || pos_.kingSquare(Black) < 0;
    const PositionKey key = pos_.key();
    reps_ = 1;
    // Using an ability changes the pieces' state, so (like a capture would) it resets the count.
    for (size_t i = history_.size(); i-- > 0 && !isAbility(moves_[i]);) reps_ += history_[i].key() == key;
    // FIDE 9.6.1: the fifth occurrence ends the game without a claim.
    if (!over() && reps_ >= 5) status_ = Status::Repetition;
}

bool Game::over() const {
    return kingDead_ || status_ == Status::Checkmate || status_ == Status::Stalemate || status_ == Status::Repetition;
}

int Game::winner() const {
    if (kingDead_) return pos_.kingSquare(White) < 0 ? Black : White;
    if (status_ == Status::Checkmate) return pos_.side == White ? Black : White;
    return -1;
}

int Game::hpAt(int s) const { return battle::maxHp(typeOf(pos_.sq[s])) - units_[s].dmg; }

Move Game::find(const std::string& uci) const {
    for (const Move& m : legal_)
        if (toUci(m) == uci) return m;
    return Move{};
}

void Game::record(const Move& m, const std::string& token) {
    history_.push_back(pos_);
    unitHistory_.push_back(units_);
    moves_.push_back(m);
    tokens_.push_back(token);
}

void Game::advance(const Move& m) {
    carryDamage(m);
    pos_ = pos_.apply(m);
    refresh();
}

// Battle state travels with the piece; a captured piece's is dropped, a promoted pawn starts fresh.
void Game::carryDamage(const Move& m) {
    units_[capturedSquare(m)] = {};
    units_[m.to] = m.promo != EMPTY ? battle::Unit{} : units_[m.from];
    if (m.to != m.from) units_[m.from] = {};
    if (m.flags & CASTLE) {
        const int rank = rankOf(m.from) * 8;
        const bool kingSide = fileOf(m.to) == 6;
        const int rookFrom = rank + (kingSide ? 7 : 0), rookTo = rank + (kingSide ? 5 : 3);
        units_[rookTo] = units_[rookFrom];
        units_[rookFrom] = {};
    }
}

void Game::play(const Move& m) {
    record(m, toUci(m));
    advance(m);
}

bool Game::owns(int s) const { return s >= 0 && s < 64 && pos_.sq[s] != EMPTY && sideOf(pos_.sq[s]) == pos_.side; }

bool Game::canUseAbility(int s, int which) const {
    return battleGame_ && (which == 0 || which == 1) && !pending_ && owns(s) && !over() &&
           battle::abilityUseful(pos_.sq, units_, s, ply(), which);
}

std::vector<battle::AbilityEffect> Game::useAbility(int s, int which) {
    Move marker;
    marker.from = marker.to = static_cast<int8_t>(s);
    marker.flags = kAbilityFlag;
    record(marker, std::string{static_cast<char>('a' + fileOf(s)), static_cast<char>('1' + rankOf(s)), '!',
                               static_cast<char>('0' + which)});
    auto effects = battle::useAbility(pos_.sq, units_, s, ply() - 1, which);
    pos_.side = pos_.side == White ? Black : White;
    pos_.ep = -1;
    refresh();
    return effects;
}

const battle::Battle& Game::begin(const Move& m) {
    const int cap = capturedSquare(m);
    const int8_t a = pos_.sq[m.from];
    pending_.emplace(typeOf(a), a > 0, hpAt(m.from), typeOf(pos_.sq[cap]), hpAt(cap), battleSeed(seedBase_, ply()));
    pendingMove_ = m;
    pendingCmds_.clear();
    // Buffs and curses from abilities used on the board.
    for (int i = 0; i < 2; ++i) {
        const battle::Unit& u = units_[i == 0 ? m.from : cap];
        pending_->prepare(i, u.buffs & battle::kBulwark ? 2 : u.buffs & battle::kShield ? 1 : 0, u.buffs & battle::kEmpower);
        pending_->curse(i, u.curses);
    }
    return *pending_;
}

std::vector<battle::Event> Game::command(battle::Command c) {
    if (!pending_ || pending_->over()) return {};
    pendingCmds_ += static_cast<char>('0' + static_cast<int>(c));
    return pending_->act(c);
}

battle::Command Game::aiCommand() { return pending_->aiChoice(); }

void Game::finishBattle() {
    if (!pending_ || !pending_->over()) return;
    const battle::Battle& f = *pending_;
    const Move m = pendingMove_;
    const bool attackerWon = f.winner() == 0;
    const int left = f.fighter(f.winner()).hp, type = f.fighter(f.winner()).type;
    const std::string token = toUci(m) + ":" + pendingCmds_;
    pending_.reset();
    pendingCmds_.clear();
    record(m, token);
    const int cap = capturedSquare(m);
    units_[m.from].buffs = units_[m.from].curses = 0;  // a battle uses up both pieces' buffs and curses
    units_[cap].buffs = units_[cap].curses = 0;
    if (attackerWon) {
        units_[m.from].dmg = battle::maxHp(type) - left;
        advance(m);
        return;
    }
    units_[cap].dmg = battle::maxHp(type) - left;
    units_[m.from] = {};
    pos_ = repelled(pos_, m);
    refresh();
}

void Game::undo() {
    if (pending_ || moves_.empty()) return;
    claimed_ = false;
    pos_ = history_.back();
    history_.pop_back();
    units_ = unitHistory_.back();
    unitHistory_.pop_back();
    moves_.pop_back();
    tokens_.pop_back();
    refresh();
}

std::string Game::encode() const {
    std::string out;
    for (const std::string& t : tokens_) out += (out.empty() ? "" : " ") + t;
    if (pending_) out += (out.empty() ? "" : " ") + toUci(pendingMove_) + ":" + pendingCmds_;
    if (claimed_) out += out.empty() ? "=" : " =";
    return out;
}

bool Game::claimRepetition() {
    if (pending_ || over() || reps_ < 3) return false;
    claimed_ = true;
    status_ = Status::Repetition;
    return true;
}

int Game::actions() const {
    int n = static_cast<int>(tokens_.size()) + claimed_;
    for (const std::string& t : tokens_) {
        const size_t colon = t.find(':');
        if (colon != std::string::npos) n += static_cast<int>(t.size() - colon - 1);
    }
    if (pending_) n += 1 + static_cast<int>(pendingCmds_.size());
    return n;
}

int Game::actor() const {
    if (pending_) return pending_->fighter(pending_->turn()).white ? White : Black;
    return pos_.side;
}

std::string Game::lastToken() const { return tokens_.empty() ? "" : tokens_.back(); }

bool Game::applyToken(const std::string& t, const std::function<bool()>& allowed) {
    if (pending_ || over()) return false;  // a battle still in progress (or the game's end) must be last
    if (t == "=") return allowed() && claimRepetition();
    if (!allowed()) return false;
    const size_t colon = t.find(':'), bang = t.find('!');
    if (colon != std::string::npos) {
        if (!battleGame_ || bang != std::string::npos) return false;
        const Move m = find(t.substr(0, colon));
        const std::string cmds = t.substr(colon + 1);
        if (m.from < 0 || !startsBattle(m)) return false;
        for (char c : cmds)
            if (c < '0' || c > '4') return false;  // checked first so a bad token leaves no half-begun battle
        begin(m);
        for (char c : cmds) {
            if (pending_->over()) {  // more commands than the battle has turns
                pending_.reset();
                pendingCmds_.clear();
                return false;
            }
            if (!allowed()) return false;  // the battle stays pending with the commands so far
            command(static_cast<battle::Command>(c - '0'));
        }
        if (pending_->over()) finishBattle();
        return true;
    }
    if (bang != std::string::npos) {
        if (t.size() != 4 || bang != 2 || t[0] < 'a' || t[0] > 'h' || t[1] < '1' || t[1] > '8' || (t[3] != '0' && t[3] != '1'))
            return false;
        const int s = (t[1] - '1') * 8 + (t[0] - 'a'), which = t[3] - '0';
        if (!canUseAbility(s, which)) return false;
        useAbility(s, which);
        return true;
    }
    const Move m = find(t);
    if (m.from < 0 || startsBattle(m)) return false;
    play(m);
    return true;
}

bool Game::replay(const std::string& tokens, int from, int side) {
    reset(battleGame_, seedBase_);
    int n = 0;
    const auto allowed = [&] { return n++ < from || side < 0 || actor() == side; };
    std::istringstream in(tokens);
    std::string t;
    while (in >> t)
        if (!applyToken(t, allowed)) return false;
    return true;
}

}  // namespace game
