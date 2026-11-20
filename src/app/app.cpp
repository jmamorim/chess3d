// App members for game flow (new game, turns, undo, the AI player) and the per-frame update and draw
// dispatch, plus the pointer helpers that differ between desktop and web.
#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "app/app.hpp"
#include "net/net.hpp"
#include "render/shaders.hpp"

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

namespace {

constexpr float kAnimSeconds = 0.3f;
constexpr int kAiSliceMs = 8;  // engine time per frame, so rendering stays smooth while it thinks

float wrapAngle(float a) { return atan2f(sinf(a), cosf(a)); }

}  // namespace

#ifdef __EMSCRIPTEN__
Vector2 cursorPosition() {
    return {pointerU() * GetScreenWidth(), pointerV() * GetScreenHeight()};
}

void startPointerTracking() { installPointerTracker(); }
#else
Vector2 cursorPosition() { return GetMousePosition(); }

void startPointerTracking() {}
#endif

void App::init() {
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
    reset(false, 0);
    // Opening a room link (…/#CODE) skips the menu and joins or resumes that room.
    const std::string code = net::urlCode();
    if (net::available() && code.size() == kCodeLength) openRoom(code);
}

void App::shutdown() {
    pieces_.unload();
    UnloadShader(shader_);
}

void App::frame() {
    update(GetFrameTime());
    draw();
}

void App::newGame(int mode) {
    leaveRoom();
    lastMode_ = mode;
    vsAi_ = mode != kHotseat;
    human_ = mode == kPlayBlack || (mode == kPlayRandom && GetRandomValue(0, 1)) ? Black : White;
    menuOpen_ = false;
    gameStarted_ = true;
    reset(battleMode_, static_cast<uint32_t>(GetRandomValue(0, INT_MAX)));
    faceSideToMove();
}

// Starts an empty game. Online, the seed comes from the room code so both sides roll the same dice.
void App::reset(bool battleGame, uint32_t seedBase) {
    game_.reset(battleGame, seedBase);
    boardPopups_.clear();
    boardFx_.clear();
    arenaFx_.clear();
    endT_ = 0;
    battle_.reset();
    clearSelection();
    animT_ = 1;
    searcher_.cancel();
    aiStarted_ = false;
}

void App::clearSelection() {
    selected_ = -1;
    promoTo_ = -1;
}

// What every change of game state needs on the UI side.
void App::afterTurn() {
    clearSelection();
    searcher_.cancel();
    aiStarted_ = false;
    if (autoRotate_) faceSideToMove();
}

void App::animate(const Move& m) {
    animFrom_ = m.from;
    animTo_ = m.to;
    animT_ = 0;
}

void App::faceSideToMove() {
    const Side view = vsAi_ || (online_ && seated_) ? human_ : pos().side;
    const float want = view == White ? 0.0f : PI;
    targetYaw_ = yaw_ + wrapAngle(want - yaw_);
}

// The piece on s uses an ability instead of moving; the turn passes. which: 0 = support, 1 = strike.
void App::useAbility(int s, int which) {
    const int type = typeOf(pos().sq[s]);
    const std::vector<battle::AbilityEffect> effects = game_.useAbility(s, which);
    boardAbilityFx(s, which, type, effects);
    for (const battle::AbilityEffect& e : effects) {
        if (e.healed > 0) boardPopups_.push_back({e.square, TextFormat("+%d", e.healed), kHeal});
        if (e.buff) boardPopups_.push_back({e.square, buffName(e.buff), SKYBLUE});
        if (e.damage > 0) boardPopups_.push_back({e.square, TextFormat("-%d", e.damage), kCheck});
        for (uint8_t bit = 1; bit <= battle::kCurseWeak; bit <<= 1)
            if (e.curse & bit) boardPopups_.push_back({e.square, battle::curseName(bit), kCurse});
    }
    animT_ = 1;
    afterTurn();
}

const char* App::buffName(uint8_t b) {
    return b & battle::kBulwark ? "BULWARK" : b & battle::kShield ? "SHIELD" : "EMPOWERED";
}

// Plays a move, or opens a battle first when it is a capture in battle mode.
void App::perform(const Move& m) {
    if (!game_.startsBattle(m)) {
        game_.play(m);
        animate(m);
        afterTurn();
        return;
    }
    game_.begin(m);
    openBattle(true);
}

void App::undo() {
    if (game_.moves().empty()) return;
    // Against the AI, take back the AI's reply too, so it is the player's move again.
    do game_.undo();
    while (vsAi_ && pos().side != human_ && !game_.moves().empty());
    animT_ = 1;
    afterTurn();
}

bool App::aiTurn() const { return vsAi_ && pos().side != human_ && !gameOver() && !menuOpen_; }

// True when clicks on the board should do nothing: the AI or the online opponent is to move.
bool App::inputLocked() const { return aiTurn() || (online_ && (!seated_ || pos().side != human_)); }

// The engine only knows legal chess. In a battle game, attack an exposed king first (with the
// healthiest attacker), and with no legal move left fall back to any pseudo-legal one, captures first.
bool App::battleOverride(Move& out) const {
    if (!game_.battleGame()) return false;
    const int king = pos().kingSquare(pos().side == White ? Black : White);
    const Move* best = nullptr;
    for (const Move& m : game_.legal())
        if (m.to == king && (!best || hpAt(m.from) > hpAt(best->from))) best = &m;
    if (best) {
        out = *best;
        return true;
    }
    if (game_.legal().empty() || !pos().legalMoves().empty()) return false;
    std::vector<Move> captures;
    for (const Move& m : game_.legal())
        if (m.flags & CAPTURE) captures.push_back(m);
    const std::vector<Move>& pool = captures.empty() ? game_.legal() : captures;
    out = pool[GetRandomValue(0, static_cast<int>(pool.size()) - 1)];
    return true;
}

// The search doesn't know about abilities. Use one only when it clearly helps: a bishop healing
// a badly wounded neighbour, a badly wounded king resting, or a strike that lands hard.
bool App::aiAbility(int& out, int& which) const {
    if (!game_.battleGame()) return false;
    which = 0;
    auto badlyHurt = [&](int s) { return hpAt(s) * 2 < battle::maxHp(typeOf(pos().sq[s])); };
    for (int s = 0; s < 64; ++s) {
        if (!game_.canUseAbility(s)) continue;
        const int type = typeOf(pos().sq[s]);
        bool helps = type == KING && badlyHurt(s);
        for (int dr = -1; dr <= 1 && type == BISHOP; ++dr)
            for (int df = -1; df <= 1; ++df) {
                const int r = rankOf(s) + dr, f = fileOf(s) + df, t = r * 8 + f;
                if ((dr || df) && r >= 0 && r < 8 && f >= 0 && f < 8 && ownPiece(t) && badlyHurt(t)) helps = true;
            }
        if (helps) {
            out = s;
            return true;
        }
    }
    // Strikes: try each on a copy of the board state and keep the best, if it is worth a turn.
    constexpr int kWorthIt = 14;  // about one solid hit; each new curse counts as 6
    int best = kWorthIt - 1;
    for (int s = 0; s < 64; ++s) {
        if (!game_.canUseAbility(s, 1)) continue;
        battle::Units trial = game_.units();
        int score = 0;
        for (const battle::AbilityEffect& e : battle::useAbility(pos().sq, trial, s, ply(), 1))
            score += e.damage + 6 * __builtin_popcount(e.curse);
        if (score > best) {
            best = score;
            out = s;
            which = 1;
        }
    }
    return which == 1;
}

void App::updateAi() {
    if (!aiTurn()) return;
    Move forced;
    if (!aiStarted_ && battleOverride(forced)) {
        if (animT_ >= 1) perform(forced);
        return;
    }
    int ability = -1, which = 0;
    if (!aiStarted_ && aiAbility(ability, which)) {
        if (animT_ >= 1) useAbility(ability, which);
        return;
    }
    if (!aiStarted_) {
        std::vector<uint64_t> hashes;
        for (const Position& p : game_.history()) hashes.push_back(hashOf(p));
        searcher_.start(pos(), std::move(hashes), kLevels[level_].limits);
        aiStarted_ = true;
    }
    if (searcher_.running()) searcher_.step(kAiSliceMs);
    // Let the player's move finish animating before the reply starts.
    if (!searcher_.running() && animT_ >= 1) perform(searcher_.best());
}

void App::update(float dt) {
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

    boardFx_.update(dt);  // cosmetic; never gates input
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
        if (game_.claimRepetition()) sendTurn(ply(), "=");  // sendTurn does nothing offline
    }
    if (IsKeyPressed(KEY_A)) {
        autoRotate_ = !autoRotate_;
        if (autoRotate_) faceSideToMove();
    }

    if (animT_ < 1) animT_ = fminf(1.0f, animT_ + dt / kAnimSeconds);
    for (BoardPopup& p : boardPopups_) p.t += dt;
    boardPopups_.erase(std::remove_if(boardPopups_.begin(), boardPopups_.end(), [](const BoardPopup& p) { return p.t > 1.4f; }),
                       boardPopups_.end());
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    endT_ = endScreen() ? endT_ + dt : 0;
    if (endScreen()) {
        if (click && endT_ > kEndFadeSeconds)
            for (const Button& b : endButtons()) {
                if (!CheckCollisionPointRec(cursorPosition(), b.rect)) continue;
                if (b.id == kRematch) {
                    battleMode_ = game_.battleGame();
                    newGame(lastMode_);
                } else {
                    menuOpen_ = true;
                }
            }
        return;
    }
    updateAi();
    if (inputLocked()) return;

    if (panelOpen()) {
        const Vector2 mouse = cursorPosition();
        for (int which = 0; which < 2; ++which) {
            const int key = which == 0 ? KEY_E : KEY_Q;
            const bool over = CheckCollisionPointRec(mouse, abilityButton(which));
            if (game_.canUseAbility(selected_, which) && (IsKeyPressed(key) || (click && over))) {
                commitAbility(selected_, which);
                return;
            }
            if (over) return;  // a disabled button still eats the click
        }
    }

    const Ray ray = GetScreenToWorldRay(cursorPosition(), camera_);
    if (promoTo_ >= 0) {
        promoHover_ = pickPromoSlot(ray);
    } else if (ray.direction.y < -1e-4f) {
        const float t = -ray.position.y / ray.direction.y;
        hovered_ = squareAt(Vector3Add(ray.position, Vector3Scale(ray.direction, t)));
    }

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

void App::draw() {
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
    boardFx_.draw();
    EndMode3D();
    drawLabels();
    drawHpBars();
    if (menuOpen_) {
        drawMenu();
    } else {
        drawHud();
        drawActionPanel();
        drawEndScreen();
    }
    EndDrawing();
}
