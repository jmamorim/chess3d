# chess3d

A 3D chess prototype in C++17 with [raylib](https://www.raylib.com/) 5.5. Play hotseat, against a built-in AI (Easy / Medium / Hard), or online against a friend (web build only), with the full rules: castling, en passant, promotion (pick the piece from a 3D picker above the square), check, checkmate, stalemate and draw by repetition (claimable on the third occurrence, automatic on the fifth). The pieces are generated in code as lathe profiles plus boxes, so there are no asset files.

## Build (desktop)

```sh
cmake -B build
cmake --build build -j
./build/chess3d
ctest --test-dir build   # perft, rules, AI and battle tests
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

In the menu, **Create room** gives you a random colour and a 4-letter code; the page address becomes `…/chess3d/#CODE`. Send that link (press C to copy it) or the code; your friend opens the link or picks **Join room** and types the code. The browsers connect directly over WebRTC through [PeerJS](https://peerjs.com/), whose free public server only introduces them. There is no game server.

Each browser keeps the room's moves in `localStorage`, so reopening the link after a reload or a dropped connection puts you back in your seat with the board as it was. There is no clock and no undo online. Players behind strict NATs may fail to connect, because no TURN relay is configured.

## Battle mode (offline)

Turn on **Battle mode** in the menu before starting a hotseat or vs-AI game. Every capture becomes a turn-based duel in an arena: the capturing piece strikes first, and each turn a fighter picks **Attack**, one of its two **skills** (each with its own 2-turn cooldown) or **Guard** (half damage until its next turn, plus a small heal). Each fighter's name, HP bar and active effects float over it. Against the AI, the AI fights its own battles.

| Piece | HP | ATK | DEF | SPD | Skill 1 | Skill 2 |
|---|---|---|---|---|---|---|
| Pawn | 40 | 10 | 4 | 5 | Valiant Strike: 2.2x power, 35% to miss | Rally: heal 15%, next hit +60% |
| Knight | 60 | 13 | 6 | 9 | Leap Strike: 1.3x power, ignores defense | Feint: 75% dodge against the next hit |
| Bishop | 55 | 12 | 5 | 7 | Mend: heal 40% of max HP | Drain: 1.0x hit, heal half the damage dealt |
| Rook | 80 | 14 | 9 | 3 | Bulwark: block 75% of the next hit and strike back | Ram: 1.8x power, 20% to miss |
| Queen | 95 | 17 | 8 | 7 | Royal Storm: three hits at 0.6x power | Dominate: 1.2x hit, the enemy's next action does half damage |
| King | 100 | 12 | 10 | 4 | Decree: 1.5x power | Royal Guard: heal 20% and guard |

Hits can crit (12%, 1.6x, ignores defense). A faster target dodges more often. After 12 turns both sides tire and every hit grows stronger, so no battle lasts forever.

- If the attacker wins, the capture happens as normal. If it loses, the attacker is removed, the defender keeps its square and the turn passes.
- Damage persists: wounded pieces show an HP bar on the board. A pawn that promotes comes back at full HP.
- King hunt: check rules are off. You may leave your king attacked, and moving onto the enemy king starts a battle like any other capture. Checkmate and stalemate don't end the game; it ends when a king dies in battle (a king that loses while attacking dies too).
- The toggle applies from the next new game.
- The AI attacks an enemy king whenever it can, with its healthiest attacker. Otherwise its search plays ordinary chess and doesn't know about battles.

In a battle, choose a command with 1-4, the arrow keys and Enter, or the mouse.

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
- `src/ai.{hpp,cpp}`: AI opponent: alpha-beta with quiescence, piece-square tables, a time budget per level, an optional margin for picking a random near-best move (the easier levels), and an explicit stack so it searches a few ms per frame instead of freezing the page
- `src/pieces.{hpp,cpp}`: procedural piece meshes
- `src/shaders.hpp`: lighting shader (GLSL 330 desktop / GLSL 100 web)
- `src/net.{hpp,cpp}`, `web/net.js`: online rooms; the C++ side calls the PeerJS code in `net.js` (bundled with `--pre-js`) and stubs everything out on desktop
- `src/battle.{hpp,cpp}`: battle-mode duel rules (no raylib dependency)
- `src/main.cpp`: app loop, camera, picking, rendering, HUD, online room protocol, battle arena scene
- `tests/perft.cpp`: node counts against published perft results
- `tests/rules.cpp`: which positions count as repeated, UCI move text, battle-mode pseudo-legal moves (walking into check, capturing a king)
- `tests/ai.cpp`: evaluation symmetry, mates in one, the near-best-move margin, time budget
- `tests/battle.cpp`: battles end, HP stays in range, separate cooldowns, healing caps, each second skill's effect, rough balance

## Not yet

50-move rule / insufficient material, sound, touch orbit controls.
