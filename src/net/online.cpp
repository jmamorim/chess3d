// App members for online rooms: sending and receiving turns, saving and resuming a room, and the
// sync/turn/cmd protocol (described in docs/online.md).
#include <deque>
#include <sstream>
#include <string>

#include "app/app.hpp"
#include "net/net.hpp"

namespace {

// True when `longer` is `shorter` plus more whole tokens, or the same battle token with more commands.
bool extendsRecord(const std::string& longer, const std::string& shorter) {
    if (longer.size() <= shorter.size() || longer.compare(0, shorter.size(), shorter) != 0) return false;
    if (shorter.empty() || longer[shorter.size()] == ' ') return true;
    return shorter.find(':', shorter.rfind(' ') + 1) != std::string::npos;  // an unfinished battle token grows in place
}

}  // namespace

// Tells the opponent about the turn just made (ply = game_.ply() before it) and keeps the room saved.
void App::sendTurn(int ply, const std::string& token) {
    if (!online_) return;
    net::send("turn " + std::to_string(ply) + " " + token);
    saveRoom();
}

// A move made by the local player; online it also goes to the opponent.
void App::commit(const Move& m) {
    const int ply0 = ply();
    perform(m);
    sendTurn(ply0, game_.pending() ? toUci(m) + ":" : game_.lastToken());
}

void App::commitAbility(int s, int which) {
    const int ply0 = ply();
    useAbility(s, which);
    sendTurn(ply0, game_.lastToken());
}

// Stored per room: "<host|guest> <w|b|-> <B|-> <tokens...>", so a reload can resume the game exactly
// (B = battle game). Rooms saved before battle mode went online lack the B/- field.
void App::saveRoom() const {
    const char side = !seated_ ? '-' : human_ == White ? 'w' : 'b';
    net::save("chess3d:" + room_, std::string(isHost_ ? "host " : "guest ") + side + (game_.battleGame() ? " B " : " - ") +
                                      game_.encode());
}

// Rebuilds the game from `tokens` (flag decides the rules); a record that doesn't replay is
// refused and the game stays as it was.
// With side >= 0, actions past the first `from` must all be that side's (see Game::replay).
bool App::adopt(bool battleGame, const std::string& tokens, int from, int side) {
    const bool oldFlag = game_.battleGame();
    const std::string old = game_.encode();
    const uint32_t seed = game::seedFromCode(room_);
    reset(battleGame, seed);
    const bool ok = game_.replay(tokens, from, side);
    if (!ok) {
        game_.reset(oldFlag, seed);
        game_.replay(old);
    }
    animT_ = 1;
    if (game_.pending()) openBattle(false);  // the intro was already seen
    afterTurn();
    return ok;
}

void App::enterRoom(const std::string& code) {
    vsAi_ = false;
    online_ = true;
    room_ = code;
    link_ = Link::Waiting;
    netError_.clear();
    menuOpen_ = typingCode_ = false;
    gameStarted_ = true;
    net::setUrlCode(code);
}

void App::createRoom() {
    leaveRoom();
    enterRoom(net::newCode());
    isHost_ = seated_ = true;
    human_ = GetRandomValue(0, 1) ? Black : White;
    reset(battleMode_, game::seedFromCode(room_));
    faceSideToMove();
    saveRoom();
    net::host(room_, false);
}

// Opens a room from a link or typed code: resumes our seat if this browser was in it before.
// A guest with nothing saved starts as a normal game; the host's first sync sets the mode.
void App::openRoom(const std::string& code) {
    leaveRoom();
    enterRoom(code);
    std::istringstream in(net::load("chess3d:" + code));
    std::string role, side, tokens, flag;
    in >> role >> side;
    std::getline(in, tokens);
    std::istringstream rest(tokens);
    rest >> flag;
    if (flag == "B" || flag == "-") std::getline(rest, tokens);  // else the old format: all moves
    isHost_ = role == "host";
    seated_ = side == "w" || side == "b";
    human_ = side == "b" ? Black : White;
    reset(flag == "B", game::seedFromCode(code));
    game_.replay(tokens);
    animT_ = 1;
    if (game_.pending()) openBattle(false);
    faceSideToMove();
    if (isHost_) net::host(code, true);
    else net::join(code);
}

void App::leaveRoom() {
    if (!online_) return;
    net::leave();
    net::setUrlCode("");
    online_ = isHost_ = seated_ = false;
}

void App::sendSync() {
    const char* side = !isHost_ ? "-" : human_ == White ? "w" : "b";
    net::send(std::string("sync ") + side + (game_.battleGame() ? " B " : " - ") + game_.encode());
}

// Both sides send "sync <host side|-> <B|-> <record>" on connect and keep the longer record when
// the shorter one is a prefix of it; if the records disagree, the host's wins, and the host's
// battle flag always does.
void App::onSync(std::istringstream& in) {
    std::string side, flag, theirs;
    in >> side >> flag;
    if (flag != "B" && flag != "-") return;
    std::getline(in, theirs);
    theirs.erase(0, theirs.find_first_not_of(' '));
    if (!isHost_) {
        human_ = side == "w" ? Black : White;
        seated_ = true;
    }
    const std::string ours = game_.encode();
    if (!isHost_ && (flag == "B") != game_.battleGame()) adopt(flag == "B", theirs);
    else if (extendsRecord(theirs, ours))  // what they add must be their own turns and commands
        adopt(game_.battleGame(), theirs, game_.actions(), human_ == White ? Black : White);
    else if (!isHost_ && theirs != ours && !extendsRecord(ours, theirs))
        adopt(game_.battleGame(), theirs);
    faceSideToMove();
    saveRoom();
}

// "turn <ply> <token>" from the opponent: a move, an ability ("e2!1") or a battle start ("e4d5:").
void App::onTurn(std::istringstream& in) {
    int ply = -1;
    std::string token;
    in >> ply >> token;
    if (!in) return;
    // The opponent only moves on after their battle's outro, so every command is in, even if our
    // animation still holds some back; settle the battle before judging the turn.
    if (battle_) {
        for (int c : battle_->remote) game_.command(static_cast<battle::Command>(c));
        battle_->remote.clear();
        saveRoom();
        if (fight().over()) finishBattle();
    }
    if (!seated_ || ply != game_.ply() || pos().side == human_ || game_.pending() || gameOver()) return;
    const size_t colon = token.find(':'), bang = token.find('!');
    if (token == "=") {
        game_.claimRepetition();
    } else if (bang != std::string::npos) {
        if (colon != std::string::npos || token.size() != 4 || bang != 2 || token[0] < 'a' || token[0] > 'h' ||
            token[1] < '1' || token[1] > '8' || (token[3] != '0' && token[3] != '1'))
            return;
        const int s = (token[1] - '1') * 8 + (token[0] - 'a'), which = token[3] - '0';
        if (!game_.canUseAbility(s, which)) return;
        useAbility(s, which);
    } else {
        const Move m = game_.find(token.substr(0, colon));
        const bool battleStart = colon != std::string::npos;
        if (m.from < 0 || battleStart != game_.startsBattle(m) || (battleStart && colon + 1 != token.size())) return;
        perform(m);
    }
    saveRoom();
}

// "cmd <ply> <k> <c>": the opponent picked command c for their fighter after k commands had been given.
void App::onCmd(std::istringstream& in) {
    int ply = -1, k = -1, c = -1;
    in >> ply >> k >> c;
    if (!in || !battle_ || !seated_ || ply != game_.ply() || c < 0 || c > 4) return;
    std::deque<int>& remote = battle_->remote;
    if (k != game_.pendingCommands() + static_cast<int>(remote.size())) return;
    // Look ahead on a copy: whose turn is it once the queued commands are in?
    battle::Battle after = fight();
    for (int r : remote) after.act(static_cast<battle::Command>(r));
    if (after.over() || (after.fighter(after.turn()).white ? White : Black) == human_ ||
        !after.canUse(static_cast<battle::Command>(c)))
        return;
    remote.push_back(c);
}

void App::pollNet() {
    std::string line;
    while (online_ && net::poll(line)) {
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (kind == "@waiting") {
            if (link_ != Link::Connected) link_ = Link::Waiting;
        } else if (kind == "@connected") {
            link_ = Link::Connected;
            netError_.clear();
            sendSync();
        } else if (kind == "@disconnected") {
            link_ = Link::Disconnected;
        } else if (kind == "@error") {
            in >> netError_;
            // Someone else holds this code: pick another one for a brand-new room.
            if (netError_ == "unavailable-id" && isHost_ && game_.moves().empty()) createRoom();
        } else if (kind == "full") {
            leaveRoom();
            menuOpen_ = true;
            netError_ = "That room already has two players";
        } else if (kind == "sync") {
            onSync(in);
        } else if (kind == "turn") {
            onTurn(in);
        } else if (kind == "cmd") {
            onCmd(in);
        }
    }
}
