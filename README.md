# chess3d

A 3D chess prototype in C++17 with [raylib](https://www.raylib.com/) 5.5. Play hotseat or against a built-in AI (Easy / Medium / Hard), with the full rules: castling, en passant, promotion (pick the piece from a 3D picker above the square), check, checkmate, stalemate and draw by repetition (claimable on the third occurrence, automatic on the fifth). The pieces are generated in code as lathe profiles plus boxes, so there are no asset files.

## Build (desktop)

```sh
cmake -B build
cmake --build build -j
./build/chess3d
ctest --test-dir build   # perft, rules and AI tests
```

CMake downloads raylib on the first configure, so no system package is needed.

## Build (web / WASM)

You need [emsdk](https://emscripten.org/docs/getting_started/downloads.html) activated:

```sh
emcmake cmake -B build-web -DPLATFORM=Web
cmake --build build-web -j
python3 -m http.server -d build-web   # open http://localhost:8000
```

`.github/workflows/pages.yml` builds the same thing on every push to `main` and deploys it to GitHub Pages. In the repo settings, set Pages → Source to "GitHub Actions".

## Controls

| Input | Action |
|---|---|
| Left click | select a piece / move it |
| Right drag | orbit camera |
| Wheel | zoom |
| Esc | cancel the promotion picker |
| D | claim a draw on threefold repetition |
| U / Backspace | undo (against the AI: your last move and its reply) |
| R | new game menu (Esc returns to the game) |
| F | flip view |
| A | toggle auto-rotate to the side to move |

## Layout

- `src/chess.{hpp,cpp}`: rules engine (no raylib dependency), with move generation verified by perft
- `src/ai.{hpp,cpp}`: AI opponent: alpha-beta with quiescence, piece-square tables, a time budget per level, and an explicit stack so it searches a few ms per frame instead of freezing the page
- `src/pieces.{hpp,cpp}`: procedural piece meshes
- `src/shaders.hpp`: lighting shader (GLSL 330 desktop / GLSL 100 web)
- `src/main.cpp`: app loop, camera, picking, rendering, HUD
- `tests/perft.cpp`: node counts against published perft results
- `tests/rules.cpp`: which positions count as repeated
- `tests/ai.cpp`: evaluation symmetry, mates in one, time budget

## Not yet

50-move rule / insufficient material, sound, touch orbit controls.
