# chess3d

A 3D chess prototype in C++17 with [raylib](https://www.raylib.com/) 5.5. Play hotseat, against a built-in AI (Easy / Medium / Hard), or online against a friend (web build only), with the full rules: castling, en passant, promotion (pick the piece from a 3D picker above the square), check, checkmate, stalemate and draw by repetition (claimable on the third occurrence, automatic on the fifth). The pieces are generated in code as lathe profiles plus boxes, so there are no asset files.

## Build (desktop)

```sh
cmake -B build
cmake --build build -j
./build/chess3d
ctest --test-dir build   # perft, rules, AI, battle and game tests
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

## AI strength

| Level | Search | Elo (95% range) |
|---|---|---|
| Easy | depth 2, then a random move within 80 cp of the best | ~1190 (1140–1240) |
| Medium | depth 4, then a random move within 45 cp of the best | ~1430 (1390–1480) |
| Hard | as deep as fits in 1.5 s, always the best move | ~1900 (1850–1950) |

Measured in November 2026 on a desktop CPU over 200 games per level against Stockfish 19 with `UCI_LimitStrength`. Stockfish played at 20 s + 0.2 s, and the games started from 24 short openings with each colour. The numbers are on Stockfish's computer rating scale (CCRL-anchored) at a faster time control than it is calibrated for, so treat them as rough, not as human or FIDE ratings. In the browser, Hard searches less deeply in its 1.5 s and plays somewhat weaker.

## Online play (web only)

In the menu, **Create room** gives you a random colour and a 4-letter code; the page address becomes `…/chess3d/#CODE`. Send that link (press C to copy it) or the code; your friend opens the link or picks **Join room** and types the code. The browsers connect directly over WebRTC through [PeerJS](https://peerjs.com/), so there is no game server, and each keeps the room's turns in `localStorage` so a reload puts you back in your seat. Details and the wire protocol: [docs/online.md](docs/online.md).

## Battle mode

Turn on **Battle mode** in the menu before starting a hotseat, vs-AI or online game. Every capture becomes a turn-based duel in an arena (Attack, two skills, Guard, an Ultimate), pieces carry damage between battles, check rules are off and a game ends when a king dies. On the board, selected pieces can use a support ability or a strike instead of moving. Piece stats, abilities, effects and AI behaviour: [docs/battle-mode.md](docs/battle-mode.md).

## Controls

| Input | Action |
|---|---|
| Left click | select a piece / move it |
| Right drag | orbit camera |
| Wheel | zoom |
| Esc | cancel the promotion picker |
| E | use the selected piece's support ability (battle mode) |
| Q | use the selected piece's strike (battle mode) |
| D | claim a draw on threefold repetition |
| U / Backspace | undo (against the AI: your last move and its reply; not online) |
| C | copy the room link (online) |
| R | new game menu (Esc returns to the game) |
| F | flip view |
| A | toggle auto-rotate to the side to move |

## Layout

- `src/chess/`: `chess.{hpp,cpp}`, the rules engine (no raylib dependency), with move generation verified by perft
- `src/ai/`: `ai.{hpp,cpp}`, the AI opponent: alpha-beta with quiescence, piece-square tables, a time budget per level, an optional margin for picking a random near-best move (the easier levels), and an explicit stack so it searches a few ms per frame instead of freezing the page
- `src/game/`: `game.{hpp,cpp}`, headless game state (position, battle units, history, pending battle) with a text record that replays exactly; the online protocol sends it
- `src/battle/`: `battle.{hpp,cpp}`, battle-mode duel rules and board abilities (no raylib dependency); `battle_scene.cpp`, the arena: animation, command input and drawing
- `src/net/`: `net.{hpp,cpp}`, the bridge to the PeerJS code in `web/net.js` (bundled with `--pre-js`), stubbed out on desktop; `online.cpp`, room protocol: turn/cmd/sync messages, saving and resuming a room
- `src/render/`: `pieces.{hpp,cpp}`, procedural piece meshes; `shaders.hpp`, the lighting shader (GLSL 330 desktop / GLSL 100 web); `board_view.cpp`, board, pieces, promotion picker, labels and the HP bars over pieces
- `src/ui/`: `hud.cpp`, the menu, room-code input, HUD, action panel and end screen
- `src/app/`: `app.hpp`, the `App` class (all state and member declarations); `app.cpp`, game flow, the AI player, the update loop and draw dispatcher, pointer handling
- `src/main.cpp`: window setup and the main loop
- `web/`: `net.js` (PeerJS wrapper), `shell.html` (page template for the web build)
- `tests/`: `check.hpp`, the shared `expect` helper
  - `perft.cpp`: node counts against published perft results
  - `rules.cpp`: which positions count as repeated, UCI move text, battle-mode pseudo-legal moves (walking into check, capturing a king)
  - `ai.cpp`: evaluation symmetry, mates in one, the near-best-move margin, time budget
  - `game.cpp`: record encode/replay round-trips, undo, battle and ability turns, seeds
  - `battle.cpp`: battles end, HP stays in range, separate cooldowns, healing caps, each second skill's effect, rough balance, board buffs entering a battle, board abilities (range, cooldown, caps)
- `docs/`: `battle-mode.md` (rules, tables), `online.md` (online play and its protocol)

## Not yet

50-move rule / insufficient material, sound, touch orbit controls.
