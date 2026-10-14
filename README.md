# PVZ-Co-op

A two-player co-op mod of **Plants vs. Zombies: Game of the Year Edition**.
Two people play the entire campaign together on one shared lawn, connected by
direct IP over a LAN or the internet.

Everything on the board is shared: one sun bank, one set of zombies, one set of
plants, one wave timer. Each player gets their own cursor. The host picks a
difficulty that can make sun scarcer and zombies tougher.

**Status:** forked from upstream, Phase 1 in progress.

- [`docs/SPEC.md`](docs/SPEC.md) — what we are building (locked scope)
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — how the netcode works
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — build order and what's needed from you

**Scope:** campaign only. No mini-games mode, no Survival, no Puzzle mode, no
Zen Garden. The campaign's own conveyor-belt levels, Wall-nut Bowling levels and
the Dr. Zomboss finale are included — on those, each player gets their own
conveyor belt running at half rate, so coordinating matters.

---

## Why we are not decompiling the retail game

The original plan was to decompile the Steam build of PvZ and add netcode to the
result. That path is a dead end, for two separate reasons.

**1. You got the wrapper because the Steam build *is* a wrapper.** The Steam
release of PvZ GOTY ships the real game inside a DRM wrapper: the outer PE file
is a loader that decrypts and maps the actual game at runtime, so a static tool
like Ghidra only ever sees the stub. There are ways around that — analyse a
non-Steam retail/Origin build instead, or dump the decrypted image out of memory
once the loader has unpacked it (x64dbg + Scylla, or Steamless) — and that does
get you the real `PlantsVsZombies.exe`.

**2. It would not help much even if it worked.** What comes back is a few
megabytes of optimised 2009 x86 with no symbols, no types, and no structure.
Retrofitting a networked game loop into that means hand-patching assembly around
code you only half understand, with no compiler and no type checker to catch
mistakes. Co-op needs invasive changes — a second cursor, a tick scheduler, a
serialisation layer, a deterministic RNG — touching hundreds of call sites. That
is not a reasonable place to do this work.

**The good news:** other people already did the reverse engineering, and
published the result as readable, buildable, modern C++. We start from that
instead. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#chosen-base) for which
project and why.

---

## Legal and asset position

The upstream project contains **no PopCap/EA assets and no PopCap IP** beyond
PopCap's own open-sourced SexyApp Framework. It is a clean reimplementation that
reads the data files from a copy of the game you own: you supply `main.pak` and
the `properties/` folder from a legally purchased PvZ GOTY install.

This mod follows the same rule. We distribute **source code only**. Nobody can
run it without owning the game. Upstream is LGPL-3.0-or-later, so this fork
stays LGPL-3.0-or-later as well.

---

## Quickstart (once Phase 0 lands)

```sh
# 1. Build (both machines must run the SAME build — see ARCHITECTURE.md)
cmake -G Ninja -B build && cmake --build build

# 2. Supply your own game data
cp /path/to/PlantsVsZombies/main.pak      build/
cp -r /path/to/PlantsVsZombies/properties build/

# 3. Host, then have your friend join by IP
./build/PvZ --host --port 1337 --difficulty hard
./build/PvZ --join 192.168.1.42:1337
```
