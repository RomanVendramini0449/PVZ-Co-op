# Architecture

How we get from "no code" to "two people playing the campaign together."

All file:line references below point into
[`wszqkzqk/PvZ-Portable`](https://github.com/wszqkzqk/PvZ-Portable) at commit
`Refactor: batch triangles with mixed blend modes (#497)` (2026-10-10), which is
the base we fork. They were read directly from the source, not inferred.

---

## Chosen base

Four reimplementation projects are live. They are not equivalent.

| Project | Stars | Commits | State | Verdict |
|---|---|---|---|---|
| [**wszqkzqk/PvZ-Portable**](https://github.com/wszqkzqk/PvZ-Portable) | ~800 | 931 | "Almost 100% gameplay", C++20 + SDL2, Win/Linux/macOS/Android/iOS/Switch/WASM, active Oct 2026 | **Fork this** |
| [Patoke/re-plants-vs-zombies](https://github.com/Patoke/re-plants-vs-zombies) | ~233 | 51 | The original RE effort. x64 "partial", GLFW renderer WIP, Windows-centric, CC0 | Ancestor, not base |
| [headshot2017/re-plants-vs-zombies](https://github.com/headshot2017/re-plants-vs-zombies) | ~48 | 249 | Patoke fork, SDL+OpenGL, console ports | Superseded |
| [bluisblu/pvz](https://github.com/bluisblu/pvz) | — | — | *Matching* decompile of 0.9.9 beta, WIP | Wrong target (beta, not GOTY) |

PvZ-Portable wins on completeness, activity, build hygiene (CMake + Ninja +
vcpkg, PR-reviewed commits), and portability. It credits Patoke and Headshotnoby
for the groundwork, so it inherits the reverse engineering without inheriting the
Windows-only renderer.

One tradeoff: it is **LGPL-3.0-or-later** (Patoke's is CC0). Our fork must stay
LGPL-3.0-or-later. For an open-source mod that costs us nothing.

---

## The key discovery: this engine is already built for lockstep

Co-op netcode normally fights the engine. Here it does not, because PvZ-Portable
preserves PopCap's original determinism machinery. Five things we verified:

**1. Fixed 100 Hz logic tick, separated from rendering.**
`SexyAppBase::DoUpdateFrames()` is the logic step; `DoUpdateFramesF(float theFrac)`
is a render-only interpolation pass (`SexyAppFramework/SexyAppBase.h:361-363`).
`mUpdateCount / 100` converts ticks to seconds (`SexyAppBase.h:407`), so one tick
is exactly 10 ms. Game logic cannot drift with framerate — the precondition for
lockstep, and we get it for free.

**2. One global Mersenne Twister behind all gameplay randomness.**
`static Sexy::MTRand gMTRand;` (`SexyAppFramework/Common.cpp:39`) backs
`Sexy::Rand(int range)` (`Common.cpp:102`, declared `Common.h:148`). All 198
`Rand()` call sites in `Lawn/` draw from that one stream. Syncing randomness
across two machines is therefore **one seed**, not an audit of 198 sites.

**3. Cosmetic randomness is already isolated from the gameplay stream.**
Particles use libc `srand()`/`rand()` (`PvzpLib/PvzpParticle.cpp:542,546`) and the
draw path uses a throwaway instance seeded off the frame counter
(`Lawn/Challenge.cpp:2766`). Neither touches `gMTRand`. That means the two
machines can render slightly different sparkles without the simulation diverging
by a single bit — exactly the separation we would otherwise have had to build.

**4. A determinism guard already ships, and it is enforced.**
`MTRand::SetRandAllowed()` maintains a `gRandAllowed` counter and
`MTRand::Next()` asserts `DBG_ASSERT(gRandAllowed==0)`
(`SexyAppFramework/misc/MTRand.cpp:75-86,140,185,196`). PopCap used this to stop
drawing code from consuming the gameplay RNG. Debug builds will catch our
determinism violations for us.

**5. There is already a tick-stamped input recorder with desync detection.**
`SexyAppBase` has a complete demo record/replay system: an opcode set
(`DEMO_MOUSE_POSITION`, `DEMO_KEY_DOWN`, `DEMO_IDLE`, … `SexyAppBase.h:105-130`),
tick-stamped commands (`mLastDemoUpdateCnt`, `mDemoCmdUpdateCnt`,
`SexyAppBase.h:307,314`), a serialisation buffer (`mDemoBuffer`, `:304`),
`WriteDemoBuffer()` / `ReadDemoBuffer()` / `ProcessDemo()` (`:396,582-583`), and
explicitly a **sync-verification** layer: `DemoSyncInt()`, `DemoSyncBool()`,
`DemoAssertIntEqual()`, `DemoAssertStringEqual()` (`:585-589`), plus a
`DEMO_SYNC` opcode.

Point 5 is the one that decides the project. A working demo replay is *proof that
a recorded input stream reproduces a bit-identical game*. Deterministic lockstep
is that same system with the input stream arriving from a socket instead of a
file. We are not inventing an architecture; we are re-pointing one that already
works and is already tested.

---

## Why lockstep, not a host-authoritative state stream

The intuitive design — host simulates, client receives the board state — is the
wrong call here, for a concrete reason: **this codebase has no board serialiser.**
`Board::SyncState` is commented out as not recovered (`Lawn/Board.h:262`), and
`DataSync` (`Lawn/System/DataSync.h`, 545 lines of impl) is currently only wired
to `ProfileMgr`. Snapshot netcode would mean hand-writing and then permanently
maintaining serialisers for `Plant`, `Zombie`, `Projectile`, `Coin`, `GridItem`
and friends — thousands of lines, re-broken by every upstream merge.

Deterministic lockstep needs none of that. Both machines run the same simulation
from the same seed and the same inputs, so there is no state to send — only
inputs, a few bytes per tick.

| | Lockstep | Host-authoritative snapshots |
|---|---|---|
| New serialisation code | ~0 (inputs only) | Every game object, forever |
| Bandwidth | ~2 KB/s | Large and scales with zombie count |
| Upstream merge pain | Low | High |
| Fails on float nondeterminism | **Yes** | No |
| Client-side latency on planting | Input delay (~30-60 ms) | Same or worse |

The one real risk is the one row that favours snapshots, so we address it head-on
below rather than discovering it in week three.

`DataSync` is still useful, though: it already supports in-memory buffers
(`DataReader::OpenMemory(...)`, `DataSync.h:46`), so it is the natural wire
codec for the handshake and for any future save/rejoin support.

### Handling the float-determinism risk

Lockstep breaks if the same inputs produce different floats on the two machines.
Mitigations, in order of importance:

1. **Same build, same architecture, both sides.** The handshake rejects a peer
   whose build hash differs. Two x86-64 machines running the same binary is the
   supported configuration; cross-architecture play (x86-64 host, ARM Android
   client) stays unsupported until proven.
2. **Pin the float environment.** `-ffp-contract=off`, never `-ffast-math`, SSE2
   math rather than x87 (no 80-bit intermediates), no `long double`.
3. **Integer math in anything we add.** Difficulty scaling uses rational integer
   arithmetic (`x * 3 / 2`), never a float multiplier.
4. **Detect, don't hope.** Checksum the gameplay state every 25 ticks and compare
   with the peer. On mismatch, say so plainly and drop to a clean error instead of
   letting the two lawns silently drift apart. This reuses the existing
   `DemoAssertIntEqual` idea.
5. **Prove it before building on it (Phase 0).** Record a demo on machine A,
   replay it on machine B, confirm no sync error. That is a day of work and it
   de-risks the entire project.

---

## Netcode design

### Transport

TCP, one connection, `TCP_NODELAY` set, length-prefixed frames. At 100 Hz with
~16-byte commands this is ~2 KB/s — TCP's reliability and ordering are worth far
more than the latency we would claw back with UDP at this data rate. Two ticks
per packet halves header overhead if needed.

Direct IP, as asked: host listens on a port (default 1337), client dials
`IP:port`. No lobby server, no relay, no NAT traversal — LAN, or a forwarded port
for internet play.

### Handshake

Both sides must agree on everything that feeds the simulation before tick 0:

- protocol version
- **build hash** — reject mismatched binaries (non-negotiable for lockstep)
- **asset hash** of `main.pak` + `properties/` — mismatched data desyncs too
- difficulty (host-authoritative)
- game mode / level selection
- **initial RNG seed** — host picks, client adopts; feeds `gMTRand` and pairs with
  the existing per-level `Board::GetLevelRandSeed()` (`Lawn/Board.h:447`)
- which plants the host has unlocked (drives the seed chooser)

### Semantic commands, not raw mouse events

Raw mouse replay (what the demo system does) is wrong for co-op: a remote click
dispatched through the local widget tree would hit the *local* player's cursor and
seed selection. So inputs are translated to intent locally, then exchanged:

```c++
struct CoopCommand {
    uint32_t tick;      // tick this executes on (sampled tick + input delay)
    uint8_t  player;    // 0 = host, 1 = client
    uint8_t  type;      // None, MouseMove, SelectSeed, PlantAt, Shovel,
                        // CollectItem, CobCannonFire, ...
    int16_t  x, y;      // board coords
    int16_t  arg;       // seed index / item id / ...
};
```

Both machines apply **both players' commands** at tick `T`, through a per-player
cursor context. Identical simulation, independent cursors.

### Tick scheduling

Input sampled at tick `T` executes at `T + D`. A tick advances only once both
players' commands for it are present; empty commands are sent every tick so the
peer can always advance.

- `D = ceil((RTT/2 + jitter) / 10 ms) + 1`
- LAN: `D = 3` (30 ms). Internet at 60 ms RTT: `D = 5-6` (~60 ms).
- PvZ is not a twitch game. 50 ms of planting delay is imperceptible.
- On missing peer input: stall logic, keep rendering interpolated frames, show
  "waiting for player" after ~500 ms, and raise `D` if stalls recur.

---

## Shared vs. per-player state

"Everything is shared" is mostly already true, because there is exactly one
`Board`:

**Already shared — zero work.** `mSunMoney` is a single `int32_t`
(`Lawn/Board.h:176`). Zombies, plants, projectiles, grid items, wave state,
`mMainCounter`, lawnmowers — all single instances on the one `Board`.

**Needs splitting into `[2]` — the main surgery.** `mCursorObject` is a single
`std::unique_ptr<CursorObject>` (`Lawn/Board.h:137`); likewise `mCursorPreview`,
`mIgnoreMouseUp` (`:143`), and the `SeedBank`'s selected packet. Each becomes
per-player, indexed by `CoopCommand::player`. The remote player's cursor and seed
highlight render as a ghost so you can see what your friend is about to do.

**Input entry points to route.** `Board::MouseDown/MouseMove/MouseUp`
(`Lawn/Board.h:281-284`) are the funnel; the variants behind them —
`MouseDownWithPlant`, `MouseDownWithTool`, `MouseDownCobcannonFire`
(`:324-325,419`) — are where commands get applied per player.

### The one design decision we need from you

**Seed packet cooldowns: shared or per-player?**

- *Shared* is the literal reading of "everything is shared": one Peashooter
  cooldown between you. Truest to your description, but the two of you contend
  for the same packets and the second player often has nothing to do.
- *Per-player* gives each of you your own cooldowns on a shared seed selection
  and a shared sun bank. Less literal, noticeably better to actually play.

Default is **shared**, since that is what you asked for, behind a one-line config
flag so we can try both. Worth playing once each way.

---

## Difficulty

Host sets Normal or Hard at handshake; it is part of the simulation, so it must
match on both sides and cannot change mid-session.

**Sun ×0.5.** `Board::AddSunMoney(int theAmount)` (`Lawn/Board.h:298`) is a single
chokepoint with only **4 call sites** across `Lawn/`. Scaling there covers
sunflowers, falling sun, Sun-shroom, Twin Sunflower — everything, uniformly, in
about five lines. Integer math (`theAmount / 2`) keeps it deterministic. Level
starting sun and the scripted tutorial grants need a look so they do not become
unwinnable.

**Zombie toughness ×1.5.** `Zombie` health is plain `int32_t`: `mBodyHealth`,
`mBodyMaxHealth`, `mHelmHealth`, `mShieldHealth`, `mFlyingHealth`
(`Lawn/Zombie.h:128-135`). Scale at spawn with integer math (`hp * 3 / 2`).

> **Ambiguity flagged:** "zombies will take 1.5 times as much damage" can mean
> they *need* 1.5× damage to die (tankier → harder) or that they *receive* 1.5×
> damage (squishier → easier). For a hard mode we implement the first: 1.5× HP.
> Flipping it is a one-line change if you meant the other.
