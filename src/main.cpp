// 3D chess prototype: hotseat play with full rules, orbit camera, click-to-move.
#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <vector>

#include "chess.hpp"
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
    int selected_ = -1, hovered_ = -1;
    int animFrom_ = -1, animTo_ = -1;
    float animT_ = 1;

    Camera3D camera_{};
    float yaw_ = 0, targetYaw_ = 0, pitch_ = 0.85f, dist_ = 11.5f;
    bool autoRotate_ = true;

    Shader shader_{};
    int viewPosLoc_ = -1;
    Material material_{};
    PieceSet pieces_;

    void reset() {
        pos_ = Position::start();
        history_.clear();
        moves_.clear();
        selected_ = -1;
        animT_ = 1;
        refresh();
    }

    void refresh() {
        legal_ = pos_.legalMoves();
        const bool check = pos_.inCheck(pos_.side);
        status_ = legal_.empty() ? (check ? Status::Checkmate : Status::Stalemate)
                                 : (check ? Status::Check : Status::Playing);
        if (autoRotate_) faceSideToMove();
    }

    void faceSideToMove() {
        const float want = pos_.side == White ? 0.0f : PI;
        targetYaw_ = yaw_ + wrapAngle(want - yaw_);
    }

    void play(const Move& m) {
        history_.push_back(pos_);
        moves_.push_back(m);
        pos_ = pos_.apply(m);
        animFrom_ = m.from;
        animTo_ = m.to;
        animT_ = 0;
        selected_ = -1;
        refresh();
    }

    void undo() {
        if (history_.empty()) return;
        pos_ = history_.back();
        history_.pop_back();
        moves_.pop_back();
        selected_ = -1;
        animT_ = 1;
        refresh();
    }

    bool ownPiece(int s) const { return s >= 0 && pos_.sq[s] != EMPTY && sideOf(pos_.sq[s]) == pos_.side; }

    // The move from the selected square to `to`, auto-promoting to a queen.
    const Move* findMove(int to) const {
        for (const Move& m : legal_)
            if (m.from == selected_ && m.to == to && (m.promo == EMPTY || m.promo == QUEEN)) return &m;
        return nullptr;
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

        if (IsKeyPressed(KEY_R)) reset();
        if (IsKeyPressed(KEY_U) || IsKeyPressed(KEY_BACKSPACE)) undo();
        if (IsKeyPressed(KEY_F)) targetYaw_ += PI;
        if (IsKeyPressed(KEY_A)) {
            autoRotate_ = !autoRotate_;
            if (autoRotate_) faceSideToMove();
        }

        hovered_ = -1;
        const Ray ray = GetScreenToWorldRay(cursorPosition(), camera_);
        if (ray.direction.y < -1e-4f) {
            const float t = -ray.position.y / ray.direction.y;
            hovered_ = squareAt(Vector3Add(ray.position, Vector3Scale(ray.direction, t)));
        }

        const bool gameOver = status_ == Status::Checkmate || status_ == Status::Stalemate;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !gameOver) {
            if (hovered_ < 0) {
                selected_ = -1;
            } else if (const Move* m = selected_ >= 0 ? findMove(hovered_) : nullptr) {
                play(*m);
            } else if (ownPiece(hovered_) && hovered_ != selected_) {
                selected_ = hovered_;
            } else {
                selected_ = -1;
            }
        }

        if (animT_ < 1) animT_ = fminf(1.0f, animT_ + dt / kAnimSeconds);
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
        if (s == selected_) c = mix(c, kSelect, 0.6f);
        if ((status_ == Status::Check || status_ == Status::Checkmate) && s == pos_.kingSquare(pos_.side))
            c = mix(c, kCheck, 0.65f);
        const bool interactive = ownPiece(s) || (selected_ >= 0 && findMove(s));
        if (s == hovered_ && interactive) c = mix(c, WHITE, 0.25f);
        return c;
    }

    void drawBoard() {
        drawMesh(pieces_.cube(), box({40, 0.1f, 40}, {0, -0.45f, 0}), kTable);
        drawMesh(pieces_.cube(), box({9, 0.3f, 9}, {0, -0.16f, 0}), kFrame);
        for (int s = 0; s < 64; ++s) {
            Vector3 c = squareCenter(s);
            drawMesh(pieces_.cube(), box({1, 0.2f, 1}, {c.x, -0.1f, c.z}), squareColor(s));
        }
        if (selected_ < 0) return;
        for (const Move& m : legal_) {
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
            const Matrix world = MatrixMultiply(MatrixRotateY(p > 0 ? 0.0f : PI), MatrixTranslate(at.x, at.y, at.z));
            const Color c = p > 0 ? kWhitePiece : kBlackPiece;
            for (const PiecePart& part : pieces_.parts(typeOf(p)))
                drawMesh(part.mesh, MatrixMultiply(part.local, world), c);
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
        switch (status_) {
            case Status::Playing: text = TextFormat("%s to move", side); break;
            case Status::Check: text = TextFormat("%s to move - CHECK", side); color = kCheck; break;
            case Status::Checkmate: text = TextFormat("Checkmate - %s wins", winner); color = kSelect; break;
            case Status::Stalemate: text = "Stalemate - draw"; color = kSelect; break;
        }
        DrawRectangle(12, 12, MeasureText(text, 28) + 24, 44, Fade(BLACK, 0.55f));
        DrawText(text, 24, 20, 28, color);
        DrawText(TextFormat("Move %d", static_cast<int>(moves_.size()) / 2 + 1), 24, 62, 18, Fade(RAYWHITE, 0.6f));

        const char* help = TextFormat("LMB select/move   RMB drag orbit   Wheel zoom   U undo   R restart   F flip   A auto-rotate [%s]",
                                      autoRotate_ ? "on" : "off");
        DrawText(help, 12, GetScreenHeight() - 28, 16, Fade(RAYWHITE, 0.55f));
    }

    void draw() {
        BeginDrawing();
        ClearBackground({22, 22, 28, 255});
        SetShaderValue(shader_, viewPosLoc_, &camera_.position, SHADER_UNIFORM_VEC3);
        BeginMode3D(camera_);
        drawBoard();
        drawPieces();
        EndMode3D();
        drawLabels();
        drawHud();
        EndDrawing();
    }
};

App app;
void frame() { app.frame(); }

}  // namespace

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Chess 3D");
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
