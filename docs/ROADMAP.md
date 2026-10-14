# Roadmap

Scope is locked in [`SPEC.md`](SPEC.md): campaign only, two players, direct IP.

---

## Phase 0 — Determinism check *(yours to run; does not block me)*

**Short answer: no, you do not have to do Phase 0 before I start.** I am
building Phases 1-5 now. But you should run it before we trust a real session,
and it costs you well under an hour.

### What it is, plainly

Lockstep co-op works by running the *same* game on both machines and sending
only the button presses between them. Nothing about the board is transmitted —
both machines compute identical results from identical inputs. That is what
keeps it cheap and keeps everything in sync.

That only holds if the game is **deterministic**: same inputs in, same frame
out, every time. If one machine computes a zombie's position as `12.0000001`
and the other gets `12.0`, the two lawns slowly drift apart until they are
different games.

Phase 0 tests whether that is true, using a recorder the engine already ships.

### 0a — Single machine (do this whenever; catches most problems)

Record a level with the built-in demo recorder, then replay the recording. If it
plays back identically, the game is internally deterministic.

This catches the common killers: logic that depends on wall-clock time,
uninitialised memory, anything keyed off a pointer address.

### 0b — Two machines (needs the game on both; the real test)

Record on machine A, replay the same recording on machine B with the same
binary. This is the one that catches floating-point differences between CPUs
and compilers.

### If it fails

The engine has built-in sync assertions (`DemoSyncInt`, `DemoAssertIntEqual`)
that report *where* the two runs diverged, so a failure is diagnosable rather
than mysterious. We fix those sites.

### Why I am not treating it as a blocker

The risk is genuinely low for your setup. Float determinism breaks across
*different* compilers, architectures, or build flags — you and your friend will
run the same binary on the same CPU architecture, which is the easy case. On top
of that, the engine ships a working, actively maintained demo record/replay
system, which is itself evidence that deterministic replay holds today.

And most of the work does not depend on the answer: the transport, the lobby,
the per-player seed banks, the cursors and the campaign flow are all needed
either way. Only Phase 2 is lockstep-specific.

---

## Phase 1 — Transport, handshake, lobby  ✅ *(lobby UI still to come)*

- `src/Coop/` module: TCP listener/dialer, `TCP_NODELAY`, length-prefixed frames.
  Reuses `DataReader`/`DataWriter` (`Lawn/System/DataSync.h`) as the wire codec.
- Handshake with hard rejection and a readable error on mismatched protocol
  version, build hash, or asset hash.
- Exchange: difficulty, starting level, RNG seed, host's unlock state.
- **Host Game** / **Join Game** buttons on the main menu; connected-player icon.
- CLI flags (`--host`, `--join IP:port`, `--difficulty`) so testing needs no
  clicking.

**Done when:** two instances connect, agree on a seed, and show each other's
connected icon.

---

## Phase 2 — Lockstep core  ✅

- Tick scheduler with input delay `D`; per-tick command ring buffer.
- A command frame every tick, empty or not.
- Stall on missing peer input, keep rendering interpolated frames, show
  "waiting for player" after ~500 ms, raise `D` if stalls recur.
- Desync checksum every 25 ticks with a clean error instead of silent drift.

**Done when:** both machines run a level in step from one player's input, and an
injected fault reports as a desync rather than drifting.

---

## Phase 3 — Two players on one lawn  🔨 *(state split done; input routing and remote cursor next)*

The central refactor: **`Board::mSeedBank` → `mSeedBank[2]`**, which gives
per-player cooldowns and per-player conveyor belts at once (SPEC §5).

- Split cursor / held-plant / shovel state per player.
- Route `Board::MouseDown/MouseMove/MouseUp` through the command layer, tagged
  with player index.
- Draw the remote cursor, including what it is holding, with the
  show/hide rules from SPEC §7.

**Done when:** both players plant on a shared lawn, from a shared sun pool, on
independent cooldowns. *First genuinely playable co-op build.*

---

## Phase 4 — Campaign interaction coverage

Every in-level interaction routed as a command: plant, shovel, collect sun,
collect coin, Flower Pot / Lily Pad, Grave Buster, instant-use plants
(Cherry Bomb, Squash, Jalapeno), Cob Cannon fire, Imitater.

Then the special campaign levels (SPEC §6): split conveyor belts at half rate
each, Wall-nut Bowling throws, and the Dr. Zomboss finale.

**Done when:** every interaction in the campaign works for both players.

---

## Phase 5 — Campaign flow

- Skip the opening tutorial levels; start at the first real level.
- Host-driven cutscenes and Crazy Dave dialogue; client input swallowed (SPEC §3).
- Host-driven level transitions and level complete / game over.
- Store: shared money, shared upgrades, host navigates.
- Host's Adventure profile is the one that progresses; client's stays untouched.
- Disconnect handling — a level restarts deterministically from its seed, so
  "rejoin and replay the level" is the cheap recovery path.

**Done when:** the campaign is playable start to finish without touching the menu.

---

## Phase 6 — Difficulty and polish

- Sun ×0.5 at `Board::AddSunMoney`; audit starting sun so Hard stays winnable.
- Zombie health ×1.5 at spawn, integer math.
- Difficulty shown in the UI; verified locked at handshake.
- Latency and desync diagnostics overlay.

---

## What I need from you, and when

| When | What |
|---|---|
| **Now** | Nothing. I am starting Phase 1. |
| Before we trust a live session | Run **Phase 0a**, then **0b** once you have the game on both machines. |
| When Phase 3 lands | Play it. Per-player cooldowns change the feel a lot and you should sanity-check the balance. |
| When Phase 6 lands | Tell me if Hard is too brutal — ×0.5 sun *and* ×1.5 health compounds hard. |
