// 3D chess prototype: hotseat, vs-AI or online play with full rules, orbit camera, click-to-move,
// and an offline battle mode where every capture is fought out as a turn-based duel.
#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cctype>
#include <cmath>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

#include "ai.hpp"
#include "battle.hpp"
#include "chess.hpp"
#include "net.hpp"
#include "pieces.hpp"
#include "shaders.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

// emscripten's GLFW can report the cursor in CSS pixels while raylib works in canvas pixels
// (HiDPI screens, browser zoom), which offsets picking. Track the pointer as a fraction of the
// canvas in JS instead, so the result is independent of either scale.
EM_JS(void, installPointerTracker, (), {
    const canvas = Module.canvas;
    Module.pointerU = 0;
    Module.pointerV = 0;
    const update = (e) => {
        const r = canvas.getBoundingClientRect();
        Module.pointerU = (e.clientX - r.left) / r.width;
        Module.pointerV = (e.clientY - r.top) / r.height;
    };
    canvas.addEventListener('pointermove', update);
    canvas.addEventListener('pointerdown', update);
});
EM_JS(float, pointerU, (), { return Module.pointerU; });
EM_JS(float, pointerV, (), { return Module.pointerV; });
#endif

using namespace chess;

namespace {

constexpr float kAnimSeconds = 0.3f;
constexpr int8_t kPromoChoices[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
constexpr int kAiSliceMs = 8;  // engine time per frame, so rendering stays smooth while it thinks

struct Level {
    const char* name;
    SearchLimits limits;
};
constexpr Level kLevels[3] = {{"Easy", {2, 300, 80}}, {"Medium", {4, 600, 45}}, {"Hard", {64, 1500}}};

enum MenuId { kLevelEasy, kLevelMedium, kLevelHard, kHotseat, kPlayWhite, kPlayBlack, kPlayRandom, kCreateRoom, kJoinRoom, kBattleToggle };
constexpr int kCodeLength = 4;
struct Button {
    Rectangle rect;
    const char* label;
    int id;
};

const Color kLightSquare{222, 202, 168, 255};
const Color kDarkSquare{118, 84, 60, 255};
const Color kFrame{58, 38, 28, 255};
const Color kTable{34, 34, 42, 255};
const Color kWhitePiece{236, 228, 210, 255};
const Color kBlackPiece{48, 44, 52, 255};
const Color kSelect{240, 210, 70, 255};
const Color kLastMove{200, 210, 90, 255};
const Color kCheck{220, 60, 50, 255};
const Color kTarget{70, 160, 90, 255};
const Color kHeal{90, 210, 120, 255};
const Color kPanel{24, 26, 48, 235};
const Color kPanelEdge{200, 200, 230, 255};

constexpr float kBattleIntroSeconds = 0.9f;
constexpr float kBattleEventSeconds = 0.8f;
constexpr float kBattleAiDelay = 0.5f;

// Square of the piece a move captures (differs from `to` for en passant).
int capturedSquare(const Move& m) { return m.flags & EN_PASSANT ? rankOf(m.from) * 8 + fileOf(m.to) : m.to; }

// The position after a capture whose attacker lost its battle: the attacker is gone, the
// defender keeps its square, and the turn passes.
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

Vector3 squareCenter(int s) { return {fileOf(s) - 3.5f, 0.0f, 3.5f - rankOf(s)}; }

int squareAt(Vector3 p) {
    const int f = static_cast<int>(floorf(p.x + 4.0f));
    const int r = static_cast<int>(floorf(4.0f - p.z));
    return (f < 0 || f > 7 || r < 0 || r > 7) ? -1 : r * 8 + f;
}

Color mix(Color a, Color b, float t) {
    auto l = [t](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), 255};
}

float wrapAngle(float a) { return atan2f(sinf(a), cosf(a)); }

#ifdef __EMSCRIPTEN__
Vector2 cursorPosition() {
    return {pointerU() * GetScreenWidth(), pointerV() * GetScreenHeight()};
}
#else
Vector2 cursorPosition() { return GetMousePosition(); }
#endif

class App {
public:
    void init() {
        shader_ = LoadShaderFromMemory(kLitVS, kLitFS);
        shader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader_, "matModel");
        shader_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader_, "matNormal");
        viewPosLoc_ = GetShaderLocation(shader_, "viewPos");
        material_ = LoadMaterialDefault();
        material_.shader = shader_;
        pieces_.load();

        camera_.up = {0, 1, 0};
        camera_.target = {0, 0, 0};
        camera_.fovy = 45;
        camera_.projection = CAMERA_PERSPECTIVE;
        reset();
        // Opening a room link (…/#CODE) skips the menu and joins or resumes that room.
        const std::string code = net::urlCode();
        if (net::available() && code.size() == kCodeLength) openRoom(code);
    }

    void shutdown() {
        pieces_.unload();
        UnloadShader(shader_);
    }

    void frame() {
        update(GetFrameTime());
        draw();
    }

private:
    Position pos_;
    std::vector<Position> history_;
    std::vector<Move> moves_;  // moves played, parallel to history_
    std::vector<Move> legal_;
    Status status_ = Status::Playing;
    int reps_ = 1;  // occurrences of the current position, this one included
    int selected_ = -1, hovered_ = -1;
    int promoTo_ = -1, promoHover_ = -1;  // promotion picker: target square (-1 = closed), hovered slot
    int animFrom_ = -1, animTo_ = -1;
    float animT_ = 1;

    // Battle mode (offline only): every capture is a battle, check no longer restricts moves, and a
    // game is won by killing the enemy king. Pieces carry damage between battles, kept per square.
    bool battleMode_ = false;  // menu toggle; applies from the next new game
    bool battleGame_ = false;  // the current game is a battle game
    bool kingDead_ = false;
    std::array<int, 64> dmg_{};
    std::vector<std::array<int, 64>> dmgHistory_;  // parallel to history_
    struct Popup {
        int target;
        std::string text;
        Color color;
        float t = 0;
    };
    struct BattleScene {
        battle::Battle fight;
        Move move;
        std::vector<battle::Event> queue;
        size_t next = 0;
        float eventT = 0;   // seconds into queue[next]
        bool applied = false;  // queue[next]'s HP change has been shown
        int shownHp[2];
        float introT = 0, outroT = 0, aiT = 0;
        int cursor = 0;  // highlighted command
        std::vector<Popup> popups;
    };
    std::unique_ptr<BattleScene> battle_;
    std::array<float, 7> pieceTops_{};

    Camera3D camera_{};
    float yaw_ = 0, targetYaw_ = 0, pitch_ = 0.85f, dist_ = 11.5f;
    bool autoRotate_ = true;

    bool vsAi_ = false;
    Side human_ = White;
    int level_ = 1;
    bool menuOpen_ = true, gameStarted_ = false;
    Searcher searcher_;
    bool aiStarted_ = false;

    // Online play (web only). The room code is also the page's #fragment, so the URL is the invite.
    enum class Link { Waiting, Connected, Disconnected };
    bool online_ = false, isHost_ = false;
    bool seated_ = false;  // a guest learns its side from the host's first sync
    Link link_ = Link::Waiting;
    std::string room_, netError_;
    bool typingCode_ = false;
    std::string codeInput_;

    Shader shader_{};
    int viewPosLoc_ = -1;
    Material material_{};
    PieceSet pieces_;

    void newGame(int mode) {
        leaveRoom();
        vsAi_ = mode != kHotseat;
        human_ = mode == kPlayBlack || (mode == kPlayRandom && GetRandomValue(0, 1)) ? Black : White;
        menuOpen_ = false;
        gameStarted_ = true;
        battleGame_ = battleMode_;
        reset();
        faceSideToMove();
    }

    void reset() {
        pos_ = Position::start();
        history_.clear();
        moves_.clear();
        dmg_.fill(0);
        dmgHistory_.clear();
        battle_.reset();
        clearSelection();
        animT_ = 1;
        refresh();
    }

    void clearSelection() {
        selected_ = -1;
        promoTo_ = -1;
    }

    void refresh() {
        legal_ = battleGame_ ? pos_.pseudoLegalMoves() : pos_.legalMoves();
        const bool check = pos_.inCheck(pos_.side);
        // In a battle game an attacked king can still fight, so no position is checkmate.
        status_ = legal_.empty() ? (check && !battleGame_ ? Status::Checkmate : Status::Stalemate)
                                 : (check ? Status::Check : Status::Playing);
        kingDead_ = pos_.kingSquare(White) < 0 || pos_.kingSquare(Black) < 0;
        const PositionKey key = pos_.key();
        reps_ = 1;
        for (const Position& p : history_) reps_ += p.key() == key;
        // FIDE 9.6.1: the fifth occurrence ends the game without a claim.
        if (!gameOver() && reps_ >= 5) status_ = Status::Repetition;
        searcher_.cancel();
        aiStarted_ = false;
        if (autoRotate_) faceSideToMove();
    }

    void faceSideToMove() {
        const Side view = vsAi_ || (online_ && seated_) ? human_ : pos_.side;
        const float want = view == White ? 0.0f : PI;
        targetYaw_ = yaw_ + wrapAngle(want - yaw_);
    }

    void play(const Move& m) {
        record(m);
        advance(m);
    }

    void record(const Move& m) {
        history_.push_back(pos_);
        dmgHistory_.push_back(dmg_);
        moves_.push_back(m);
    }

    void advance(const Move& m) {
        carryDamage(m);
        pos_ = pos_.apply(m);
        animFrom_ = m.from;
        animTo_ = m.to;
        animT_ = 0;
        clearSelection();
        refresh();
    }

    // Damage travels with the piece; a captured piece's damage is dropped, a promoted pawn is healed.
    void carryDamage(const Move& m) {
        dmg_[capturedSquare(m)] = 0;
        dmg_[m.to] = m.promo != EMPTY ? 0 : dmg_[m.from];
        if (m.to != m.from) dmg_[m.from] = 0;
        if (m.flags & CASTLE) {
            const int rank = rankOf(m.from) * 8;
            const bool kingSide = fileOf(m.to) == 6;
            const int rookFrom = rank + (kingSide ? 7 : 0), rookTo = rank + (kingSide ? 5 : 3);
            dmg_[rookTo] = dmg_[rookFrom];
            dmg_[rookFrom] = 0;
        }
    }

    int hpAt(int s) const { return battle::maxHp(typeOf(pos_.sq[s])) - dmg_[s]; }

    bool startsBattle(const Move& m) const {
        return battleGame_ && (m.flags & CAPTURE);
    }

    // Plays a move, or opens a battle first when it is a capture in battle mode.
    void perform(const Move& m) {
        if (!startsBattle(m)) {
            play(m);
            return;
        }
        const int cap = capturedSquare(m);
        const int8_t a = pos_.sq[m.from];
        auto scene = std::unique_ptr<BattleScene>(new BattleScene{
            battle::Battle(typeOf(a), a > 0, hpAt(m.from), typeOf(pos_.sq[cap]), hpAt(cap),
                           static_cast<uint32_t>(GetRandomValue(0, INT_MAX))),
            m});
        scene->shownHp[0] = scene->fight.fighter(0).hp;
        scene->shownHp[1] = scene->fight.fighter(1).hp;
        battle_ = std::move(scene);
        clearSelection();
        searcher_.cancel();
    }

    void finishBattle() {
        const battle::Battle& f = battle_->fight;
        const Move m = battle_->move;
        const bool attackerWon = f.winner() == 0;
        const int left = f.fighter(f.winner()).hp, type = f.fighter(f.winner()).type;
        battle_.reset();
        record(m);
        if (attackerWon) {
            dmg_[m.from] = battle::maxHp(type) - left;
            advance(m);
            return;
        }
        dmg_[capturedSquare(m)] = battle::maxHp(type) - left;
        dmg_[m.from] = 0;
        pos_ = repelled(pos_, m);
        animT_ = 1;
        clearSelection();
        refresh();
    }

    void undo() {
        if (history_.empty()) return;
        // Against the AI, take back the AI's reply too, so it is the player's move again.
        do {
            pos_ = history_.back();
            history_.pop_back();
            dmg_ = dmgHistory_.back();
            dmgHistory_.pop_back();
            moves_.pop_back();
        } while (vsAi_ && pos_.side != human_ && !history_.empty());
        clearSelection();
        animT_ = 1;
        refresh();
    }

    bool aiTurn() const { return vsAi_ && pos_.side != human_ && !gameOver() && !menuOpen_; }
    // True when clicks on the board should do nothing: the AI or the online opponent is to move.
    bool inputLocked() const { return aiTurn() || (online_ && (!seated_ || pos_.side != human_)); }

    // A move made by the local player; online it also goes to the opponent.
    void commit(const Move& m) {
        perform(m);
        if (!online_) return;
        net::send("move " + std::to_string(moves_.size() - 1) + " " + toUci(m));
        saveRoom();
    }

    std::string moveList() const {
        std::string s;
        for (const Move& m : moves_) s += " " + toUci(m);
        return s;
    }

    // Replays `uci` moves from the start position; stops at the first one that is not legal.
    void replay(const std::vector<std::string>& uci) {
        reset();
        for (const std::string& u : uci) {
            const Move m = pos_.parseUci(u);
            if (m.from < 0) break;
            play(m);
        }
        animT_ = 1;
    }

    // Stored per room: "<host|guest> <w|b|-> <moves...>", so a reload can resume the game.
    void saveRoom() const {
        const char side = !seated_ ? '-' : human_ == White ? 'w' : 'b';
        net::save("chess3d:" + room_, std::string(isHost_ ? "host " : "guest ") + side + moveList());
    }

    void enterRoom(const std::string& code) {
        vsAi_ = false;
        battleGame_ = false;
        online_ = true;
        room_ = code;
        link_ = Link::Waiting;
        netError_.clear();
        menuOpen_ = typingCode_ = false;
        gameStarted_ = true;
        net::setUrlCode(code);
    }

    void createRoom() {
        leaveRoom();
        enterRoom(net::newCode());
        isHost_ = seated_ = true;
        human_ = GetRandomValue(0, 1) ? Black : White;
        reset();
        faceSideToMove();
        saveRoom();
        net::host(room_, false);
    }

    // Opens a room from a link or typed code: resumes our seat if this browser was in it before.
    void openRoom(const std::string& code) {
        leaveRoom();
        enterRoom(code);
        std::istringstream in(net::load("chess3d:" + code));
        std::string role, side, u;
        in >> role >> side;
        std::vector<std::string> moves;
        while (in >> u) moves.push_back(u);
        isHost_ = role == "host";
        seated_ = side == "w" || side == "b";
        human_ = side == "b" ? Black : White;
        replay(moves);
        faceSideToMove();
        if (isHost_) net::host(code, true);
        else net::join(code);
    }

    void leaveRoom() {
        if (!online_) return;
        net::leave();
        net::setUrlCode("");
        online_ = isHost_ = seated_ = false;
    }

    void sendSync() {
        const char* side = !isHost_ ? "-" : human_ == White ? "w" : "b";
        net::send(std::string("sync ") + side + moveList());
    }

    // Both sides send "sync <host side|-> <moves>" on connect and keep the longer history when the
    // shorter one is a prefix of it; if the histories disagree, the host's wins.
    void onSync(std::istringstream& in) {
        std::string side, u;
        in >> side;
        std::vector<std::string> theirs;
        while (in >> u) theirs.push_back(u);
        if (!isHost_) {
            human_ = side == "w" ? Black : White;
            seated_ = true;
        }
        std::vector<std::string> ours;
        for (const Move& m : moves_) ours.push_back(toUci(m));
        const bool prefix = std::equal(ours.begin(), ours.begin() + std::min(ours.size(), theirs.size()), theirs.begin());
        if ((prefix && theirs.size() > ours.size()) || (!prefix && !isHost_)) replay(theirs);
        faceSideToMove();
        saveRoom();
    }

    void pollNet() {
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
                if (netError_ == "unavailable-id" && isHost_ && moves_.empty()) createRoom();
            } else if (kind == "full") {
                leaveRoom();
                menuOpen_ = true;
                netError_ = "That room already has two players";
            } else if (kind == "sync") {
                onSync(in);
            } else if (kind == "move") {
                int ply = -1;
                std::string u;
                in >> ply >> u;
                const Move m = pos_.parseUci(u);
                if (ply == static_cast<int>(moves_.size()) && seated_ && pos_.side != human_ && m.from >= 0) {
                    clearSelection();
                    play(m);
                    saveRoom();
                }
            } else if (kind == "claim") {
                if (reps_ >= 3) status_ = Status::Repetition;
            }
        }
    }

    // The engine only knows legal chess. In a battle game, attack an exposed king first (with the
    // healthiest attacker), and with no legal move left fall back to any pseudo-legal one, captures first.
    bool battleOverride(Move& out) const {
        if (!battleGame_) return false;
        const int king = pos_.kingSquare(pos_.side == White ? Black : White);
        const Move* best = nullptr;
        for (const Move& m : legal_)
            if (m.to == king && (!best || hpAt(m.from) > hpAt(best->from))) best = &m;
        if (best) {
            out = *best;
            return true;
        }
        if (legal_.empty() || !pos_.legalMoves().empty()) return false;
        std::vector<Move> captures;
        for (const Move& m : legal_)
            if (m.flags & CAPTURE) captures.push_back(m);
        const std::vector<Move>& pool = captures.empty() ? legal_ : captures;
        out = pool[GetRandomValue(0, static_cast<int>(pool.size()) - 1)];
        return true;
    }

    void updateAi() {
        if (!aiTurn()) return;
        Move forced;
        if (!aiStarted_ && battleOverride(forced)) {
            if (animT_ >= 1) perform(forced);
            return;
        }
        if (!aiStarted_) {
            std::vector<uint64_t> hashes;
            for (const Position& p : history_) hashes.push_back(hashOf(p));
            searcher_.start(pos_, std::move(hashes), kLevels[level_].limits);
            aiStarted_ = true;
        }
        if (searcher_.running()) searcher_.step(kAiSliceMs);
        // Let the player's move finish animating before the reply starts.
        if (!searcher_.running() && animT_ >= 1) perform(searcher_.best());
    }

    std::vector<Button> menuButtons() const {
        const float cx = GetScreenWidth() / 2.0f, cy = GetScreenHeight() / 2.0f;
        const float gap = 12, h = 44;
        const float w = fminf(140, (GetScreenWidth() - 32 - 3 * gap) / 4);  // four buttons fit a phone-width window
        auto row = [&](int i, int n, float y) { return Rectangle{cx - (n * w + (n - 1) * gap) / 2 + i * (w + gap), y, w, h}; };
        std::vector<Button> b = {
            {row(0, 3, cy - 40), kLevels[0].name, kLevelEasy},
            {row(1, 3, cy - 40), kLevels[1].name, kLevelMedium},
            {row(2, 3, cy - 40), kLevels[2].name, kLevelHard},
            {row(0, 4, cy + 50), "Hotseat", kHotseat},
            {row(1, 4, cy + 50), "Play White", kPlayWhite},
            {row(2, 4, cy + 50), "Play Black", kPlayBlack},
            {row(3, 4, cy + 50), "Random", kPlayRandom},
        };
        Rectangle toggle = row(0, 1, cy + 106);
        toggle.x -= w / 2 + gap / 2;
        toggle.width = 2 * w + gap;
        b.push_back({toggle, battleMode_ ? "Battle mode: ON" : "Battle mode: off", kBattleToggle});
        if (net::available()) {
            b.push_back({row(0, 2, cy + 200), "Create room", kCreateRoom});
            b.push_back({row(1, 2, cy + 200), "Join room", kJoinRoom});
        }
        return b;
    }

    void updateCodeInput() {
        if (IsKeyPressed(KEY_ESCAPE)) {
            typingCode_ = false;
            return;
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !codeInput_.empty()) codeInput_.pop_back();
        for (int c = GetCharPressed(); c; c = GetCharPressed())
            if (c < 128 && std::isalnum(c) && codeInput_.size() < kCodeLength)
                codeInput_ += static_cast<char>(std::toupper(c));
        if (IsKeyPressed(KEY_ENTER) && codeInput_.size() == kCodeLength) openRoom(codeInput_);
    }

    void updateMenu() {
        if (typingCode_) {
            updateCodeInput();
            return;
        }
        if (IsKeyPressed(KEY_ESCAPE) && gameStarted_) {
            menuOpen_ = false;
            return;
        }
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        for (const Button& b : menuButtons()) {
            if (!CheckCollisionPointRec(cursorPosition(), b.rect)) continue;
            if (b.id <= kLevelHard) level_ = b.id;
            else if (b.id == kBattleToggle) battleMode_ = !battleMode_;
            else if (b.id == kCreateRoom) createRoom();
            else if (b.id == kJoinRoom) {
                typingCode_ = true;
                codeInput_.clear();
                netError_.clear();
            } else newGame(b.id);
        }
    }

    bool gameOver() const {
        return kingDead_ || status_ == Status::Checkmate || status_ == Status::Stalemate || status_ == Status::Repetition;
    }
    bool canClaimDraw() const { return !gameOver() && !inputLocked() && reps_ >= 3; }

    bool ownPiece(int s) const { return s >= 0 && pos_.sq[s] != EMPTY && sideOf(pos_.sq[s]) == pos_.side; }

    const Move* findMove(int to, int8_t promo = EMPTY) const {
        for (const Move& m : legal_)
            if (m.from == selected_ && m.to == to && m.promo == promo) return &m;
        return nullptr;
    }

    bool isPromotion(int to) const { return findMove(to, QUEEN) != nullptr; }
    bool isTarget(int s) const { return selected_ >= 0 && (findMove(s) || isPromotion(s)); }

    // Picker slots float in a row above the promotion square, laid out along the camera's right axis.
    Vector3 promoSlot(int i) const {
        const Vector3 right{cosf(yaw_), 0, -sinf(yaw_)};
        Vector3 at = squareCenter(promoTo_);
        at.y = 1.3f;
        return Vector3Add(at, Vector3Scale(right, (i - 1.5f) * 0.95f));
    }

    int pickPromoSlot(const Ray& ray) const {
        int best = -1;
        float bestDist = 1e9f;
        for (int i = 0; i < 4; ++i) {
            const RayCollision hit = GetRayCollisionSphere(ray, Vector3Add(promoSlot(i), {0, 0.45f, 0}), 0.45f);
            if (hit.hit && hit.distance < bestDist) {
                best = i;
                bestDist = hit.distance;
            }
        }
        return best;
    }

    void update(float dt) {
#ifdef __EMSCRIPTEN__
        double cssW = 0, cssH = 0;
        emscripten_get_element_css_size("#canvas", &cssW, &cssH);
        if (static_cast<int>(cssW) != GetScreenWidth() || static_cast<int>(cssH) != GetScreenHeight())
            SetWindowSize(static_cast<int>(cssW), static_cast<int>(cssH));
#endif
        // Camera: right-drag orbits, wheel zooms; otherwise ease toward the side to move.
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            const Vector2 d = GetMouseDelta();
            yaw_ -= d.x * 0.006f;
            pitch_ = Clamp(pitch_ + d.y * 0.006f, 0.25f, 1.45f);
            targetYaw_ = yaw_;
        } else {
            yaw_ += (targetYaw_ - yaw_) * fminf(1.0f, dt * 5.0f);
        }
        dist_ = Clamp(dist_ - GetMouseWheelMove() * 0.8f, 6.0f, 22.0f);
        // Back off in narrow (portrait / tiled) windows so the whole board stays in view.
        const float aspect = static_cast<float>(GetScreenWidth()) / fmaxf(1.0f, static_cast<float>(GetScreenHeight()));
        const float d = dist_ * fmaxf(1.0f, 1.3f / aspect);
        camera_.position = {d * cosf(pitch_) * sinf(yaw_), d * sinf(pitch_), d * cosf(pitch_) * cosf(yaw_)};

        hovered_ = -1;
        promoHover_ = -1;
        pollNet();
        if (menuOpen_) {
            updateMenu();
            return;
        }
        if (IsKeyPressed(KEY_R)) {
            menuOpen_ = true;
            clearSelection();
            return;
        }
        if (battle_) {
            updateBattle(dt);
            return;
        }
        if ((IsKeyPressed(KEY_U) || IsKeyPressed(KEY_BACKSPACE)) && !online_) undo();  // no take-backs online
        if (IsKeyPressed(KEY_C) && online_) net::copyLink();
        if (IsKeyPressed(KEY_F)) targetYaw_ += PI;
        if (IsKeyPressed(KEY_D) && canClaimDraw()) {
            clearSelection();
            status_ = Status::Repetition;
            if (online_) net::send("claim");
        }
        if (IsKeyPressed(KEY_A)) {
            autoRotate_ = !autoRotate_;
            if (autoRotate_) faceSideToMove();
        }

        if (animT_ < 1) animT_ = fminf(1.0f, animT_ + dt / kAnimSeconds);
        updateAi();
        if (inputLocked()) return;

        const Ray ray = GetScreenToWorldRay(cursorPosition(), camera_);
        if (promoTo_ >= 0) {
            promoHover_ = pickPromoSlot(ray);
        } else if (ray.direction.y < -1e-4f) {
            const float t = -ray.position.y / ray.direction.y;
            hovered_ = squareAt(Vector3Add(ray.position, Vector3Scale(ray.direction, t)));
        }

        const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (promoTo_ >= 0) {
            // Esc or a click off the picker cancels; the pawn stays selected.
            if (IsKeyPressed(KEY_ESCAPE) || (click && promoHover_ < 0)) promoTo_ = -1;
            else if (click) commit(*findMove(promoTo_, kPromoChoices[promoHover_]));
        } else if (click && !gameOver()) {
            if (hovered_ < 0) {
                selected_ = -1;
            } else if (isPromotion(hovered_)) {
                promoTo_ = hovered_;
            } else if (const Move* m = selected_ >= 0 ? findMove(hovered_) : nullptr) {
                commit(*m);
            } else if (ownPiece(hovered_) && hovered_ != selected_) {
                selected_ = hovered_;
            } else {
                selected_ = -1;
            }
        }
    }

    // ---- Battle mode ----

    std::string fighterName(int i) const {
        const battle::Fighter& f = battle_->fight.fighter(i);
        return std::string(f.white ? "White " : "Black ") + battle::statsOf(f.type).name;
    }

    bool aiFighter(int i) const { return vsAi_ && (battle_->fight.fighter(i).white ? White : Black) != human_; }

    static float eventSeconds(const battle::Event& e) {
        return e.kind == battle::Event::Announce || e.kind == battle::Event::Status ? kBattleEventSeconds * 0.65f : kBattleEventSeconds;
    }

    // Bottom command menu: a 2x2 grid in battle::Command order.
    Rectangle commandRect(int i) const {
        const float total = fminf(GetScreenWidth() - 32.0f, 920), gap = 8, h = 60;
        const float w = (total - gap) / 2;
        const float x0 = (GetScreenWidth() - total) / 2, y0 = GetScreenHeight() - 16 - 2 * h - gap;
        return {x0 + (i % 2) * (w + gap), y0 + (i / 2) * (h + gap), w, h};
    }

    static std::string commandLabel(int i, int type) {
        const battle::Stats& st = battle::statsOf(type);
        switch (i) {
            case 0: return "Attack";
            case 1: return st.skill;
            case 2: return st.skill2;
            default: return "Guard";
        }
    }

    static const char* commandInfo(int i, int type) {
        const battle::Stats& st = battle::statsOf(type);
        switch (i) {
            case 0: return "1.0x power";
            case 1: return st.skillInfo;
            case 2: return st.skill2Info;
            default: return "half damage until your next turn, heal 5%";
        }
    }

    bool awaitingHuman() const {
        const BattleScene& b = *battle_;
        return b.introT >= kBattleIntroSeconds && b.next >= b.queue.size() && !b.fight.over() && !aiFighter(b.fight.turn());
    }

    void enqueue(std::vector<battle::Event> events) {
        BattleScene& b = *battle_;
        b.queue = std::move(events);
        b.next = 0;
        b.eventT = 0;
        b.applied = false;
    }

    void updateBattle(float dt) {
        BattleScene& b = *battle_;
        for (Popup& p : b.popups) p.t += dt;
        b.popups.erase(std::remove_if(b.popups.begin(), b.popups.end(), [](const Popup& p) { return p.t > 1.2f; }),
                       b.popups.end());
        if (b.introT < kBattleIntroSeconds) {
            b.introT += dt;
            return;
        }
        if (b.next < b.queue.size()) {
            const battle::Event& e = b.queue[b.next];
            b.eventT += dt;
            if (!b.applied && b.eventT >= eventSeconds(e) * 0.45f) {
                b.applied = true;
                b.shownHp[0] = e.hp[0];
                b.shownHp[1] = e.hp[1];
                using K = battle::Event;
                if (e.kind == K::Hit) b.popups.push_back({e.target, TextFormat(e.crit ? "%d!" : "%d", e.amount), e.crit ? kSelect : RAYWHITE});
                if (e.kind == K::Miss) b.popups.push_back({e.target, "MISS", Fade(RAYWHITE, 0.7f)});
                if ((e.kind == K::Heal || e.kind == K::Guard) && e.amount > 0) b.popups.push_back({e.target, TextFormat("+%d", e.amount), kHeal});
                // A tag follows a heal number a moment later so the two don't overlap.
                if (e.tag) b.popups.push_back({e.target, e.tag, e.kind == K::Status ? kCheck : SKYBLUE, e.amount > 0 ? -0.35f : 0.0f});
            }
            if (b.eventT >= eventSeconds(e)) {
                ++b.next;
                b.eventT = 0;
                b.applied = false;
            }
            return;
        }
        const bool confirm = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
        if (b.fight.over()) {
            b.outroT += dt;
            if (b.outroT > 0.8f && confirm) finishBattle();
            return;
        }
        if (aiFighter(b.fight.turn())) {
            b.aiT += dt;
            if (b.aiT >= kBattleAiDelay) {
                b.aiT = 0;
                enqueue(b.fight.act(b.fight.aiChoice()));
            }
            return;
        }
        int chosen = -1;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT)) b.cursor ^= 1;
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN)) b.cursor ^= 2;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) chosen = b.cursor;
        for (int i = 0; i < 4; ++i) {
            if (IsKeyPressed(KEY_ONE + i)) chosen = i;
            if (CheckCollisionPointRec(cursorPosition(), commandRect(i))) {
                b.cursor = i;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) chosen = i;
            }
        }
        const auto command = static_cast<battle::Command>(chosen);
        if (chosen >= 0 && b.fight.canUse(command)) enqueue(b.fight.act(command));
    }

    static float fighterX(int i) { return i == 0 ? -1.9f : 1.9f; }

    static constexpr float kFighterScale = 1.7f;

    // Height of a piece's model at scale 1, for placing labels over it.
    float pieceTop(int type) {
        float& top = pieceTops_[type];
        if (top <= 0)
            for (const PiecePart& part : pieces_.parts(type)) {
                const BoundingBox bb = GetMeshBoundingBox(part.mesh);
                top = fmaxf(top, fmaxf(Vector3Transform(bb.min, part.local).y, Vector3Transform(bb.max, part.local).y));
            }
        return top;
    }

    // Returns where the fighter was drawn, for the overhead HP bar.
    Vector3 drawFighter(int i) {
        const BattleScene& b = *battle_;
        const battle::Fighter& f = b.fight.fighter(i);
        const float dir = i == 0 ? 1.0f : -1.0f;
        Vector3 at{fighterX(i), 0, 0};
        Color c = f.white ? kWhitePiece : kBlackPiece;
        const float scale = kFighterScale;
        const float intro = fminf(1.0f, b.introT / (kBattleIntroSeconds * 0.7f));
        at.x -= dir * (1 - intro * intro * (3 - 2 * intro)) * 5;
        if (b.next < b.queue.size()) {
            using K = battle::Event;
            const K& e = b.queue[b.next];
            const float t = b.eventT / eventSeconds(e);
            if (e.actor == i && (e.kind == K::Hit || e.kind == K::Miss)) at.x += dir * sinf(PI * fminf(1.0f, t * 1.6f)) * 1.5f;
            if (e.actor == i && e.kind == K::Announce) at.y += sinf(PI * t) * 0.4f;
            if (e.target == i && e.kind == K::Hit && t > 0.4f) {
                at.x += sinf(t * 70) * 0.1f * (1 - t);
                c = mix(c, kCheck, 0.7f * (1 - t));
            }
            if (e.target == i && e.kind == K::Miss && t > 0.3f) at.z += sinf(PI * (t - 0.3f) / 0.7f) * 0.7f;
            if (e.actor == i && (e.kind == K::Heal || e.kind == K::Guard))
                c = mix(c, e.kind == K::Heal ? kHeal : SKYBLUE, 0.6f * sinf(PI * t));
        }
        if (b.fight.over() && b.next >= b.queue.size() && b.fight.winner() != i) {
            const float k = fminf(1.0f, b.outroT / 0.8f);
            at.y -= k * 1.4f * scale;  // sinks through the arena floor
            c = mix(c, {20, 20, 20, 255}, k);
        }
        if (b.introT >= kBattleIntroSeconds && b.next >= b.queue.size() && !b.fight.over() && b.fight.turn() == i) {
            const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(GetTime()) * 6);
            drawMesh(pieces_.disc(), box({0.75f, 0.02f, 0.75f}, {at.x, 0.01f, at.z}), mix(kFrame, kSelect, pulse));
        }
        if (f.guard > 0 && b.next >= b.queue.size())
            drawMesh(pieces_.disc(), box({0.85f, 0.015f, 0.85f}, {at.x, 0.005f, at.z}), f.guard == 2 ? kSelect : SKYBLUE);
        // Board pieces face -z for White; turn them side-on, facing each other.
        drawPiece(f.type, at, i == 0 ? -PI / 2 : PI / 2, c, scale);
        return at;
    }

    void drawPanel(Rectangle r) const {
        DrawRectangleRounded(r, 0.12f, 6, kPanel);
        DrawRectangleRoundedLinesEx(r, 0.12f, 6, 2, kPanelEdge);
    }

    void drawHpBar(float x, float y, float w, float h, int hp, int max) const {
        const float frac = Clamp(static_cast<float>(hp) / fmaxf(1.0f, static_cast<float>(max)), 0, 1);
        const Color fill = frac > 0.5f ? kHeal : frac > 0.25f ? kSelect : kCheck;
        DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), Fade(BLACK, 0.7f));
        DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w * frac), static_cast<int>(h), fill);
    }

    void drawBattle() {
        const BattleScene& b = *battle_;
        const int W = GetScreenWidth(), H = GetScreenHeight();
        ClearBackground(BLACK);  // also clears depth
        DrawRectangleGradientV(0, 0, W, H, {52, 34, 86, 255}, {12, 10, 22, 255});

        const float aspect = static_cast<float>(W) / fmaxf(1.0f, static_cast<float>(H));
        const float back = fmaxf(1.0f, 1.5f / aspect);  // keep both fighters in narrow windows
        Camera3D cam{};
        cam.position = {0, 2.4f * back, 8.6f * back};
        cam.target = {0, 0.35f, 0};  // below the fighters, so they sit above the bottom panels
        cam.up = {0, 1, 0};
        cam.fovy = 40;
        cam.projection = CAMERA_PERSPECTIVE;
        SetShaderValue(shader_, viewPosLoc_, &cam.position, SHADER_UNIFORM_VEC3);
        BeginMode3D(cam);
        drawMesh(pieces_.disc(), box({4.4f, 0.3f, 4.4f}, {0, -0.3f, 0}), kFrame);
        drawMesh(pieces_.disc(), box({4.1f, 0.3f, 4.1f}, {0, -0.29f, 0}), {86, 70, 110, 255});
        Vector3 anchor[2];
        for (int i = 0; i < 2; ++i) anchor[i] = drawFighter(i);
        EndMode3D();

        // Name, status tags and HP bar over each fighter.
        for (int i = 0; i < 2; ++i) {
            const battle::Fighter& f = b.fight.fighter(i);
            if (b.introT < kBattleIntroSeconds * 0.7f) break;  // still sliding in
            if (b.fight.over() && b.next >= b.queue.size() && b.fight.winner() != i) continue;  // sinking out
            const Vector2 p = GetWorldToScreen({anchor[i].x, anchor[i].y + pieceTop(f.type) * kFighterScale + 0.2f, anchor[i].z}, cam);
            const int max = battle::maxHp(f.type);
            const bool active = !b.fight.over() && b.fight.turn() == i && b.next >= b.queue.size();
            const std::string name = fighterName(i);
            const float w = 180, x = p.x - w / 2;
            DrawText(name.c_str(), static_cast<int>(p.x) - MeasureText(name.c_str(), 20) / 2, static_cast<int>(p.y - 46), 20,
                     active ? kSelect : RAYWHITE);
            drawHpBar(x, p.y - 22, w, 12, b.shownHp[i], max);
            DrawRectangleLines(static_cast<int>(x) - 1, static_cast<int>(p.y) - 23, static_cast<int>(w) + 2, 14, Fade(RAYWHITE, 0.5f));
            const char* hp = TextFormat("%d/%d", b.shownHp[i], max);
            DrawText(hp, static_cast<int>(p.x) - MeasureText(hp, 16) / 2, static_cast<int>(p.y - 6), 16, RAYWHITE);
            std::string tags;
            if (f.guard == 2) tags += "BULWARK  ";
            if (f.guard == 1) tags += "GUARD  ";
            if (f.evading) tags += "EVADE  ";
            if (f.empowered) tags += "EMPOWERED  ";
            if (f.weakened) tags += "WEAKENED  ";
            if (!tags.empty()) {
                tags.resize(tags.size() - 2);
                DrawText(tags.c_str(), static_cast<int>(p.x) - MeasureText(tags.c_str(), 14) / 2, static_cast<int>(p.y - 64), 14,
                         f.weakened ? kCheck : SKYBLUE);
            }
        }

        for (const Popup& p : b.popups) {
            if (p.t < 0) continue;  // delayed
            // Mid-body, so popups stay below the overhead HP bar.
            const float mid = pieceTop(b.fight.fighter(p.target).type) * kFighterScale * 0.5f;
            const Vector2 at = GetWorldToScreen({fighterX(p.target), mid, 0}, cam);
            const int size = 40, tw = MeasureText(p.text.c_str(), size);
            const float a = p.t < 0.8f ? 1.0f : 1 - (p.t - 0.8f) / 0.4f;
            const int x = static_cast<int>(at.x) - tw / 2, y = static_cast<int>(at.y - p.t * 40);
            DrawText(p.text.c_str(), x + 2, y + 2, size, Fade(BLACK, a));
            DrawText(p.text.c_str(), x, y, size, Fade(p.color, a));
        }

        // Message box along the top.
        std::string msg;
        if (b.introT < kBattleIntroSeconds) msg = fighterName(0) + " attacks " + fighterName(1) + "!";
        else if (b.next < b.queue.size()) msg = b.queue[b.next].text;
        else if (b.fight.over()) msg = fighterName(b.fight.winner()) + (b.fight.winner() == 0 ? " wins and takes the square!" : " holds its ground - the capture fails!");
        else if (aiFighter(b.fight.turn())) msg = fighterName(b.fight.turn()) + " is deciding...";
        else msg = fighterName(b.fight.turn()) + " - choose a command";
        const Rectangle top{16, 16, W - 32.0f, 52};
        drawPanel(top);
        DrawText(msg.c_str(), 32, 32, 22, RAYWHITE);

        // Command menu.
        const bool live = awaitingHuman();
        const battle::Fighter& actor = b.fight.fighter(b.fight.turn());
        for (int i = 0; i < 4; ++i) {
            const Rectangle r = commandRect(i);
            const bool usable = b.fight.canUse(static_cast<battle::Command>(i));
            const bool hot = live && i == b.cursor && usable;
            DrawRectangleRounded(r, 0.2f, 6, hot ? mix(kPanel, kSelect, 0.35f) : kPanel);
            DrawRectangleRoundedLinesEx(r, 0.2f, 6, 2, hot ? kSelect : Fade(kPanelEdge, live ? 1.0f : 0.35f));
            std::string label = TextFormat("%d  %s", i + 1, commandLabel(i, actor.type).c_str());
            if (!usable) label += TextFormat("  (ready in %d)", actor.cooldown[i - 1]);
            const Color text = !live ? Fade(RAYWHITE, 0.35f) : usable ? RAYWHITE : Fade(RAYWHITE, 0.4f);
            DrawText(label.c_str(), static_cast<int>(r.x + 14), static_cast<int>(r.y + 9), 20, text);
            DrawText(commandInfo(i, actor.type), static_cast<int>(r.x + 38), static_cast<int>(r.y + 35), 16, Fade(text, 0.7f));
        }

        // Banners.
        auto banner = [&](const char* big, const char* small, float scale) {
            const int size = static_cast<int>(64 * scale);
            const int y = 84;  // just under the message box, above the fighters
            DrawText(big, (W - MeasureText(big, size)) / 2 + 3, y + 3, size, Fade(BLACK, 0.6f));
            DrawText(big, (W - MeasureText(big, size)) / 2, y, size, kSelect);
            if (small) DrawText(small, (W - MeasureText(small, 22)) / 2, y + size + 8, 22, RAYWHITE);
        };
        if (b.introT < kBattleIntroSeconds) {
            const float k = fminf(1.0f, b.introT / 0.25f);
            banner("BATTLE!", (fighterName(0) + "  vs  " + fighterName(1)).c_str(), 0.5f + 0.5f * k);
        } else if (b.fight.over() && b.next >= b.queue.size()) {
            banner(b.fight.winner() == 0 ? "VICTORY" : "REPELLED", b.outroT > 0.8f ? "click or press Enter to continue" : nullptr, 1);
        }
    }

    // Small HP bars over wounded pieces on the board.
    void drawHpBars() {
        if (!battleGame_) return;
        for (int s = 0; s < 64; ++s) {
            if (pos_.sq[s] == EMPTY || dmg_[s] <= 0) continue;
            Vector3 at = squareCenter(s);
            at.y = 1.5f;
            const Vector2 p = GetWorldToScreen(at, camera_);
            drawHpBar(p.x - 20, p.y, 40, 6, hpAt(s), battle::maxHp(typeOf(pos_.sq[s])));
        }
    }

    void drawMesh(Mesh mesh, Matrix transform, Color c) {
        material_.maps[MATERIAL_MAP_DIFFUSE].color = c;
        DrawMesh(mesh, material_, transform);
    }

    Matrix box(Vector3 size, Vector3 at) {
        return MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(at.x, at.y, at.z));
    }

    Color squareColor(int s) const {
        Color c = (fileOf(s) + rankOf(s)) % 2 == 0 ? kDarkSquare : kLightSquare;
        if (!moves_.empty() && (s == moves_.back().from || s == moves_.back().to)) c = mix(c, kLastMove, 0.45f);
        if (s == selected_ || s == promoTo_) c = mix(c, kSelect, 0.6f);
        if ((status_ == Status::Check || status_ == Status::Checkmate) && s == pos_.kingSquare(pos_.side))
            c = mix(c, kCheck, 0.65f);
        if (s == hovered_ && (ownPiece(s) || isTarget(s))) c = mix(c, WHITE, 0.25f);
        return c;
    }

    void drawBoard() {
        drawMesh(pieces_.cube(), box({40, 0.1f, 40}, {0, -0.45f, 0}), kTable);
        drawMesh(pieces_.cube(), box({9, 0.3f, 9}, {0, -0.16f, 0}), kFrame);
        for (int s = 0; s < 64; ++s) {
            Vector3 c = squareCenter(s);
            drawMesh(pieces_.cube(), box({1, 0.2f, 1}, {c.x, -0.1f, c.z}), squareColor(s));
        }
        if (selected_ < 0 || promoTo_ >= 0) return;
        for (const Move& m : legal_) {
            // A promotion appears four times (one per piece); mark its square once.
            if (m.from != selected_ || (m.promo != EMPTY && m.promo != QUEEN)) continue;
            Vector3 c = squareCenter(m.to);
            const bool capture = m.flags & CAPTURE;
            const float r = capture ? 0.46f : 0.14f;
            drawMesh(pieces_.disc(), box({r, 0.02f, r}, {c.x, 0.0f, c.z}), capture ? kCheck : kTarget);
        }
    }

    void drawPieces() {
        for (int s = 0; s < 64; ++s) {
            const int8_t p = pos_.sq[s];
            if (p == EMPTY) continue;
            Vector3 at = squareCenter(s);
            if (animT_ < 1 && s == animTo_) {
                const float e = animT_ * animT_ * (3 - 2 * animT_);  // smoothstep
                at = Vector3Lerp(squareCenter(animFrom_), squareCenter(animTo_), e);
                at.y += sinf(PI * e) * (typeOf(p) == KNIGHT ? 0.6f : 0.15f);
            }
            if (s == selected_) at.y += 0.12f;
            drawPiece(typeOf(p), at, p > 0 ? 0.0f : PI, p > 0 ? kWhitePiece : kBlackPiece);
        }
    }

    void drawPiece(int type, Vector3 at, float rotY, Color c, float scale = 1) {
        const Matrix world = MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixRotateY(rotY)),
                                            MatrixTranslate(at.x, at.y, at.z));
        for (const PiecePart& part : pieces_.parts(type)) drawMesh(part.mesh, MatrixMultiply(part.local, world), c);
    }

    void drawPromotionPicker() {
        if (promoTo_ < 0) return;
        const Color base = pos_.side == White ? kWhitePiece : kBlackPiece;
        for (int i = 0; i < 4; ++i) {
            const bool hot = i == promoHover_;
            Vector3 at = promoSlot(i);
            drawMesh(pieces_.disc(), box({0.4f, 0.04f, 0.4f}, {at.x, at.y - 0.04f, at.z}), hot ? kSelect : kFrame);
            if (hot) at.y += 0.12f;
            // yaw + 90 degrees turns the knight side-on to the camera.
            drawPiece(kPromoChoices[i], at, yaw_ + PI / 2, hot ? mix(base, kSelect, 0.3f) : base);
        }
    }

    void drawLabels() {
        for (int i = 0; i < 8; ++i) {
            const char file[2] = {static_cast<char>('a' + i), 0};
            const char rank[2] = {static_cast<char>('1' + i), 0};
            // Skip the edges facing away from the camera; their labels would sit on top of pieces.
            for (float side : {-1.0f, 1.0f}) {
                if (side * cosf(yaw_) > -0.3f) {
                    Vector2 f = GetWorldToScreen({i - 3.5f, 0, side * 4.25f}, camera_);
                    DrawText(file, static_cast<int>(f.x) - 4, static_cast<int>(f.y) - 8, 16, Fade(RAYWHITE, 0.7f));
                }
                if (side * sinf(yaw_) > -0.3f) {
                    Vector2 r = GetWorldToScreen({side * 4.25f, 0, 3.5f - i}, camera_);
                    DrawText(rank, static_cast<int>(r.x) - 4, static_cast<int>(r.y) - 8, 16, Fade(RAYWHITE, 0.7f));
                }
            }
        }
    }

    void drawHud() {
        const char* side = pos_.side == White ? "White" : "Black";
        const char* winner = pos_.side == White ? "Black" : "White";
        const char* text = nullptr;
        Color color = RAYWHITE;
        if (kingDead_) {
            const char* fallen = pos_.kingSquare(White) < 0 ? "White" : "Black";
            text = TextFormat("The %s king has fallen - %s wins", fallen, fallen[0] == 'W' ? "Black" : "White");
            color = kSelect;
        } else switch (status_) {
            case Status::Playing: text = TextFormat("%s to move", side); break;
            case Status::Check: text = TextFormat("%s to move - CHECK", side); color = kCheck; break;
            case Status::Checkmate: text = TextFormat("Checkmate - %s wins", winner); color = kSelect; break;
            case Status::Stalemate: text = "Stalemate - draw"; color = kSelect; break;
            case Status::Repetition:
                text = reps_ >= 5 ? "Draw - fivefold repetition" : "Draw - threefold repetition claimed";
                color = kSelect;
                break;
        }
        DrawRectangle(12, 12, MeasureText(text, 28) + 24, 44, Fade(BLACK, 0.55f));
        DrawText(text, 24, 20, 28, color);
        const int moveNo = static_cast<int>(moves_.size()) / 2 + 1;
        const char* sub = vsAi_ ? TextFormat("Move %d   vs AI (%s)%s", moveNo, kLevels[level_].name,
                                             aiTurn() ? "   thinking..." : "")
                          : online_ ? TextFormat("Move %d   Room %s   %s   %s   C copy link", moveNo, room_.c_str(),
                                                 !seated_ ? "joining..." : human_ == White ? "you play White" : "you play Black",
                                                 link_ == Link::Connected ? "connected"
                                                 : link_ == Link::Disconnected ? "opponent disconnected"
                                                                               : "waiting for opponent")
                                    : TextFormat("Move %d   hotseat", moveNo);
        if (battleGame_) sub = TextFormat("%s   BATTLE MODE", sub);
        DrawText(sub, 24, 62, 18, Fade(RAYWHITE, 0.6f));
        if (online_ && !netError_.empty())
            DrawText(TextFormat("Network error: %s", netError_.c_str()), 24, 86, 18, kCheck);
        if (promoTo_ >= 0)
            DrawText("Choose a promotion piece  (Esc or click elsewhere to cancel)", 12, GetScreenHeight() - 56, 18, kSelect);
        else if (canClaimDraw())
            DrawText("Threefold repetition - press D to claim a draw", 12, GetScreenHeight() - 56, 18, kSelect);

        const char* help = TextFormat("LMB select/move   RMB drag orbit   Wheel zoom   %sR new game   F flip   A auto-rotate [%s]",
                                      online_ ? "" : "U undo   ", autoRotate_ ? "on" : "off");
        DrawText(help, 12, GetScreenHeight() - 28, 16, Fade(RAYWHITE, 0.55f));
    }

    void drawMenu() {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.55f));
        const auto buttons = menuButtons();
        const int cx = GetScreenWidth() / 2;
        auto centered = [cx](const char* text, float y, int size, Color c) {
            DrawText(text, cx - MeasureText(text, size) / 2, static_cast<int>(y), size, c);
        };
        centered("Chess 3D", buttons[0].rect.y - 110, 48, RAYWHITE);
        centered("AI level", buttons[0].rect.y - 30, 20, Fade(RAYWHITE, 0.7f));
        centered("New game", buttons[3].rect.y - 30, 20, Fade(RAYWHITE, 0.7f));
        const Vector2 mouse = cursorPosition();
        for (const Button& b : buttons) {
            const bool selected = b.id == level_ || (b.id == kBattleToggle && battleMode_);
            const bool hover = CheckCollisionPointRec(mouse, b.rect);
            DrawRectangleRec(b.rect, selected ? kSelect : hover ? Color{70, 70, 84, 255} : Color{44, 44, 54, 255});
            const Color text = selected ? BLACK : RAYWHITE;
            DrawText(b.label, static_cast<int>(b.rect.x + (b.rect.width - MeasureText(b.label, 20)) / 2),
                     static_cast<int>(b.rect.y + 12), 20, text);
        }
        for (const Button& b : buttons)
            if (b.id == kCreateRoom) centered("Online (battle mode is offline only)", b.rect.y - 30, 20, Fade(RAYWHITE, 0.7f));
        const float below = buttons.back().rect.y + buttons.back().rect.height;
        if (!netError_.empty() && !online_) centered(netError_.c_str(), below + 16, 18, kCheck);
        if (gameStarted_) centered("Esc to return to the game", below + 44, 18, Fade(RAYWHITE, 0.55f));
        if (typingCode_) drawCodeInput();
    }

    void drawCodeInput() {
        const int w = GetScreenWidth(), h = GetScreenHeight();
        DrawRectangle(0, 0, w, h, Fade(BLACK, 0.6f));
        std::string shown = codeInput_;
        shown.resize(kCodeLength, '_');
        const char* title = "Enter room code";
        DrawText(title, (w - MeasureText(title, 24)) / 2, h / 2 - 70, 24, RAYWHITE);
        DrawText(shown.c_str(), (w - MeasureText(shown.c_str(), 56)) / 2, h / 2 - 28, 56, kSelect);
        const char* hint = "Enter to join   Esc to cancel";
        DrawText(hint, (w - MeasureText(hint, 18)) / 2, h / 2 + 44, 18, Fade(RAYWHITE, 0.55f));
    }

    void draw() {
        BeginDrawing();
        if (battle_) {
            drawBattle();
            if (menuOpen_) drawMenu();
            EndDrawing();
            return;
        }
        ClearBackground({22, 22, 28, 255});
        SetShaderValue(shader_, viewPosLoc_, &camera_.position, SHADER_UNIFORM_VEC3);
        BeginMode3D(camera_);
        drawBoard();
        drawPieces();
        drawPromotionPicker();
        EndMode3D();
        drawLabels();
        drawHpBars();
        if (menuOpen_) drawMenu();
        else drawHud();
        EndDrawing();
    }
};

App app;
void frame() { app.frame(); }

}  // namespace

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Chess 3D");
    SetExitKey(KEY_NULL);  // Esc cancels the promotion picker instead of quitting
    app.init();
#ifdef __EMSCRIPTEN__
    installPointerTracker();
    emscripten_set_main_loop(frame, 0, 1);
#else
    SetTargetFPS(144);
    while (!WindowShouldClose()) frame();
    app.shutdown();
    CloseWindow();
#endif
    return 0;
}
