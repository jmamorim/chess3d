// The App class: all application state and the declarations of its members. The members are defined
// by topic: app/app.cpp (game flow, update), net/online.cpp, battle/battle_scene.cpp, ui/hud.cpp and
// render/board_view.cpp.
#pragma once
#include <raylib.h>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "ai/ai.hpp"
#include "battle/battle.hpp"
#include "chess/chess.hpp"
#include "game/game.hpp"
#include "render/fx.hpp"
#include "render/pieces.hpp"

using namespace chess;

inline constexpr int8_t kPromoChoices[4] = {QUEEN, ROOK, BISHOP, KNIGHT};

struct Level {
    const char* name;
    SearchLimits limits;
};
inline constexpr Level kLevels[3] = {{"Easy", {2, 300, 80}}, {"Medium", {4, 600, 45}}, {"Hard", {64, 1500}}};

enum MenuId { kLevelEasy, kLevelMedium, kLevelHard, kHotseat, kPlayWhite, kPlayBlack, kPlayRandom, kCreateRoom, kJoinRoom, kBattleToggle };
inline constexpr int kCodeLength = 4;
struct Button {
    Rectangle rect;
    const char* label;
    int id;
};

// Colors shared by more than one file; the ones only one file draws with live in that file.
inline const Color kFrame{58, 38, 28, 255};
inline const Color kWhitePiece{236, 228, 210, 255};
inline const Color kBlackPiece{48, 44, 52, 255};
inline const Color kSelect{240, 210, 70, 255};
inline const Color kCheck{220, 60, 50, 255};
inline const Color kHeal{90, 210, 120, 255};
inline const Color kCurse{230, 120, 40, 255};  // debuffs: burn, bleed, exposed, weakened
inline const Color kPanel{24, 26, 48, 235};
inline const Color kPanelEdge{200, 200, 230, 255};

inline Color mix(Color a, Color b, float t) {
    auto l = [t](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), 255};
}

Vector3 squareCenter(int s);  // render/board_view.cpp
int squareAt(Vector3 p);      // render/board_view.cpp
Vector2 cursorPosition();     // app/app.cpp
void startPointerTracking();  // app/app.cpp; a no-op on desktop

class App {
public:

    void init();
    void shutdown();
    void frame();

private:
    game::Game game_;  // all rules and battle state; the App only animates and draws it
    int selected_ = -1, hovered_ = -1;
    int promoTo_ = -1, promoHover_ = -1;  // promotion picker: target square (-1 = closed), hovered slot
    int animFrom_ = -1, animTo_ = -1;
    float animT_ = 1;

    // Battle mode: every capture is a battle, check no longer restricts moves, and a game is won by
    // killing the enemy king. Pieces carry damage between battles (see game::Game). Online, the
    // host's toggle decides and the guest learns it from the first sync.
    bool battleMode_ = false;  // menu toggle; applies from the next new game
    int lastMode_ = kHotseat;  // for Rematch
    float endT_ = 0;                          // seconds the end screen has been up
    struct BoardPopup {
        int square;
        std::string text;
        Color color;
        float t = 0;
    };
    std::vector<BoardPopup> boardPopups_;
    struct Popup {
        int target;
        std::string text;
        Color color;
        float t = 0;
    };
    // A run of consecutive events of one Ultimate: it gets the cinematic treatment.
    struct UltRun {
        int first, last, lastHit;  // queue indices; lastHit = -1 when nothing lands
        float total;               // seconds
    };
    // How a fighter is moved and tinted while an event plays (see battle_fx.cpp).
    struct Pose {
        Vector3 off{0, 0, 0};
        float yaw = 0, tilt = 0, scale = 1, alpha = 1, flash = 0, tintAmt = 0;
        Color tint = WHITE;
        int ghosts = 0;  // translucent afterimages trailing the fighter
    };
    // The ability an event belongs to: the acting fighter's type plus its command (0 once a Bulwark counter-strike).
    struct Beat {
        int type, move, hitIdx;  // hitIdx: how many hits of this action came before it
        bool counter;
    };
    // Only the presentation of game_.pending(); a scene exists exactly while a battle is pending.
    struct BattleScene {
        std::vector<battle::Event> queue;
        size_t next = 0;
        float eventT = 0;   // seconds into queue[next]
        bool applied = false;  // queue[next]'s HP change has been shown
        int shownHp[2] = {0, 0};
        float introT = 0, outroT = 0, aiT = 0;
        int cursor = 0;  // highlighted command
        std::vector<float> dur;  // seconds each queued event plays (Ultimates are stretched to ~2 s)
        std::vector<int> runOf;  // index into runs for an Ultimate's events, else -1
        std::vector<UltRun> runs;
        bool started = false;    // queue[next]'s effects have been spawned
        float speed = 1;         // fast-forward multiplier for the queued events
        float shake = 0, flash = 0;  // screen shake (world units) and white flash (alpha), both decay
        float camBlend = 0;      // 0 normal framing .. 1 pushed in on camActor
        int camActor = 0;
        float ambientT[2] = {0, 0};  // status particle timers per fighter
        std::vector<Popup> popups;
        std::deque<int> remote;  // opponent commands received but not yet applied (the queue is still animating)
    };
    std::unique_ptr<BattleScene> battle_;
    std::array<float, 7> pieceTops_{};
    Fx arenaFx_, boardFx_;

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

    // ---- End screen ----

    static constexpr float kEndFadeSeconds = 0.6f;

    enum EndId { kRematch, kToMenu };

    struct SideStats {
        int battlesWon = 0, piecesLost = 0, abilities = 0;
    };

    static constexpr float kFighterScale = 1.7f;

    const Position& pos() const { return game_.pos(); }

    int ply() const { return game_.ply(); }

    int hpAt(int s) const { return game_.hpAt(s); }

    bool gameOver() const { return game_.over(); }

    const battle::Battle& fight() const { return *game_.pending(); }  // only while battle_ is set

    bool canClaimDraw() const { return !gameOver() && !inputLocked() && game_.reps() >= 3; }

    bool ownPiece(int s) const { return s >= 0 && pos().sq[s] != EMPTY && sideOf(pos().sq[s]) == pos().side; }

    const Move* findMove(int to, int8_t promo = EMPTY) const {
        for (const Move& m : game_.legal())
            if (m.from == selected_ && m.to == to && m.promo == promo) return &m;
        return nullptr;
    }

    bool isPromotion(int to) const { return findMove(to, QUEEN) != nullptr; }

    bool isTarget(int s) const { return selected_ >= 0 && (findMove(s) || isPromotion(s)); }

    // ---- Action panel (battle games): the selected piece moves or uses its ability ----

    bool panelOpen() const { return game_.battleGame() && selected_ >= 0 && promoTo_ < 0 && !gameOver() && !inputLocked(); }

    // game_.winner() is the side that won, or -1 while the game goes on or after a draw.
    bool endScreen() const { return game_.winner() >= 0 && !battle_ && animT_ >= 1 && !menuOpen_; }

    bool aiFighter(int i) const { return vsAi_ && (fight().fighter(i).white ? White : Black) != human_; }

    // Online, only the opponent's own player can command their fighter.
    bool remoteFighter(int i) const { return online_ && (fight().fighter(i).white ? White : Black) != human_; }

    // ---- defined in app.cpp ----
    void newGame(int mode);
    void reset(bool battleGame, uint32_t seedBase);
    void clearSelection();
    void afterTurn();
    void animate(const Move& m);
    void faceSideToMove();
    void useAbility(int s, int which = 0);
    static const char* buffName(uint8_t b);
    void perform(const Move& m);
    void undo();
    bool aiTurn() const;
    bool inputLocked() const;
    bool battleOverride(Move& out) const;
    bool aiAbility(int& out, int& which) const;
    void updateAi();
    void update(float dt);
    void draw();

    // ---- defined in online.cpp ----
    void sendTurn(int ply, const std::string& token);
    void commit(const Move& m);
    void commitAbility(int s, int which);
    void saveRoom() const;
    bool adopt(bool battleGame, const std::string& tokens, int from = 0, int side = -1);
    void enterRoom(const std::string& code);
    void createRoom();
    void openRoom(const std::string& code);
    void leaveRoom();
    void sendSync();
    void onSync(std::istringstream& in);
    void onTurn(std::istringstream& in);
    void onCmd(std::istringstream& in);
    void pollNet();

    // ---- defined in battle_scene.cpp ----
    void openBattle(bool intro);
    void finishBattle();
    std::string fighterName(int i) const;
    static bool debuffTag(const char* tag);
    static float eventSeconds(const battle::Event& e);
    bool animatingBattle() const;
    Rectangle commandRect(int i) const;
    static std::string commandLabel(int i, int type);
    static const char* commandInfo(int i, int type);
    bool awaitingHuman() const;
    void enqueue(std::vector<battle::Event> events);
    void updateBattle(float dt);
    static float fighterX(int i);
    float pieceTop(int type);
    Vector3 drawFighter(int i);
    void drawPanel(Rectangle r) const;
    void drawBattle();

    // ---- defined in battle_fx.cpp ----
    Beat beat(size_t idx) const;
    float runElapsed(size_t idx, float t) const;
    Vector3 fighterMid(int i);
    Pose eventPose(int i, size_t idx, float t);
    void startEvent();
    void ambientFx(float dt);
    void shakeArena(float amount);
    void boardAbilityFx(int s, int which, int type, const std::vector<battle::AbilityEffect>& effects);

    // ---- defined in hud.cpp ----
    std::vector<Button> menuButtons() const;
    void updateCodeInput();
    void updateMenu();
    Rectangle panelRect() const;
    Rectangle abilityButton(int which) const;
    std::vector<Button> endButtons() const;
    std::array<SideStats, 2> gameStats() const;
    void drawHpBar(float x, float y, float w, float h, int hp, int max) const;
    static std::string curseTags(uint8_t c);
    static std::string buffTags(uint8_t b);
    void drawActionPanel();
    void drawEndScreen();
    void drawHud();
    void drawMenu();
    void drawCodeInput();

    // ---- defined in board_view.cpp ----
    Vector3 promoSlot(int i) const;
    int pickPromoSlot(const Ray& ray) const;
    void drawHpBars();
    void drawMesh(Mesh mesh, Matrix transform, Color c);
    Matrix box(Vector3 size, Vector3 at);
    Color squareColor(int s) const;
    void drawBoard();
    void drawPieces();
    void drawPiece(int type, Vector3 at, float rotY, Color c, float scale = 1, float tilt = 0);
    void drawPromotionPicker();
    void drawLabels();
};
