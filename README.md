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

Turn on **Battle mode** in the menu before starting a hotseat or vs-AI game. Every capture becomes a turn-based duel in an arena: the capturing piece strikes first, and each turn a fighter picks **Attack**, one of its two **skills** (each with its own 2-turn cooldown), **Guard** (half damage until its next turn, plus a small heal) or, once its meter is full, its **Ultimate**. Each fighter's name, HP bar, ultimate meter and active effects (with turns left) float over it. Against the AI, the AI fights its own battles.

| Piece | HP | ATK | DEF | SPD | Skill 1 | Skill 2 |
|---|---|---|---|---|---|---|
| Pawn | 40 | 10 | 4 | 5 | Valiant Strike: 2.2x power, 35% to miss | Rally: heal 15%, next hit +60% |
| Knight | 60 | 13 | 6 | 9 | Leap Strike: 1.3x power, ignores defense, Bleed 2 | Feint: 75% dodge against the next hit |
| Bishop | 55 | 12 | 5 | 7 | Mend: heal 40% of max HP | Drain: 1.0x hit, heal half the damage dealt |
| Rook | 80 | 14 | 9 | 3 | Bulwark: block 75% of the next hit and strike back | Ram: 1.8x power, 20% to miss, 35% to Stun |
| Queen | 95 | 17 | 8 | 7 | Royal Storm: three hits at 0.6x power | Dominate: 1.2x hit, Weakened for the enemy's next action |
| King | 100 | 12 | 10 | 4 | Decree: 1.5x power, Exposed 2 | Royal Guard: heal 20% and guard |

**Ultimates.** The meter fills from damage dealt (3 per HP), damage taken (2 per HP) and 10 per turn; at 100 the Ultimate is ready and using it empties the meter.

| Piece | Ultimate | Effect |
|---|---|---|
| Pawn | En Passant | 2.0x, ignores defense, can't miss and goes through guard and dodges; Bleed 2 |
| Knight | Fork | two hits at 1.0x ignoring defense; Stun if both land |
| Bishop | Holy Fire | 1.4x hit and Burn 3; the bishop gains Regen 3 |
| Rook | Siege | 2.4x, ignores defense, 10% to miss; Exposed 3 |
| Queen | Checkmate | four hits at 0.7x, then Weakened 2 and Exposed 2 |
| King | Divine Right | clears its debuffs, heals 30%, Regen 3 and Haste 2 |

**Effects** last a number of the carrier's turns. Burn (6% of max HP), Bleed (10%) and Regen (+8%) tick at the start of the carrier's turn. Stun skips its next action, and a fighter can't be stunned again right after. Weakened halves its damage, Exposed makes it take 35% more, Haste makes it 20% harder to hit.

Hits can crit (12%, 1.6x, ignores defense). A faster target dodges more often. After 12 turns both sides tire and every hit grows stronger, so no battle lasts forever.

On the board, selecting a piece opens an action panel in the top-right corner: click a highlighted square to move, or use one of the piece's two abilities instead: its **support ability** (button or E) or its **strike** (button or Q). An ability takes the whole turn; afterwards the support ability is unavailable for 3 of that side's turns and the strike for 4. Each can only be used when it would change something. Buffs and curses show over the piece and are used up by its next battle, whether it attacks or defends.

| Piece | Ability | Effect |
|---|---|---|
| Pawn | Shield Up | enters its next battle guarded (half damage until its own next turn) |
| Knight | Battle Cry | in its next battle, its first hit gets +60% |
| Bishop | Heal | allies on the 8 surrounding squares regain 25% of their max HP |
| Rook | Fortify | starts its next battle with Bulwark |
| Queen | Inspire | allies on the 8 surrounding squares get +60% on their next battle's first hit |
| King | Rest | heals 30% of its max HP |

Strikes hit enemy pieces on the board. Their damage never takes a piece below 1 HP, and their curses wait on the target until its next battle, where they start as Burn 3, Bleed 2, Exposed 2 or Weakened 2.

| Piece | Strike | Effect |
|---|---|---|
| Pawn | Pike Jab | enemies on its two capture squares: 6 damage and Bleed |
| Knight | Harass | enemies a knight's jump away: 4 damage and Exposed |
| Bishop | Smite | the first piece along each diagonal, if an enemy: 10 damage and Burn |
| Rook | Cannon | the first piece along each rank and file, if an enemy: 12 damage |
| Queen | Terror | enemies within 2 squares: Weakened |
| King | Condemn | adjacent enemies: 8 damage and Burn |

- If the attacker wins, the capture happens as normal. If it loses, the attacker is removed, the defender keeps its square and the turn passes.
- Damage persists: wounded pieces show an HP bar on the board. A pawn that promotes comes back at full HP.
- King hunt: check rules are off. You may leave your king attacked, and moving onto the enemy king starts a battle like any other capture. Checkmate and stalemate don't end the game; it ends when a king dies in battle (a king that loses while attacking dies too).
- The toggle applies from the next new game.
- The AI attacks an enemy king whenever it can, with its healthiest attacker. Otherwise it uses an ability only when it clearly helps (a bishop healing a neighbour below half HP, resting a king below half HP, or a strike worth at least 14, counting damage plus 6 per new curse; it picks the best one); its search plays ordinary chess and doesn't know about battles or buffs.
- Each duel ends with VICTORY or DEFEAT from your side against the AI, or the winning colour in hotseat. When a king falls (or on checkmate in a normal game), a VICTORY / DEFEAT screen shows battles won, abilities used and pieces lost per side, with Rematch and Menu buttons.
- Using an ability resets the repetition count, like a capture.

In a battle, choose a command with 1-4 (5 or U for the Ultimate), the arrow keys and Enter, or the mouse. In battle the AI fires its Ultimate as soon as it is ready; an AI king saves it until it is hurt or debuffed.

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

- `src/chess.{hpp,cpp}`: rules engine (no raylib dependency), with move generation verified by perft
- `src/ai.{hpp,cpp}`: AI opponent: alpha-beta with quiescence, piece-square tables, a time budget per level, an optional margin for picking a random near-best move (the easier levels), and an explicit stack so it searches a few ms per frame instead of freezing the page
- `src/pieces.{hpp,cpp}`: procedural piece meshes
- `src/shaders.hpp`: lighting shader (GLSL 330 desktop / GLSL 100 web)
- `src/net.{hpp,cpp}`, `web/net.js`: online rooms; the C++ side calls the PeerJS code in `net.js` (bundled with `--pre-js`) and stubs everything out on desktop
- `src/battle.{hpp,cpp}`: battle-mode duel rules and board abilities (no raylib dependency)
- `src/main.cpp`: app loop, camera, picking, rendering, HUD, online room protocol, battle arena scene, action panel, end screen
- `tests/perft.cpp`: node counts against published perft results
- `tests/rules.cpp`: which positions count as repeated, UCI move text, battle-mode pseudo-legal moves (walking into check, capturing a king)
- `tests/ai.cpp`: evaluation symmetry, mates in one, the near-best-move margin, time budget
- `tests/battle.cpp`: battles end, HP stays in range, separate cooldowns, healing caps, each second skill's effect, rough balance, board buffs entering a battle, board abilities (range, cooldown, caps)

## Not yet

50-move rule / insufficient material, sound, touch orbit controls.
