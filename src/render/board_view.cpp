// App members for the 3D board: picking helpers, the board, pieces, promotion picker, labels and the
// HP bars drawn over wounded pieces.
#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <string>

#include "app/app.hpp"

namespace {

const Color kLightSquare{222, 202, 168, 255};
const Color kDarkSquare{118, 84, 60, 255};
const Color kTable{34, 34, 42, 255};
const Color kLastMove{200, 210, 90, 255};
const Color kTarget{70, 160, 90, 255};

}  // namespace

Vector3 squareCenter(int s) { return {fileOf(s) - 3.5f, 0.0f, 3.5f - rankOf(s)}; }

int squareAt(Vector3 p) {
    const int f = static_cast<int>(floorf(p.x + 4.0f));
    const int r = static_cast<int>(floorf(4.0f - p.z));
    return (f < 0 || f > 7 || r < 0 || r > 7) ? -1 : r * 8 + f;
}

// Picker slots float in a row above the promotion square, laid out along the camera's right axis.
Vector3 App::promoSlot(int i) const {
    const Vector3 right{cosf(yaw_), 0, -sinf(yaw_)};
    Vector3 at = squareCenter(promoTo_);
    at.y = 1.3f;
    return Vector3Add(at, Vector3Scale(right, (i - 1.5f) * 0.95f));
}

int App::pickPromoSlot(const Ray& ray) const {
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

// Small HP bars over wounded pieces on the board, buff tags, and ability popups.
void App::drawHpBars() {
    if (!game_.battleGame()) return;
    for (int s = 0; s < 64; ++s) {
        const battle::Unit& u = game_.units()[s];
        if (pos().sq[s] == EMPTY || (u.dmg <= 0 && !u.buffs && !u.curses)) continue;
        Vector3 at = squareCenter(s);
        at.y = 1.5f;
        const Vector2 p = GetWorldToScreen(at, camera_);
        if (u.dmg > 0) drawHpBar(p.x - 20, p.y, 40, 6, hpAt(s), battle::maxHp(typeOf(pos().sq[s])));
        const std::string tags = buffTags(u.buffs);
        if (!tags.empty())
            DrawText(tags.c_str(), static_cast<int>(p.x) - MeasureText(tags.c_str(), 12) / 2, static_cast<int>(p.y) - 15, 12, SKYBLUE);
        const std::string curses = curseTags(u.curses);
        if (!curses.empty())
            DrawText(curses.c_str(), static_cast<int>(p.x) - MeasureText(curses.c_str(), 12) / 2,
                     static_cast<int>(p.y) - (tags.empty() ? 15 : 29), 12, kCurse);
    }
    for (const BoardPopup& pop : boardPopups_) {
        Vector3 at = squareCenter(pop.square);
        at.y = 1.2f + pop.t * 0.5f;
        const Vector2 p = GetWorldToScreen(at, camera_);
        const float alpha = fminf(1.0f, (1.4f - pop.t) / 0.4f);
        const int w = MeasureText(pop.text.c_str(), 22);
        DrawText(pop.text.c_str(), static_cast<int>(p.x) - w / 2 + 2, static_cast<int>(p.y) + 2, 22, Fade(BLACK, 0.6f * alpha));
        DrawText(pop.text.c_str(), static_cast<int>(p.x) - w / 2, static_cast<int>(p.y), 22, Fade(pop.color, alpha));
    }
}

void App::drawMesh(Mesh mesh, Matrix transform, Color c) {
    material_.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(mesh, material_, transform);
}

Matrix App::box(Vector3 size, Vector3 at) {
    return MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(at.x, at.y, at.z));
}

Color App::squareColor(int s) const {
    Color c = (fileOf(s) + rankOf(s)) % 2 == 0 ? kDarkSquare : kLightSquare;
    if (!game_.moves().empty() && (s == game_.moves().back().from || s == game_.moves().back().to)) c = mix(c, kLastMove, 0.45f);
    if (s == selected_ || s == promoTo_) c = mix(c, kSelect, 0.6f);
    if ((game_.status() == Status::Check || game_.status() == Status::Checkmate) && s == pos().kingSquare(pos().side))
        c = mix(c, kCheck, 0.65f);
    if (s == hovered_ && (ownPiece(s) || isTarget(s))) c = mix(c, WHITE, 0.25f);
    return c;
}

void App::drawBoard() {
    drawMesh(pieces_.cube(), box({40, 0.1f, 40}, {0, -0.45f, 0}), kTable);
    drawMesh(pieces_.cube(), box({9, 0.3f, 9}, {0, -0.16f, 0}), kFrame);
    for (int s = 0; s < 64; ++s) {
        Vector3 c = squareCenter(s);
        drawMesh(pieces_.cube(), box({1, 0.2f, 1}, {c.x, -0.1f, c.z}), squareColor(s));
    }
    if (selected_ < 0 || promoTo_ >= 0) return;
    for (const Move& m : game_.legal()) {
        // A promotion appears four times (one per piece); mark its square once.
        if (m.from != selected_ || (m.promo != EMPTY && m.promo != QUEEN)) continue;
        Vector3 c = squareCenter(m.to);
        const bool capture = m.flags & CAPTURE;
        const float r = capture ? 0.46f : 0.14f;
        drawMesh(pieces_.disc(), box({r, 0.02f, r}, {c.x, 0.0f, c.z}), capture ? kCheck : kTarget);
    }
}

void App::drawPieces() {
    for (int s = 0; s < 64; ++s) {
        const int8_t p = pos().sq[s];
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

void App::drawPiece(int type, Vector3 at, float rotY, Color c, float scale, float tilt) {
    Matrix rot = MatrixRotateY(rotY);
    if (tilt != 0) rot = MatrixMultiply(rot, MatrixRotateZ(tilt));  // about the world z axis, so fighters lean along x
    const Matrix world = MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), rot), MatrixTranslate(at.x, at.y, at.z));
    for (const PiecePart& part : pieces_.parts(type)) drawMesh(part.mesh, MatrixMultiply(part.local, world), c);
}

void App::drawPromotionPicker() {
    if (promoTo_ < 0) return;
    const Color base = pos().side == White ? kWhitePiece : kBlackPiece;
    for (int i = 0; i < 4; ++i) {
        const bool hot = i == promoHover_;
        Vector3 at = promoSlot(i);
        drawMesh(pieces_.disc(), box({0.4f, 0.04f, 0.4f}, {at.x, at.y - 0.04f, at.z}), hot ? kSelect : kFrame);
        if (hot) at.y += 0.12f;
        // yaw + 90 degrees turns the knight side-on to the camera.
        drawPiece(kPromoChoices[i], at, yaw_ + PI / 2, hot ? mix(base, kSelect, 0.3f) : base);
    }
}

void App::drawLabels() {
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
