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
constexpr float kFastForward = 4.0f;
constexpr float kUltimateSeconds = 2.2f;  // what a run of Ultimate events is stretched towards
constexpr float kUltimateSlowMo = 0.35f;

const Color kUltimate{250, 190, 60, 255};

}  // namespace

// Opens the arena for game_.pending(). Skipping the intro is for a battle restored mid-fight.
void App::openBattle(bool intro) {
    battle_ = std::make_unique<BattleScene>();
    arenaFx_.clear();
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
    arenaFx_.clear();
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

// The intro, the queued events, or the outro before it can be dismissed: what fast-forward speeds up.
bool App::animatingBattle() const {
    const BattleScene& b = *battle_;
    return b.introT < kBattleIntroSeconds || b.next < b.queue.size() || (fight().over() && b.outroT <= 0.8f);
}

void App::enqueue(std::vector<battle::Event> events) {
    BattleScene& b = *battle_;
    b.queue = std::move(events);
    b.next = 0;
    b.eventT = 0;
    b.applied = false;
    b.started = false;
    b.dur.clear();
    b.runOf.assign(b.queue.size(), -1);
    b.runs.clear();
    for (const battle::Event& e : b.queue) b.dur.push_back(eventSeconds(e));
    // Stretch (or squeeze) each Ultimate's run of events towards ~2 s, so single hits get room and a
    // four-hit run doesn't drag on.
    for (size_t i = 0; i < b.queue.size();) {
        if (b.queue[i].move != 4) {
            ++i;
            continue;
        }
        size_t j = i;
        float sum = 0;
        for (; j < b.queue.size() && b.queue[j].move == 4; ++j) sum += b.dur[j];
        const float k = std::clamp(kUltimateSeconds / sum, 0.6f, 1.6f);
        UltRun run{static_cast<int>(i), static_cast<int>(j) - 1, -1, 0};
        for (size_t n = i; n < j; ++n) {
            b.dur[n] *= k;
            run.total += b.dur[n];
            b.runOf[n] = static_cast<int>(b.runs.size());
            if (b.queue[n].kind == battle::Event::Hit && b.queue[n].actor == b.queue[i].actor) run.lastHit = static_cast<int>(n);
        }
        b.runs.push_back(run);
        i = j;
    }
}

void App::updateBattle(float dt) {
    BattleScene& b = *battle_;
    // Space or a click skips ahead through what is playing; it only scales time, never what happens.
    const bool animating = animatingBattle();
    if (!animating) b.speed = 1;
    else if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) b.speed = kFastForward;
    float scale = b.speed;
    if (b.next < b.queue.size() && b.runOf[b.next] >= 0 && static_cast<int>(b.next) == b.runs[b.runOf[b.next]].lastHit)
        scale *= kUltimateSlowMo;
    dt *= scale;

    arenaFx_.update(dt);
    b.shake = fmaxf(0.0f, b.shake - dt);
    b.flash = fmaxf(0.0f, b.flash - dt * 1.8f);
    const bool cinematic = b.next < b.queue.size() && b.runOf[b.next] >= 0;
    if (cinematic) b.camActor = b.queue[b.runs[b.runOf[b.next]].first].actor;
    b.camBlend += ((cinematic ? 1.0f : 0.0f) - b.camBlend) * fminf(1.0f, dt * 4.0f);
    if (b.introT >= kBattleIntroSeconds) ambientFx(dt);
    for (Popup& p : b.popups) p.t += dt;
    b.popups.erase(std::remove_if(b.popups.begin(), b.popups.end(), [](const Popup& p) { return p.t > 1.2f; }),
                   b.popups.end());
    if (b.introT < kBattleIntroSeconds) {
        b.introT += dt;
        return;
    }
    if (b.next < b.queue.size()) {
        const battle::Event& e = b.queue[b.next];
        if (!b.started) {
            b.started = true;
            startEvent();
        }
        b.eventT += dt;
        if (!b.applied && b.eventT >= b.dur[b.next] * 0.45f) {
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
        if (b.eventT >= b.dur[b.next]) {
            ++b.next;
            b.eventT = 0;
            b.applied = false;
            b.started = false;
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

// Returns where the fighter rests, for the overhead HP bar (the pose's lunges and jumps are not followed).
Vector3 App::drawFighter(int i) {
    const BattleScene& b = *battle_;
    const battle::Fighter& f = fight().fighter(i);
    const float dir = i == 0 ? 1.0f : -1.0f;
    Vector3 at{fighterX(i), 0, 0};
    Color c = f.white ? kWhitePiece : kBlackPiece;
    const float scale = kFighterScale;
    const float intro = fminf(1.0f, b.introT / (kBattleIntroSeconds * 0.7f));
    at.x -= dir * (1 - intro * intro * (3 - 2 * intro)) * 5;
    const Vector3 rest = at;
    Pose p;
    float t = 0;
    if (b.next < b.queue.size()) {
        t = b.eventT / b.dur[b.next];
        p = eventPose(i, b.next, t);
    }
    float alpha = p.alpha;
    if (fight().over() && b.next >= b.queue.size() && fight().winner() != i) {
        // Fade out, then stop drawing.
        const float k = fminf(1.0f, b.outroT / 0.8f);
        if (k >= 1) return rest;
        alpha *= 1 - k;
    }
    if (b.introT >= kBattleIntroSeconds && b.next >= b.queue.size() && !fight().over() && fight().turn() == i) {
        const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(GetTime()) * 6);
        drawMesh(pieces_.disc(), box({0.75f, 0.02f, 0.75f}, {at.x, 0.01f, at.z}), mix(kFrame, kSelect, pulse));
    }
    if (f.guard > 0 && b.next >= b.queue.size())
        drawMesh(pieces_.disc(), box({0.85f, 0.015f, 0.85f}, {at.x, 0.005f, at.z}), f.guard == 2 ? kSelect : SKYBLUE);
    // Board pieces face -z for White; turn them side-on, facing each other.
    const float yaw = i == 0 ? -PI / 2 : PI / 2;
    for (int g = p.ghosts; g >= 1; --g) {
        const Pose old = eventPose(i, b.next, fmaxf(0.0f, t - 0.045f * g));
        Color gc = mix(c, WHITE, 0.5f);
        gc.a = static_cast<unsigned char>(110.0f / g * old.alpha);
        if (gc.a > 4) drawPiece(f.type, Vector3Add(at, old.off), yaw + old.yaw, gc, scale * old.scale, old.tilt);
    }
    c = mix(mix(c, p.tint, p.tintAmt), WHITE, p.flash);
    c.a = static_cast<unsigned char>(255 * fminf(1.0f, fmaxf(0.0f, alpha)));
    if (c.a > 4) drawPiece(f.type, Vector3Add(at, p.off), yaw + p.yaw, c, scale * p.scale, p.tilt);
    return rest;
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
    if (b.camBlend > 0.001f) {
        // Ultimates push in on the user, easing back afterwards.
        const float k = b.camBlend * b.camBlend * (3 - 2 * b.camBlend), ax = fighterX(b.camActor);
        cam.position = Vector3Lerp(cam.position, {ax * 0.75f, 1.9f, 5.0f * back}, k);
        cam.target = Vector3Lerp(cam.target, {ax * 0.4f, 1.0f, 0}, k);
    }
    if (b.shake > 0) {
        const float time = static_cast<float>(GetTime()), amp = fminf(b.shake, 0.6f) * 0.5f;
        cam.position.x += sinf(time * 93) * amp;
        cam.position.y += sinf(time * 71 + 1) * amp;
        cam.target.x += sinf(time * 83 + 2) * amp * 0.5f;
    }
    cam.up = {0, 1, 0};
    cam.fovy = 40;
    cam.projection = CAMERA_PERSPECTIVE;
    SetShaderValue(shader_, viewPosLoc_, &cam.position, SHADER_UNIFORM_VEC3);
    BeginMode3D(cam);
    drawMesh(pieces_.disc(), box({4.4f, 0.3f, 4.4f}, {0, -0.3f, 0}), kFrame);
    drawMesh(pieces_.disc(), box({4.1f, 0.3f, 4.1f}, {0, -0.29f, 0}), {86, 70, 110, 255});
    Vector3 anchor[2];
    for (int i = 0; i < 2; ++i) anchor[i] = drawFighter(i);
    arenaFx_.draw();
    EndMode3D();
    if (b.flash > 0) DrawRectangle(0, 0, W, H, Fade(WHITE, fminf(1.0f, b.flash)));

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
    if (animatingBattle()) {
        const char* hint = b.speed > 1 ? "Space: skip (x4)" : "Space: skip";
        DrawText(hint, W - 32 - MeasureText(hint, 16), 40, 16, Fade(RAYWHITE, 0.6f));
    }

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

    // Ultimate title card: the screen dims and the name appears for the first half second of the run.
    if (b.next < b.queue.size() && b.runOf[b.next] >= 0) {
        const float el = runElapsed(b.next, b.eventT / b.dur[b.next]);
        const float env = fminf(fminf(1.0f, el / 0.12f), fmaxf(0.0f, (0.65f - el) / 0.15f));
        if (env > 0) {
            const battle::Fighter& user = fight().fighter(b.camActor);
            const char* name = battle::statsOf(user.type).ultimate;
            const int size = 72;
            DrawRectangle(0, 0, W, H, Fade(BLACK, 0.55f * env));
            DrawText(name, (W - MeasureText(name, size)) / 2 + 4, H * 2 / 5 + 4, size, Fade(BLACK, env));
            DrawText(name, (W - MeasureText(name, size)) / 2, H * 2 / 5, size, Fade(kUltimate, env));
            const std::string who = fighterName(b.camActor);
            DrawText(who.c_str(), (W - MeasureText(who.c_str(), 24)) / 2, H * 2 / 5 + size + 6, 24, Fade(RAYWHITE, env));
        }
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
