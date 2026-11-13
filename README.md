# chess3d

A 3D chess prototype in C++17 with [raylib](https://www.raylib.com/) 5.5. It is a hotseat game with the full rules: castling, en passant, promotion (pick the piece from a 3D picker above the square), check, checkmate and stalemate. The pieces are generated in code as lathe profiles plus boxes, so there are no asset files.

## Build (desktop)

```sh
cmake -B build
cmake --build build -j
./build/chess3d
ctest --test-dir build   # perft move-generator check
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
| U / Backspace | undo |
| R | restart |
| F | flip view |
| A | toggle auto-rotate to the side to move |

## Layout

- `src/chess.{hpp,cpp}`: rules engine (no raylib dependency), with move generation verified by perft
- `src/pieces.{hpp,cpp}`: procedural piece meshes
- `src/shaders.hpp`: lighting shader (GLSL 330 desktop / GLSL 100 web)
- `src/main.cpp`: app loop, camera, picking, rendering, HUD
- `tests/perft.cpp`: node counts against published perft results

## Not yet

Draw by repetition / 50-move rule / insufficient material, an AI opponent, sound, touch orbit controls.
