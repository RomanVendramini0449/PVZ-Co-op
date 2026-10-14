# Roadmap

Ordered so that the riskiest assumption is tested first and each phase is
playable or verifiable on its own.

---

## Phase 0 — Prove determinism before building on it

**Do this first. It is the cheapest way to find out whether the whole plan works.**

1. Fork `wszqkzqk/PvZ-Portable`, build it on both machines, drop in your own
   `main.pak` + `properties/`, confirm the stock game runs.
2. Pin the float environment in CMake: `-ffp-contract=off`, no `-ffast-math`,
   SSE2 math, no x87.
3. Use the **existing demo recorder**: record a full level on machine A, replay
   that recording on machine B with the same binary.
4. If it replays identically, lockstep is viable and everything downstream is
   mechanical. If it desyncs, the sync asserts
   (`DemoSyncInt` / `DemoAssertIntEqual`) tell us where, and we fix those sites
   before writing a line of netcode.

**Exit criteria:** a recorded level replays bit-identically on the other machine.

> If Phase 0 fails in a way we cannot fix, that is the moment to reconsider
> host-authoritative snapshots — not later. Everything after this point assumes
> Phase 0 passed.

---

## Phase 1 — Transport and handshake

- `src/Coop/Net.{h,cpp}`: TCP listener/dialer, `TCP_NODELAY`, length-prefixed
  frames. Reuse `DataReader`/`DataWriter` (`Lawn/System/DataSync.h`) as the codec
  — it already does in-memory buffers.
- Lobby UI: Host / Join-by-IP / port / difficulty, plus CLI flags
  (`--host`, `--join IP:port`, `--difficulty`) so testing needs no clicking.
- Handshake with hard rejection on protocol, build-hash or asset-hash mismatch,
  and a readable error for each.
- Seed + difficulty + level selection exchange.

**Exit criteria:** two instances connect, agree, and print a matching seed.

---

## Phase 2 — Lockstep core

- Tick scheduler with input delay `D`; command ring buffer keyed by tick.
- Send an (often empty) command frame every tick.
- Stall on missing peer input; keep rendering interpolated frames via
  `DoUpdateFramesF`; "waiting for player" overlay after ~500 ms; adaptive `D`.
- Desync checksum every 25 ticks (`mSunMoney`, `mMainCounter`, plant/zombie
  counts, quantised zombie positions and health) with a clean error on mismatch.

**Exit criteria:** both machines run the same level in step with one player's
input, and an injected fault is reported as a desync rather than silently drifting.

---

## Phase 3 — Two cursors

- Split `mCursorObject`, `mCursorPreview`, `mIgnoreMouseUp` and the `SeedBank`
  selection into per-player state indexed by `CoopCommand::player`.
- Route `Board::MouseDown/MouseMove/MouseUp` through the command layer.
- Render the remote cursor as a ghost, plus the remote player's seed highlight.

**Exit criteria:** both players can plant on a shared lawn from a shared sun bank.
*This is the first genuinely playable co-op build.*

---

## Phase 4 — Full input coverage

The long tail. Each is a `MouseDown` variant to wrap as a command:

- Core: plant, shovel, collect sun, collect coin, Flower Pot / Lily Pad placement,
  Grave Buster, Cherry Bomb / Squash / Jalapeno placement, Cob Cannon fire
  (`MouseDownCobcannonFire`), Imitater.
- Minigames and puzzles: Wall-nut Bowling, I Zombie, Vasebreaker,
  Whack-a-Zombie, Beghouled, Zombiquarium, conveyor-belt levels.
  Puzzle modes already seed from `Board::GetLevelRandSeed()`, so they are
  deterministic per level — they just need their inputs routed.

**Exit criteria:** every interaction in Adventure mode works for both players.

---

## Phase 5 — Campaign flow

- Host's Adventure profile is the one that progresses; client's stays untouched.
- Host drives level transitions, seed chooser (shared selection, host confirms),
  level complete / game over.
- Anything outside a level (store, almanac, Zen Garden) is host-driven with a
  "waiting for host" screen on the client. Big scope reduction, and it matches
  how people actually play a co-op campaign.
- Disconnect handling: a level can be restarted deterministically from its seed,
  so "rejoin and replay the level" is the cheap recovery path.

**Exit criteria:** 1-1 through 5-10 playable start to finish without returning to
the menu.

---

## Phase 6 — Difficulty and polish

- Sun ×0.5 in `Board::AddSunMoney`; audit level starting sun and tutorial grants
  so nothing becomes unwinnable.
- Zombie HP ×1.5 at spawn, integer math.
- Difficulty indicator in the UI; verify it is locked at handshake.
- Latency/desync diagnostics overlay.

---

## Scope reality

Phases 0-3 are the hard, interesting part and get you a playable shared lawn.
Phase 4 is where the real time goes — PvZ has a lot of one-off interactions, and
the minigames each break their own rules. Adventure mode proper is the achievable
core; full minigame parity is a stretch goal, not a launch requirement.

Worth saying plainly: this is a multi-week project, not a weekend. But it is
*tractable*, which it would not have been from a Ghidra listing.
