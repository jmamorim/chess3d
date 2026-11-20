// Game state: turns are recorded as tokens and replay to exactly the same state.
#include <cstdio>
#include <random>
#include <sstream>

#include "game/game.hpp"
#include "check.hpp"

using namespace game;

namespace {

bool samePos(const chess::Position& a, const chess::Position& b) {
    return a.sq == b.sq && a.side == b.side && a.castling == b.castling && a.ep == b.ep;
}

bool sameUnits(const battle::Units& a, const battle::Units& b) {
    for (int i = 0; i < 64; ++i) {
        const battle::Unit &x = a[i], &y = b[i];
        if (x.dmg != y.dmg || x.readyAt != y.readyAt || x.buffs != y.buffs || x.readyAt2 != y.readyAt2 || x.curses != y.curses)
            return false;
    }
    return true;
}

bool sameState(const Game& a, const Game& b) {
    return samePos(a.pos(), b.pos()) && sameUnits(a.units(), b.units()) && a.status() == b.status() && a.reps() == b.reps() &&
           a.kingDead() == b.kingDead() && a.ply() == b.ply();
}

// One random turn: an ability if one is usable (sometimes), otherwise a random move; captures are fought out.
void takeTurn(Game& g, std::mt19937& rng) {
    if (g.battleGame() && rng() % 4 == 0) {
        std::vector<std::pair<int, int>> usable;
        for (int s = 0; s < 64; ++s)
            for (int w = 0; w < 2; ++w)
                if (g.canUseAbility(s, w)) usable.push_back({s, w});
        if (!usable.empty()) {
            const auto [s, w] = usable[rng() % usable.size()];
            g.useAbility(s, w);
            return;
        }
    }
    const chess::Move m = g.legal()[rng() % g.legal().size()];
    if (!g.startsBattle(m)) {
        g.play(m);
        return;
    }
    g.begin(m);
    for (int i = 0; i < 500 && !g.pending()->over(); ++i) g.command(static_cast<battle::Command>(rng() % 5));
    g.finishBattle();
}

std::vector<std::string> split(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    for (std::string t; in >> t;) out.push_back(t);
    return out;
}

std::string join(const std::vector<std::string>& t, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) out += (i ? " " : "") + t[i];
    return out;
}

}  // namespace

int main() {
    // Red if a token (move) is encoded or parsed differently from how play() applies it.
    {
        std::mt19937 rng(7);
        Game g;
        g.reset(false, 1);
        for (int i = 0; i < 30 && !g.over(); ++i) takeTurn(g, rng);
        Game r;
        r.reset(false, 1);
        expect(r.replay(g.encode()) && sameState(g, r) && g.ply() > 10, "plain game replays to the same state");
        expect(g.pos().key() == r.pos().key(), "plain replay has the same position key");
    }

    // Red if any per-turn state (unit fields, carried damage, rook/promotion handling, dice seeding) is not reproduced by replay.
    bool allReplay = true, sawBattle = false, sawAbility = false;
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        std::mt19937 rng(seed);
        Game g;
        g.reset(true, seed * 977);
        std::vector<Game> snaps;
        for (int t = 0; t < 60 && !g.over(); ++t) {
            takeTurn(g, rng);
            snaps.push_back(g);
        }
        const std::string full = g.encode();
        for (const chess::Move& m : g.moves()) sawAbility |= isAbility(m);
        sawBattle |= full.find(':') != std::string::npos;
        Game r;
        r.reset(true, seed * 977);
        allReplay &= r.replay(full) && sameState(g, r) && r.encode() == full;

        // Prefix property: the first k tokens give the state after turn k.
        const std::vector<std::string> toks = split(full);
        for (size_t k : {size_t(1), toks.size() / 3, toks.size() / 2, toks.size() - 1}) {
            if (k == 0 || k > snaps.size()) continue;
            Game p;
            p.reset(true, seed * 977);
            allReplay &= p.replay(join(toks, k)) && sameState(snaps[k - 1], p);
        }
    }
    expect(allReplay, "battle playouts (and their token prefixes) replay to identical state");
    expect(sawBattle && sawAbility, "playouts include battles and abilities");

    // Red if the pending battle's commands are not part of encode(), or replay finishes/drops an unfinished battle.
    {
        bool done = false;
        for (uint32_t seed = 1; seed <= 40 && !done; ++seed) {
            std::mt19937 rng(seed);
            Game g;
            g.reset(true, seed);
            for (int t = 0; t < 80 && !done && !g.over(); ++t) {
                const chess::Move m = g.legal()[rng() % g.legal().size()];
                if (!g.startsBattle(m)) {
                    g.play(m);
                    continue;
                }
                g.begin(m);
                g.command(static_cast<battle::Command>(rng() % 5));
                g.command(static_cast<battle::Command>(rng() % 5));
                if (g.pending()->over()) {
                    g.finishBattle();
                    continue;
                }
                Game r;
                r.reset(true, seed);
                const bool ok = r.replay(g.encode());
                const battle::Battle *a = g.pending(), *b = r.pending();
                expect(ok && b && a->fighter(0).hp == b->fighter(0).hp && a->fighter(1).hp == b->fighter(1).hp && a->turn() == b->turn() &&
                           r.pendingMove().from == g.pendingMove().from && r.encode() == g.encode(),
                       "a pending battle replays with the same HP and turn");
                expect(!r.replay(g.encode() + " e2e4"), "a pending battle must be the last token");
                done = true;
            }
        }
        expect(done, "found a battle to leave pending");
    }

    // Red if useAbility ignores `which` (strike replays as the support ability, or vice versa).
    {
        bool done = false;
        for (uint32_t seed = 1; seed <= 200 && !done; ++seed) {
            std::mt19937 rng(seed);
            Game g;
            g.reset(true, seed);
            for (int t = 0; t < 40 && !done && !g.over(); ++t) {
                for (int s = 0; s < 64 && !done; ++s) {
                    if (!g.canUseAbility(s, 0) || !g.canUseAbility(s, 1)) continue;
                    Game support = g, strike = g;
                    support.useAbility(s, 0);
                    strike.useAbility(s, 1);
                    Game r;
                    r.reset(true, seed);
                    const bool ok = r.replay(strike.encode());
                    expect(ok && sameUnits(r.units(), strike.units()) && strike.lastToken().back() == '1', "a strike replays as the strike");
                    expect(r.units()[s].readyAt2 != support.units()[s].readyAt2 && r.units()[s].readyAt == strike.units()[s].readyAt,
                           "strike and support leave different cooldowns");
                    done = true;
                }
                if (!done) takeTurn(g, rng);
            }
        }
        expect(done, "found a square with both abilities usable");
    }

    // Red if replay accepts or silently skips a bad token, or discards the valid prefix.
    {
        Game g;
        g.reset(false, 3);
        expect(!g.replay("e2e4 e7e5 zz99 d2d4") && g.ply() == 2, "an unparseable token stops replay and keeps the prefix");
        expect(!g.replay("e2e4 e2e4") && g.ply() == 1, "an illegal move stops replay");
        expect(!g.replay("e2e4: ") && !g.replay("e2e4:") && g.ply() == 0, "battle tokens are invalid in a plain game");
        expect(!g.replay("e2!0") && g.ply() == 0, "ability tokens are invalid in a plain game");
        Game b;
        b.reset(true, 3);
        expect(!b.replay("e2e4 e7e5 e2!7") && b.ply() == 2, "a bad ability token stops replay");
        expect(!b.replay("e2e4 d7d5 e4d5") && b.ply() == 2, "a capture without ':' is invalid in a battle game");
        expect(!b.replay("e2e4 d7d5 e4d5:9") && b.ply() == 2 && !b.pending(), "a bad command digit leaves no battle pending");
        expect(b.replay("") && b.ply() == 0, "an empty record replays to the start");
    }

    // Red if undo misses a part of the state a turn changes (units, position, token list).
    {
        bool ok = true;
        for (uint32_t seed = 1; seed <= 10; ++seed) {
            std::mt19937 rng(seed);
            Game g;
            g.reset(true, seed);
            for (int t = 0; t < 50 && !g.over(); ++t) {
                Game c = g;
                takeTurn(c, rng);
                c.undo();
                ok &= sameState(c, g) && c.encode() == g.encode();
                takeTurn(g, rng);
            }
        }
        expect(ok, "undo restores position and units to before the last turn");
    }

    // Red if a lost attack changes the defender's square, drops its damage, or leaves the attacker standing.
    {
        bool done = false;
        for (uint32_t seed = 1; seed <= 300 && !done; ++seed) {
            std::mt19937 rng(seed);
            Game g;
            g.reset(true, seed);
            for (int t = 0; t < 60 && !done && !g.over(); ++t) {
                const chess::Move m = g.legal()[rng() % g.legal().size()];
                if (!g.startsBattle(m)) {
                    g.play(m);
                    continue;
                }
                const chess::Position before = g.pos();
                const int cap = capturedSquare(m);
                g.begin(m);
                for (int i = 0; i < 500 && !g.pending()->over(); ++i) g.command(static_cast<battle::Command>(rng() % 5));
                const bool defenderWon = g.pending()->winner() == 1;
                const int left = g.pending()->fighter(1).hp;
                g.finishBattle();
                if (!defenderWon) continue;
                expect(samePos(g.pos(), repelled(before, m)) && g.pos().sq[cap] == before.sq[cap] && g.pos().sq[m.from] == chess::EMPTY &&
                           g.pos().side != before.side,
                       "a lost attack removes the attacker, the defender keeps its square, the turn passes");
                expect(g.hpAt(cap) == left && g.units()[m.from].dmg == 0, "the defender keeps its damage");
                done = true;
            }
        }
        expect(done, "found a battle the defender wins");
    }

    // Red if battleSeed ignores an input or seedFromCode is not FNV-1a.
    {
        expect(battleSeed(1, 0) != battleSeed(1, 1) && battleSeed(1, 5) != battleSeed(2, 5) && battleSeed(9, 3) == battleSeed(9, 3),
               "battleSeed differs per ply and base, and is deterministic");
        expect(seedFromCode("ABCD") == seedFromCode("ABCD") && seedFromCode("ABCD") != seedFromCode("ABCE"), "seedFromCode is deterministic");
        expect(seedFromCode("") == 2166136261u && seedFromCode("a") == 0xE40C292Cu, "seedFromCode is FNV-1a");
    }

    // Red if aiChoice draws from the dice stream: asking the AI would then change the battle.
    {
        bool same = true;
        int fought = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            std::mt19937 rng(seed);
            Game a;
            a.reset(true, seed);
            bool seen = false;
            for (int t = 0; t < 60 && !seen && !a.over(); ++t) {
                const chess::Move m = a.legal()[rng() % a.legal().size()];
                if (!a.startsBattle(m)) {
                    a.play(m);
                    continue;
                }
                Game b = a;
                a.begin(m);
                b.begin(m);
                std::vector<battle::Command> cmds;
                for (int i = 0; i < 300; ++i) cmds.push_back(static_cast<battle::Command>(rng() % 5));
                for (size_t i = 0; i < cmds.size() && !a.pending()->over(); ++i) {
                    b.aiCommand();
                    a.command(cmds[i]);
                    b.command(cmds[i]);
                }
                same &= a.pending()->fighter(0).hp == b.pending()->fighter(0).hp && a.pending()->fighter(1).hp == b.pending()->fighter(1).hp &&
                        a.encode() == b.encode();
                seen = true;
                ++fought;
            }
        }
        expect(fought >= 10 && same, "aiCommand() does not change a battle's outcome");
    }

    // Red if a draw claim is not part of the record, or a claim/turn is accepted when it shouldn't be.
    {
        const std::string shuffle = "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8";
        Game g;
        g.reset(false, 3);
        expect(!g.replay("g1f3 g8f6 f3g1 f6g8 =") && g.status() != Status::Repetition, "no claim before the third occurrence");
        expect(g.replay(shuffle + " =") && g.over() && g.encode() == shuffle + " =", "a claim replays and is encoded");
        expect(!g.replay(shuffle + " = e2e4") && g.over(), "nothing is played after the game ends");
        g.undo();
        expect(!g.over() && g.encode() == shuffle.substr(0, shuffle.size() - 5), "undo takes back the claim with the last move");
    }

    // Red if a peer's record could add turns or battle commands for the other side.
    {
        Game g;
        g.reset(false, 3);
        expect(!g.replay("e2e4 e7e5 d2d4", 2, chess::Black) && g.ply() == 2, "a new turn by the wrong side is refused");
        expect(g.replay("e2e4 e7e5 d2d4", 2, chess::White) && g.ply() == 3, "a new turn by the right side is taken");
        Game b;
        b.reset(true, 3);
        expect(b.replay("e2e4 d7d5 e4d5:") && b.actions() == 3, "a battle start counts as one action");
        expect(!b.replay("e2e4 d7d5 e4d5:0", 3, chess::Black) && b.pending() && b.pendingCommands() == 0,
               "a command for the other side's fighter is refused");
        expect(b.replay("e2e4 d7d5 e4d5:0", 3, chess::White) && b.pendingCommands() == 1 && b.actions() == 4,
               "a command for the sender's own fighter is taken");
    }

    return failures == 0 ? 0 : 1;
}
