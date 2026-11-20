// 3D chess prototype: hotseat, vs-AI or online play with full rules, orbit camera, click-to-move,
// and a battle mode where every capture is fought out as a turn-based duel.
#include <raylib.h>

#include "app/app.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {

App app;
void frame() { app.frame(); }

}  // namespace

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Chess 3D");
    SetExitKey(KEY_NULL);  // Esc cancels the promotion picker instead of quitting
    app.init();
#ifdef __EMSCRIPTEN__
    startPointerTracking();
    emscripten_set_main_loop(frame, 0, 1);
#else
    SetTargetFPS(144);
    while (!WindowShouldClose()) frame();
    app.shutdown();
    CloseWindow();
#endif
    return 0;
}
