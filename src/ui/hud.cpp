// App members for 2D screens: the menu and room-code input, the HUD, the battle-game action panel,
// the end screen and the HP bars that other views reuse.
#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "net/net.hpp"

std::vector<Button> App::menuButtons() const {
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

void App::updateCodeInput() {
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

void App::updateMenu() {
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

// Top-right corner, where it hides the fewest squares (next to the piece it covered move targets).
Rectangle App::panelRect() const {
    const float w = 300, h = 194;
    return {fmaxf(12, GetScreenWidth() - w - 12), 12, w, h};
}

// which 0 = support ability (E), 1 = strike (Q), stacked at the bottom of the panel.
Rectangle App::abilityButton(int which) const {
    const Rectangle r = panelRect();
    return {r.x + 10, r.y + r.height - 124 + which * 62, r.width - 20, 52};
}

std::vector<Button> App::endButtons() const {
    const float cx = GetScreenWidth() / 2.0f, y = GetScreenHeight() / 2.0f + 150, w = 160, h = 48, gap = 16;
    // Online games can't restart in place (newGame leaves the room), so they only offer the menu.
    if (online_) return {{{cx - w / 2, y, w, h}, "Menu", kToMenu}};
    return {{{cx - w - gap / 2, y, w, h}, "Rematch", kRematch}, {{cx + gap / 2, y, w, h}, "Menu", kToMenu}};
}

// Counted from the game record, so it stays right after undo.
std::array<App::SideStats, 2> App::gameStats() const {
    std::array<SideStats, 2> st{};
    for (size_t i = 0; i < game_.moves().size(); ++i) {
        const Move& m = game_.moves()[i];
        const Side mover = game_.history()[i].side;
        const Position& after = i + 1 < game_.history().size() ? game_.history()[i + 1] : pos();
        if (game::isAbility(m)) {
            ++st[mover].abilities;
        } else if (game_.battleGame() && (m.flags & CAPTURE)) {
            const bool attackerWon = after.sq[m.to] != EMPTY && sideOf(after.sq[m.to]) == mover;
            ++st[attackerWon ? mover : (mover == White ? Black : White)].battlesWon;
        }
    }
    int left[2] = {0, 0};
    for (int8_t p : pos().sq)
        if (p != EMPTY) ++left[sideOf(p)];
    for (int c = 0; c < 2; ++c) st[c].piecesLost = 16 - left[c];
    return st;
}

void App::drawHpBar(float x, float y, float w, float h, int hp, int max) const {
    const float frac = Clamp(static_cast<float>(hp) / fmaxf(1.0f, static_cast<float>(max)), 0, 1);
    const Color fill = frac > 0.5f ? kHeal : frac > 0.25f ? kSelect : kCheck;
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), Fade(BLACK, 0.7f));
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w * frac), static_cast<int>(h), fill);
}

std::string App::curseTags(uint8_t c) {
    std::string out;
    for (uint8_t bit = 1; bit <= battle::kCurseWeak; bit <<= 1)
        if (c & bit) out += std::string(out.empty() ? "" : " ") + battle::curseName(bit);
    return out;
}

std::string App::buffTags(uint8_t b) {
    std::string out;
    for (uint8_t f : {battle::kShield, battle::kBulwark, battle::kEmpower})
        if (b & f) out += std::string(out.empty() ? "" : " ") + buffName(f);
    return out;
}

void App::drawActionPanel() {
    if (!panelOpen()) return;
    const Rectangle r = panelRect();
    const int x = static_cast<int>(r.x), y = static_cast<int>(r.y);
    const int8_t p = pos().sq[selected_];
    const int type = typeOf(p), max = battle::maxHp(type);
    DrawRectangleRounded(r, 0.12f, 6, kPanel);
    DrawRectangleRoundedLinesEx(r, 0.12f, 6, 2, kPanelEdge);
    DrawText(TextFormat("%s %s", p > 0 ? "White" : "Black", battle::statsOf(type).name), x + 12, y + 10, 22, kSelect);
    drawHpBar(r.x + 12, r.y + 40, r.width - 24, 8, hpAt(selected_), max);
    DrawText(TextFormat("HP %d/%d", hpAt(selected_), max), x + 12, y + 54, 16, RAYWHITE);
    const std::string tags = buffTags(game_.units()[selected_].buffs);
    const char* right = tags.empty() ? "move: click a square" : tags.c_str();
    DrawText(right, x + static_cast<int>(r.width) - 12 - MeasureText(right, 14), y + 56, 14, tags.empty() ? Fade(RAYWHITE, 0.5f) : SKYBLUE);
    const std::string curses = curseTags(game_.units()[selected_].curses);
    if (!curses.empty()) DrawText(curses.c_str(), x + 12, y + 74, 14, kCurse);

    for (int which = 0; which < 2; ++which) {
        const Rectangle btn = abilityButton(which);
        const bool ready = game_.canUseAbility(selected_, which);
        const bool hot = ready && CheckCollisionPointRec(cursorPosition(), btn);
        const Color accent = which == 0 ? kSelect : kCurse;
        DrawRectangleRounded(btn, 0.2f, 6, hot ? mix(kPanel, accent, 0.35f) : mix(kPanel, BLACK, 0.3f));
        DrawRectangleRoundedLinesEx(btn, 0.2f, 6, 2, ready ? accent : Fade(kPanelEdge, 0.35f));
        const battle::Ability& a = battle::abilityOf(type, which);
        const int wait = battle::abilityWait(game_.units()[selected_], ply(), which);
        std::string label = std::string(which == 0 ? "E  " : "Q  ") + a.name;
        if (wait > 0) label += TextFormat("  (ready in %d)", wait);
        else if (!ready) label += which == 0 ? "  (no effect now)" : "  (no target)";
        const Color text = ready ? RAYWHITE : Fade(RAYWHITE, 0.4f);
        DrawText(label.c_str(), static_cast<int>(btn.x + 12), static_cast<int>(btn.y + 7), 20, text);
        DrawText(a.info, static_cast<int>(btn.x + 34), static_cast<int>(btn.y + 31), 16, Fade(text, 0.7f));
    }
}

void App::drawEndScreen() {
    if (!endScreen()) return;
    const int W = GetScreenWidth(), H = GetScreenHeight(), cx = W / 2;
    const float k = fminf(1.0f, endT_ / kEndFadeSeconds);
    DrawRectangle(0, 0, W, H, Fade(BLACK, 0.7f * k));
    auto centered = [&](const char* text, int y, int size, Color c) {
        DrawText(text, cx - MeasureText(text, size) / 2, y, size, Fade(c, k));
    };
    const Side won = static_cast<Side>(game_.winner());
    const bool personal = vsAi_ || (online_ && seated_);
    const bool victory = personal && won == human_;
    const char* title = personal ? (victory ? "VICTORY" : "DEFEAT") : won == White ? "WHITE WINS" : "BLACK WINS";
    const int top = H / 2 - 210;
    int size = 96;  // shrink the title to fit narrow windows
    while (size > 40 && MeasureText(title, size) > W - 32) size -= 8;
    DrawText(title, cx - MeasureText(title, size) / 2 + 4, top + 4, size, Fade(BLACK, 0.6f * k));
    centered(title, top, size, personal && !victory ? kCheck : kSelect);
    const char* reason = game_.kingDead() ? TextFormat("The %s king has fallen in battle", won == White ? "black" : "white")
                                   : TextFormat("Checkmate - %s wins", won == White ? "White" : "Black");
    centered(reason, top + size + 10, 24, RAYWHITE);

    // Stats: one column per side.
    const auto st = gameStats();
    const int tableW = std::min(460, W - 32), labelX = cx - tableW / 2;
    const int col1 = labelX + tableW * 45 / 100, col2 = labelX + tableW * 73 / 100;
    const int rowY = top + size + 64;
    auto row = [&](int i, const char* name, int white, int black) {
        const int y = rowY + 30 + i * 28;
        DrawText(name, labelX, y, 18, Fade(RAYWHITE, 0.7f * k));
        DrawText(TextFormat("%d", white), col1, y, 18, Fade(RAYWHITE, k));
        DrawText(TextFormat("%d", black), col2, y, 18, Fade(RAYWHITE, k));
    };
    auto header = [&](Side c, int x) {
        const char* name = c == White ? (personal && human_ == White ? "White (you)" : "White")
                                      : (personal && human_ == Black ? "Black (you)" : "Black");
        DrawText(name, x, rowY, 18, Fade(kSelect, k));
    };
    header(White, col1);
    header(Black, col2);
    int i = 0;
    if (game_.battleGame()) {
        row(i++, "Battles won", st[White].battlesWon, st[Black].battlesWon);
        row(i++, "Abilities used", st[White].abilities, st[Black].abilities);
    }
    row(i++, "Pieces lost", st[White].piecesLost, st[Black].piecesLost);
    centered(TextFormat("%d turns played", static_cast<int>(game_.moves().size())), rowY + 30 + i * 28 + 8, 18, Fade(RAYWHITE, 0.6f));

    const Vector2 mouse = cursorPosition();
    for (const Button& b : endButtons()) {
        const bool hover = endT_ > kEndFadeSeconds && CheckCollisionPointRec(mouse, b.rect);
        DrawRectangleRounded(b.rect, 0.2f, 6, Fade(hover ? mix(kPanel, kSelect, 0.35f) : kPanel, k));
        DrawRectangleRoundedLinesEx(b.rect, 0.2f, 6, 2, Fade(hover ? kSelect : kPanelEdge, k));
        DrawText(b.label, static_cast<int>(b.rect.x + (b.rect.width - MeasureText(b.label, 22)) / 2),
                 static_cast<int>(b.rect.y + 13), 22, Fade(RAYWHITE, k));
    }
    if (!online_) centered("U undo the last move", static_cast<int>(endButtons()[0].rect.y) + 64, 16, Fade(RAYWHITE, 0.45f));
}

void App::drawHud() {
    const char* side = pos().side == White ? "White" : "Black";
    const char* winner = pos().side == White ? "Black" : "White";
    const char* text = nullptr;
    Color color = RAYWHITE;
    if (game_.kingDead()) {
        const char* fallen = pos().kingSquare(White) < 0 ? "White" : "Black";
        text = TextFormat("The %s king has fallen - %s wins", fallen, fallen[0] == 'W' ? "Black" : "White");
        color = kSelect;
    } else switch (game_.status()) {
        case Status::Playing: text = TextFormat("%s to move", side); break;
        case Status::Check: text = TextFormat("%s to move - CHECK", side); color = kCheck; break;
        case Status::Checkmate: text = TextFormat("Checkmate - %s wins", winner); color = kSelect; break;
        case Status::Stalemate: text = "Stalemate - draw"; color = kSelect; break;
        case Status::Repetition:
            text = game_.reps() >= 5 ? "Draw - fivefold repetition" : "Draw - threefold repetition claimed";
            color = kSelect;
            break;
    }
    DrawRectangle(12, 12, MeasureText(text, 28) + 24, 44, Fade(BLACK, 0.55f));
    DrawText(text, 24, 20, 28, color);
    const int moveNo = static_cast<int>(game_.moves().size()) / 2 + 1;
    const char* sub = vsAi_ ? TextFormat("Move %d   vs AI (%s)%s", moveNo, kLevels[level_].name,
                                         aiTurn() ? "   thinking..." : "")
                      : online_ ? TextFormat("Move %d   Room %s   %s   %s   C copy link", moveNo, room_.c_str(),
                                             !seated_ ? "joining..." : human_ == White ? "you play White" : "you play Black",
                                             link_ == Link::Connected ? "connected"
                                             : link_ == Link::Disconnected ? "opponent disconnected"
                                                                           : "waiting for opponent")
                                : TextFormat("Move %d   hotseat", moveNo);
    if (game_.battleGame()) sub = TextFormat("%s   BATTLE MODE", sub);
    DrawText(sub, 24, 62, 18, Fade(RAYWHITE, 0.6f));
    if (online_ && !netError_.empty())
        DrawText(TextFormat("Network error: %s", netError_.c_str()), 24, 86, 18, kCheck);
    if (promoTo_ >= 0)
        DrawText("Choose a promotion piece  (Esc or click elsewhere to cancel)", 12, GetScreenHeight() - 56, 18, kSelect);
    else if (canClaimDraw())
        DrawText("Threefold repetition - press D to claim a draw", 12, GetScreenHeight() - 56, 18, kSelect);

    const char* help = TextFormat("LMB select/move   %sRMB drag orbit   Wheel zoom   %sR new game   F flip   A auto-rotate [%s]",
                                  game_.battleGame() ? "E ability  Q strike   " : "", online_ ? "" : "U undo   ", autoRotate_ ? "on" : "off");
    DrawText(help, 12, GetScreenHeight() - 28, 16, Fade(RAYWHITE, 0.55f));
}

void App::drawMenu() {
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
        if (b.id == kCreateRoom) centered("Online", b.rect.y - 30, 20, Fade(RAYWHITE, 0.7f));
    const float below = buttons.back().rect.y + buttons.back().rect.height;
    if (!netError_.empty() && !online_) centered(netError_.c_str(), below + 16, 18, kCheck);
    if (gameStarted_) centered("Esc to return to the game", below + 44, 18, Fade(RAYWHITE, 0.55f));
    if (typingCode_) drawCodeInput();
}

void App::drawCodeInput() {
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
