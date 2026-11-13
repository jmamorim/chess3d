# chess3d

A 3D chess prototype in C++17 with [raylib](https://www.raylib.com/) 5.5. Play hotseat, against a built-in AI (Easy / Medium / Hard), or online against a friend (web build only), with the full rules: castling, en passant, promotion (pick the piece from a 3D picker above the square), check, checkmate, stalemate and draw by repetition (claimable on the third occurrence, automatic on the fifth). The pieces are generated in code as lathe profiles plus boxes, so there are no asset files.

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

## Online play (web only)

In the menu, **Create room** gives you a random colour and a 4-letter code; the page address becomes `…/chess3d/#CODE`. Send that link (press C to copy it) or the code; your friend opens the link or picks **Join room** and types the code. The browsers connect directly over WebRTC through [PeerJS](https://peerjs.com/), whose free public server only introduces them. There is no game server.

Each browser keeps the room's moves in `localStorage`, so reopening the link after a reload or a dropped connection puts you back in your seat with the board as it was. There is no clock and no undo online. Players behind strict NATs may fail to connect, because no TURN relay is configured.

## Controls

| Input | Action |
|---|---|
| Left click | select a piece / move it |
| Right drag | orbit camera |
| Wheel | zoom |
| Esc | cancel the promotion picker |
| D | claim a draw on threefold repetition |
| U / Backspace | undo (against the AI: your last move and its reply; not online) |
| C | copy the room link (online) |
| R | new game menu (Esc returns to the game) |
| F | flip view |
| A | toggle auto-rotate to the side to move |

## Layout

- `src/chess.{hpp,cpp}`: rules engine (no raylib dependency), with move generation verified by perft
- `src/ai.{hpp,cpp}`: AI opponent: alpha-beta with quiescence, piece-square tables, a time budget per level, and an explicit stack so it searches a few ms per frame instead of freezing the page
- `src/pieces.{hpp,cpp}`: procedural piece meshes
- `src/shaders.hpp`: lighting shader (GLSL 330 desktop / GLSL 100 web)
- `src/net.{hpp,cpp}`, `web/net.js`: online rooms; the C++ side calls the PeerJS code in `net.js` (bundled with `--pre-js`) and stubs everything out on desktop
- `src/main.cpp`: app loop, camera, picking, rendering, HUD, online room protocol
- `tests/perft.cpp`: node counts against published perft results
- `tests/rules.cpp`: which positions count as repeated, UCI move text
- `tests/ai.cpp`: evaluation symmetry, mates in one, time budget

## Not yet

50-move rule / insufficient material, sound, touch orbit controls.
