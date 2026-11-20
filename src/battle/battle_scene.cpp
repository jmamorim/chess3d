// App members for the battle arena: opening and finishing a battle, its animation and input, and
// drawing the fighters, command menu and banners.
#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app/app.hpp"
#include "net/net.hpp"

namespace {

constexpr float kBattleIntroSeconds = 0.9f;
constexpr float kBattleEventSeconds = 0.8f;
constexpr float kBattleAiDelay = 0.5f;

const Color kUltimate{250, 190, 60, 255};

}  // namespace

// Opens the arena for game_.pending(). Skipping the intro is for a battle restored mid-fight.
void App::openBattle(bool intro) {
    battle_ = std::make_unique<BattleScene>();
    battle_->shownHp[0] = fight().fighter(0).hp;
    battle_->shownHp[1] = fight().fighter(1).hp;
    if (!intro) battle_->introT = kBattleIntroSeconds;
    clearSelection();
    searcher_.cancel();
}

// The outro was confirmed (or the opponent has moved on): apply the result to the board.
void App::finishBattle() {
    const Move m = game_.pendingMove();
    const bool attackerWon = fight().winner() == 0;
    game_.finishBattle();
    battle_.reset();
    if (attackerWon) animate(m);
    else animT_ = 1;
    afterTurn();
}

// ---- Battle mode ----

std::string App::fighterName(int i) const {
    const battle::Fighter& f = fight().fighter(i);
    return std::string(f.white ? "White " : "Black ") + battle::statsOf(f.type).name;
}

// Burn, Bleed, Stunned, Weakened and Exposed are bad for whoever carries them.
bool App::debuffTag(const char* tag) {
    using S = battle::Status;
    for (S s : {S::Burn, S::Bleed, S::Stun, S::AtkDown, S::DefDown})
        if (std::strcmp(tag, battle::statusName(s)) == 0) return true;
    return false;
}

float App::eventSeconds(const battle::Event& e) {
    return e.kind == battle::Event::Announce || e.kind == battle::Event::Status ? kBattleEventSeconds * 0.65f : kBattleEventSeconds;
}

// Bottom command menu: a 2x2 grid in battle::Command order, with the Ultimate (4) as a bar above it.
Rectangle App::commandRect(int i) const {
    const float total = fminf(GetScreenWidth() - 32.0f, 920), gap = 8, h = 60, ultH = 40;
    const float w = (total - gap) / 2;
    const float x0 = (GetScreenWidth() - total) / 2, y0 = GetScreenHeight() - 16 - 2 * h - gap;
    if (i == 4) return {x0, y0 - gap - ultH, total, ultH};
    return {x0 + (i % 2) * (w + gap), y0 + (i / 2) * (h + gap), w, h};
}

std::string App::commandLabel(int i, int type) {
    const battle::Stats& st = battle::statsOf(type);
    switch (i) {
        case 0: return "Attack";
        case 1: return st.skill;
        case 2: return st.skill2;
        case 4: return st.ultimate;
        default: return "Guard";
    }
}

const char* App::commandInfo(int i, int type) {
    const battle::Stats& st = battle::statsOf(type);
    switch (i) {
        case 0: return "1.0x power";
        case 1: return st.skillInfo;
        case 2: return st.skill2Info;
        case 4: return st.ultimateInfo;
        default: return "half damage until your next turn, heal 5%";
    }
}

bool App::awaitingHuman() const {
    const BattleScene& b = *battle_;
    return b.introT >= kBattleIntroSeconds && b.next >= b.queue.size() && !fight().over() &&
           !aiFighter(fight().turn()) && !remoteFighter(fight().turn());
}

void App::enqueue(std::vector<battle::Event> events) {
    BattleScene& b = *battle_;
    b.queue = std::move(events);
    b.next = 0;
    b.eventT = 0;
    b.applied = false;
}

void App::updateBattle(float dt) {
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
            if (e.kind == K::Tick) b.popups.push_back({e.target, TextFormat("%d", e.amount), kCurse});
            if ((e.kind == K::Heal || e.kind == K::Guard) && e.amount > 0) b.popups.push_back({e.target, TextFormat("+%d", e.amount), kHeal});
            // A tag follows a heal number a moment later so the two don't overlap.
            if (e.tag) b.popups.push_back({e.target, e.tag, debuffTag(e.tag) ? kCurse : SKYBLUE, e.amount > 0 ? -0.35f : 0.0f});
        }
        if (b.eventT >= eventSeconds(e)) {
            ++b.next;
            b.eventT = 0;
            b.applied = false;
        }
        return;
    }
    const bool confirm = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
    if (fight().over()) {
        b.outroT += dt;
        if (b.outroT > 0.8f && confirm) finishBattle();
        return;
    }
    if (aiFighter(fight().turn())) {
        b.aiT += dt;
        if (b.aiT >= kBattleAiDelay) {
            b.aiT = 0;
            enqueue(game_.command(game_.aiCommand()));
        }
        return;
    }
    if (remoteFighter(fight().turn())) {
        // Wait for the opponent's choice; it was checked on arrival, so it applies as is.
        if (b.remote.empty()) return;
        const int c = b.remote.front();
        b.remote.pop_front();
        enqueue(game_.command(static_cast<battle::Command>(c)));
        saveRoom();
        return;
    }
    int chosen = -1;
    // Cursor: 0 1 / 2 3 grid, with the Ultimate bar (4) above the top row.
    if (b.cursor < 4 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) b.cursor ^= 1;
    if (IsKeyPressed(KEY_UP)) b.cursor = b.cursor >= 2 && b.cursor < 4 ? b.cursor - 2 : 4;
    if (IsKeyPressed(KEY_DOWN)) b.cursor = b.cursor == 4 ? 0 : b.cursor < 2 ? b.cursor + 2 : b.cursor;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) chosen = b.cursor;
    if (IsKeyPressed(KEY_U)) chosen = 4;
    for (int i = 0; i < 5; ++i) {
        if (IsKeyPressed(KEY_ONE + i)) chosen = i;
        if (CheckCollisionPointRec(cursorPosition(), commandRect(i))) {
            b.cursor = i;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) chosen = i;
        }
    }
    const auto command = static_cast<battle::Command>(chosen);
    if (chosen < 0 || !fight().canUse(command)) return;
    const int k = game_.pendingCommands();
    enqueue(game_.command(command));
    if (!online_) return;
    net::send("cmd " + std::to_string(ply()) + " " + std::to_string(k) + " " + std::to_string(chosen));
    saveRoom();
}

float App::fighterX(int i) { return i == 0 ? -1.9f : 1.9f; }

// Height of a piece's model at scale 1, for placing labels over it.
float App::pieceTop(int type) {
    float& top = pieceTops_[type];
    if (top <= 0)
        for (const PiecePart& part : pieces_.parts(type)) {
            const BoundingBox bb = GetMeshBoundingBox(part.mesh);
            top = fmaxf(top, fmaxf(Vector3Transform(bb.min, part.local).y, Vector3Transform(bb.max, part.local).y));
        }
    return top;
}

// Returns where the fighter was drawn, for the overhead HP bar.
Vector3 App::drawFighter(int i) {
    const BattleScene& b = *battle_;
    const battle::Fighter& f = fight().fighter(i);
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
        if (e.target == i && e.kind == K::Tick) c = mix(c, kCurse, 0.6f * sinf(PI * t));
        if (e.actor == i && (e.kind == K::Heal || e.kind == K::Guard))
            c = mix(c, e.kind == K::Heal ? kHeal : SKYBLUE, 0.6f * sinf(PI * t));
    }
    if (fight().over() && b.next >= b.queue.size() && fight().winner() != i) {
        // Fade out, then stop drawing.
        const float k = fminf(1.0f, b.outroT / 0.8f);
        if (k >= 1) return at;
        c.a = static_cast<unsigned char>(255 * (1 - k));
    }
    if (b.introT >= kBattleIntroSeconds && b.next >= b.queue.size() && !fight().over() && fight().turn() == i) {
        const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(GetTime()) * 6);
        drawMesh(pieces_.disc(), box({0.75f, 0.02f, 0.75f}, {at.x, 0.01f, at.z}), mix(kFrame, kSelect, pulse));
    }
    if (f.guard > 0 && b.next >= b.queue.size())
        drawMesh(pieces_.disc(), box({0.85f, 0.015f, 0.85f}, {at.x, 0.005f, at.z}), f.guard == 2 ? kSelect : SKYBLUE);
    // Board pieces face -z for White; turn them side-on, facing each other.
    drawPiece(f.type, at, i == 0 ? -PI / 2 : PI / 2, c, scale);
    return at;
}

void App::drawPanel(Rectangle r) const {
    DrawRectangleRounded(r, 0.12f, 6, kPanel);
    DrawRectangleRoundedLinesEx(r, 0.12f, 6, 2, kPanelEdge);
}

void App::drawBattle() {
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
        const battle::Fighter& f = fight().fighter(i);
        if (b.introT < kBattleIntroSeconds * 0.7f) break;  // still sliding in
        if (fight().over() && b.next >= b.queue.size() && fight().winner() != i) continue;  // sinking out
        const Vector2 p = GetWorldToScreen({anchor[i].x, anchor[i].y + pieceTop(f.type) * kFighterScale + 0.2f, anchor[i].z}, cam);
        const int max = battle::maxHp(f.type);
        const bool active = !fight().over() && fight().turn() == i && b.next >= b.queue.size();
        const std::string name = fighterName(i);
        const float w = 180, x = p.x - w / 2;
        DrawText(name.c_str(), static_cast<int>(p.x) - MeasureText(name.c_str(), 20) / 2, static_cast<int>(p.y - 46), 20,
                 active ? kSelect : RAYWHITE);
        drawHpBar(x, p.y - 22, w, 12, b.shownHp[i], max);
        DrawRectangleLines(static_cast<int>(x) - 1, static_cast<int>(p.y) - 23, static_cast<int>(w) + 2, 14, Fade(RAYWHITE, 0.5f));
        // Ultimate charge: a thin gold bar under the HP bar.
        const float charge = static_cast<float>(f.charge) / battle::kUltimateCharge;
        DrawRectangle(static_cast<int>(x), static_cast<int>(p.y - 9), static_cast<int>(w), 3, Fade(BLACK, 0.7f));
        DrawRectangle(static_cast<int>(x), static_cast<int>(p.y - 9), static_cast<int>(w * charge), 3,
                      charge >= 1 ? mix(kUltimate, RAYWHITE, 0.5f + 0.5f * sinf(static_cast<float>(GetTime()) * 8)) : kUltimate);
        const char* hp = TextFormat("%d/%d", b.shownHp[i], max);
        DrawText(hp, static_cast<int>(p.x) - MeasureText(hp, 16) / 2, static_cast<int>(p.y - 4), 16, RAYWHITE);
        // Buffs on one line, debuffs (with turns left) on the line above.
        std::string buffs, debuffs;
        if (f.guard == 2) buffs += "BULWARK  ";
        if (f.guard == 1) buffs += "GUARD  ";
        if (f.evading) buffs += "EVADE  ";
        if (f.empowered) buffs += "EMPOWERED  ";
        for (int k = 0; k < battle::kStatusCount; ++k) {
            if (f.status[k] <= 0) continue;
            const char* name = battle::statusName(static_cast<battle::Status>(k));
            (debuffTag(name) ? debuffs : buffs) += TextFormat("%s %d  ", name, f.status[k]);
        }
        int lineY = static_cast<int>(p.y - 64);
        for (auto [text, color] : {std::pair{&buffs, SKYBLUE}, std::pair{&debuffs, kCurse}}) {
            if (text->empty()) continue;
            text->resize(text->size() - 2);
            DrawText(text->c_str(), static_cast<int>(p.x) - MeasureText(text->c_str(), 14) / 2, lineY, 14, color);
            lineY -= 17;
        }
    }

    for (const Popup& p : b.popups) {
        if (p.t < 0) continue;  // delayed
        // Mid-body, so popups stay below the overhead HP bar.
        const float mid = pieceTop(fight().fighter(p.target).type) * kFighterScale * 0.5f;
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
    else if (fight().over()) msg = fighterName(fight().winner()) + (fight().winner() == 0 ? " wins and takes the square!" : " holds its ground - the capture fails!");
    else if (aiFighter(fight().turn())) msg = fighterName(fight().turn()) + " is deciding...";
    else if (remoteFighter(fight().turn())) msg = fighterName(fight().turn()) + " - opponent is choosing...";
    else msg = fighterName(fight().turn()) + " - choose a command";
    const Rectangle top{16, 16, W - 32.0f, 52};
    drawPanel(top);
    DrawText(msg.c_str(), 32, 32, 22, RAYWHITE);

    // Command menu.
    const bool live = awaitingHuman();
    const battle::Fighter& actor = fight().fighter(fight().turn());
    for (int i = 0; i < 5; ++i) {
        const Rectangle r = commandRect(i);
        const bool usable = fight().canUse(static_cast<battle::Command>(i));
        const bool hot = live && i == b.cursor && usable;
        const Color accent = i == 4 ? kUltimate : kSelect;
        DrawRectangleRounded(r, 0.2f, 6, hot ? mix(kPanel, accent, 0.35f) : kPanel);
        const Color text = !live ? Fade(RAYWHITE, 0.35f) : usable ? RAYWHITE : Fade(RAYWHITE, 0.4f);
        if (i == 4) {
            // The bar fills as the fighter charges; full, it glows.
            const float k = static_cast<float>(actor.charge) / battle::kUltimateCharge;
            DrawRectangleRounded({r.x, r.y, r.width * k, r.height}, 0.2f, 6, Fade(kUltimate, usable ? 0.45f : 0.2f));
            DrawRectangleRoundedLinesEx(r, 0.2f, 6, 2, hot || (live && usable) ? kUltimate : Fade(kPanelEdge, live ? 1.0f : 0.35f));
            std::string label = TextFormat("5  ULTIMATE: %s", commandLabel(i, actor.type).c_str());
            label += usable ? std::string("  -  ") + commandInfo(i, actor.type) : TextFormat("  (charging %d%%)", actor.charge);
            DrawText(label.c_str(), static_cast<int>(r.x + 14), static_cast<int>(r.y + 10), 20, text);
            continue;
        }
        DrawRectangleRoundedLinesEx(r, 0.2f, 6, 2, hot ? kSelect : Fade(kPanelEdge, live ? 1.0f : 0.35f));
        std::string label = TextFormat("%d  %s", i + 1, commandLabel(i, actor.type).c_str());
        if (!usable && (i == 1 || i == 2)) label += TextFormat("  (ready in %d)", actor.cooldown[i - 1]);
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
    } else if (fight().over() && b.next >= b.queue.size()) {
        // From the player's side against the AI or online; in hotseat, name the winning colour.
        const Side won = fight().fighter(fight().winner()).white ? White : Black;
        const char* big = vsAi_ || online_ ? (won == human_ ? "VICTORY" : "DEFEAT") : won == White ? "WHITE WINS" : "BLACK WINS";
        banner(big, b.outroT > 0.8f ? "click or press Enter to continue" : nullptr, 1);
    }
}
