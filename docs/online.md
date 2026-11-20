# Online play

In the menu, **Create room** gives you a random colour and a 4-letter code; the page address becomes `…/chess3d/#CODE`. Send that link (press C to copy it) or the code; your friend opens the link or picks **Join room** and types the code. The browsers connect directly over WebRTC through [PeerJS](https://peerjs.com/), whose free public server only introduces them. There is no game server.

Each browser keeps the room's turns (moves, abilities and battle commands) in `localStorage`, so reopening the link after a reload or a dropped connection puts you back in your seat with the board as it was. There is no clock and no undo online. Players behind strict NATs may fail to connect, because no TURN relay is configured.

## Protocol

The wire format and the saved format, as implemented in `src/net/online.cpp`, `src/game/game.cpp` and `web/net.js`. Online play exists only in the web build; on desktop `net::available()` is false and everything here is a no-op.

### Transport

- `web/net.js` wraps PeerJS (WebRTC data channels). `src/net/net.cpp` calls it through `EM_JS`; `net::send` sends one string over the open connection (dropped if there is none), and `net::poll` returns the next incoming string. Each message is one line of text with space-separated fields.
- A room code is 4 characters from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (no `0/O/1/I`). The page address carries it as `#CODE`, and opening such a link joins or resumes that room.
- The **host** registers the PeerJS peer id `chess3d-<CODE>` on the public PeerJS server. The **guest** creates an anonymous peer and opens a reliable connection to `chess3d-<CODE>`. After the introduction the two browsers talk directly.
- A second guest connecting while a connection is open receives the message `full` and is closed 500 ms later. The App answers `full` by leaving the room, opening the menu and showing "That room already has two players".
- Connection events arrive in the same queue as lines starting with `@`:

| Line | Meaning |
|---|---|
| `@waiting` | the host is registered and waiting; for a guest, the host is not there (yet), and it retries every 3 s |
| `@connected` | the data connection is open; each side answers by sending a `sync` |
| `@disconnected` | the data connection closed |
| `@error <type>` | a PeerJS error type, or `no-peerjs` if the library did not load |

- `@waiting` does not downgrade an established `connected` state. A host that gets `@error unavailable-id` (someone already holds that code) picks a new code if the game has no moves yet (a brand-new room). A host that is resuming a room (`net::host(code, true)`, used when a room is reopened from a link or reload) retries the id every 3 s without reporting an error, because the old registration can linger after a reload. A guest checks every 3 s and reconnects if it has no connection.

### Messages

Every message is validated on arrival; one that does not fit the current state is ignored.

**`turn <ply> <token>`** is sent by the player who just made a turn (`sendTurn`). `<ply>` is the game's ply before the turn, `<token>` is its record token (see below). A battle capture is announced as `e4d5:`, with the commands to follow in `cmd` messages, and a claimed repetition draw is sent as `=`. The receiver first settles a battle it is showing (it applies the opponent's queued commands, and finishes the battle if it is over). Then it ignores the message unless it is seated, `<ply>` equals its own ply, it is the sender's side to move, no battle is pending and the game is not over. After that:

- `=` claims the repetition draw.
- An ability token (contains `!`) must be exactly 4 characters, `<file><rank>!<0|1>`, on the board, and `canUseAbility` must allow it.
- Otherwise the move is looked up among the legal moves (the part before any `:`). It is dropped if the move starts a battle but the token has no `:`, or the other way round, or if anything follows the `:`.

The turn is then performed and the room saved.

**`cmd <ply> <k> <c>`** is sent by the player who picked a command for their fighter during a battle. `<ply>` is the game's ply (a pending battle's capture is not recorded yet, so it equals the `ply` of the `turn` that began it), `<k>` is how many commands had already been given (`pendingCommands()`) and `<c>` is the command digit 0-4. The receiver needs a pending battle, a seat and `<ply>` equal to its own. `<k>` must equal the commands already applied plus those still queued while the animation plays, which keeps commands in order. It then looks ahead on a copy of the battle with the queued commands applied, and accepts `<c>` only if the battle is not over, the fighter on turn is the opponent's and the battle allows `c` (`Battle::canUse`). Accepted commands wait in a queue until the animation has caught up.

**`sync <hostSide|-> <B|-> <record>`** is sent by both sides on every `@connected`. The first field is `w` or `b` from the host (its own colour) and `-` from the guest. The second is `B` for a battle game and `-` otherwise; any other value makes the receiver ignore the message. The rest is the game record. Receiver:

- A guest takes the opposite colour to the host's and is seated.
- A guest whose battle flag differs from the sync's adopts the sender's flag and record as they are. The host's flag always wins.
- Otherwise, if the received record extends ours, it is accepted, but only if every action after our own (`Game::actions()`) is the sender's: the replay refuses a record that moves or fights for the receiving side. "Extends" means ours is a prefix at a token boundary, or the last token is a battle token (one containing `:`) that gains more command digits.
- Otherwise, a guest that differs from the host's record, and whose own record does not extend the host's, adopts the host's record. The host never replaces its record this way, so on disagreement the host's record wins. If the guest's record is longer and extends the host's, the host accepts it under the rule above.
- When a record does not replay, it is refused and the game stays as it was. Afterwards the view is turned to the player's side and the room is saved.

Seeds: both sides roll the same dice. The base seed is `seedFromCode(room)`, the FNV-1a hash (offset 2166136261, prime 16777619) of the room code's bytes. A battle's seed is `battleSeed(base, ply)`, a hash mix of the base and the ply at which the battle begins.

### Record tokens

A game record is its turns as space-separated tokens (`Game::encode`, replayed by `Game::replay`):

| Token | Meaning |
|---|---|
| `e2e4`, `e7e8q` | a move in UCI notation; a promotion adds the piece letter `n`, `b`, `r` or `q` |
| `e2!0`, `e2!1` | the piece on `e2` uses its support ability (`0`) or strike (`1`) |
| `e4d5:0142` | a battle capture, with the digits of the commands given in it: 0 Attack, 1 Skill, 2 Skill2, 3 Guard, 4 Ultimate |
| `=` | a threefold-repetition claim; only ever last |

A battle that is still in progress is appended last, as `e4d5:` plus the commands so far, and must be the final token. In a record that does not replay, the valid prefix stays applied and the replay reports failure. Move and battle tokens are only accepted when the rules allow them (battle tokens only in a battle game; plain moves only when they do not start a battle).

### Saved rooms

Each browser stores the room under the `localStorage` key `chess3d:<CODE>`:

```
<host|guest> <w|b|-> <B|-> <tokens...>
```

The role, then the player's colour (`-` until a guest has been seated by the host's first sync), then `B` for a battle game or `-`, then the record. It is rewritten after every turn, command and sync. Reopening the room replays the record and reconnects as host (resuming its id) or guest. A record saved before battle mode went online has no `B`/`-` field and loads as a non-battle game; its whole remainder is read as moves.
