# Co-op specification

Locked from the design conversation. This is the definition of done. Anything
not listed here is **out of scope for this build**.

---

## 1. Scope

**In:** Adventure mode (the campaign), two players, start to finish.

**Out, explicitly:** mini-games as a menu mode, Survival, Puzzle mode
(Vasebreaker / I Zombie), Zen Garden, Zombatar, the Almanac as a shared screen.

The campaign's own special levels — the conveyor-belt levels, the Wall-nut
Bowling levels, and the Dr. Zomboss finale — **are in scope**, because they are
part of the campaign. They are not "mini-games" for our purposes. See §6.

The opening tutorial levels are skipped. A new co-op session starts at the first
real level.

---

## 2. Session lifecycle

1. **Main menu** gains a **Host Game** button and a **Join Game** button.
2. Host picks difficulty (Normal / Hard) and a port, then waits.
3. Joiner enters `IP:port` and connects. No lobby server, no matchmaking, no NAT
   traversal — direct IP only.
4. Once connected, both players see an **icon representing the other player** so
   it is obvious someone is attached.
5. Host starts the campaign. Both players proceed together.
6. Disconnect returns both to the menu with a clear reason.

---

## 3. Authority model

The host owns everything that is not gameplay:

| Thing | Who controls it |
|---|---|
| Cutscenes / Crazy Dave dialogue | **Host only.** Host skips, client watches. Client input is swallowed. |
| Level start / transitions | Host |
| Store (Dave's Twiddydinks) navigation | Host |
| Menus, settings, pause | Host drives flow; client sees "waiting for host" |
| **In-level gameplay** | **Both players, equally** |
| Seed picker | **Both players** (see §4) |

The rule of thumb: outside a level, the host drives and the client follows.
Inside a level, both players are equal.

---

## 4. Seed picker

Both players pick from **the same seed selection screen**, into **one shared
seed bank**. Either player can add or remove any seed.

The game does **not** enforce agreement — you sort it out verbally. Host presses
"Let's Rock!" to proceed.

---

## 5. Shared vs. per-player state

### Shared (one copy, already single on `Board` — mostly free)
- **Sun** (`Board::mSunMoney`) — one pool, both spend from it
- **Money / coins** — one pool
- **Store upgrades** — shared; bought once, both benefit
- All zombies, plants, projectiles, grid items, lawnmowers, wave state
- The chosen seed line-up (which seeds are in the bank)

### Per-player (needs splitting)
- **Seed packet cooldowns** — **not shared.** Each player has their own
  cooldown on every packet. If you plant a Peashooter, your friend's Peashooter
  is still ready.
- **Conveyor belts** on conveyor levels — one belt each (§6)
- Cursor / held plant / shovel state
- Seed packet selection highlight

> **Implementation note.** `Board::mSeedBank` is a single
> `std::unique_ptr<SeedBank>`, and that one object owns *both*
> `mSeedPackets[]` (each with `mRefreshCounter` / `mRefreshTime`, i.e. the
> cooldowns) *and* `mConveyorBeltCounter` (the belt). Promoting it to
> `mSeedBank[2]` therefore delivers per-player cooldowns **and** split conveyor
> belts in a single refactor. This is the central change of the whole mod.

---

## 6. Conveyor and special campaign levels

On conveyor-belt levels (including Wall-nut Bowling and the Dr. Zomboss finale):

- **Each player gets their own conveyor belt**, rendered separately.
- **Each belt delivers at half the normal rate**, so the total plant income
  across both players matches a single-player run.

The intent is that you cannot each brute-force your own lane with a full belt —
you have to coordinate who spends what. Teamwork is the point.

---

## 7. Remote cursor

Each player sees the other player's cursor on screen, drawn with an existing
in-game cursor-like sprite.

Visibility rules:
- **Visible** during gameplay on the board, and on the seed picker.
- **Hidden** whenever that player is in a menu, a settings screen, the store, or
  a cutscene.
- It appears and disappears as they move in and out of those screens.

The remote cursor also shows what the other player is currently holding (a seed
packet, the shovel) so you can see what they are about to do.

---

## 8. Difficulty

Host-set at session creation, locked for the whole session, identical on both
machines (it is part of the simulation).

| | Normal | Hard |
|---|---|---|
| Sun from every source | ×1 | **×0.5** |
| Zombie strength | ×1 | **×1.5** |

- **Sun ×0.5 is universal** — sunflowers, sky sun, Sun-shroom, Twin Sunflower,
  everything. Implemented at `Board::AddSunMoney()`, a single chokepoint with
  4 call sites.
- **Zombies ×1.5 "stronger" means tougher**: 1.5× health, so they take 1.5×
  more damage to kill. Applied at spawn to `mBodyHealth`, `mBodyMaxHealth`,
  `mHelmHealth`, `mShieldHealth`, `mFlyingHealth` (all plain `int32_t`).
- Both use **integer rational math** (`x / 2`, `x * 3 / 2`). No floats — a float
  multiplier here could desync the two machines.
- Level starting sun and any scripted grants get audited so Hard does not make a
  level mathematically unwinnable.

---

## 9. Non-goals for this build

Worth stating so they do not creep in: no dedicated servers, no more than two
players, no spectators, no mid-level join, no cross-platform play (both players
run the same build on the same CPU architecture), no save-scumming a co-op
session, no rollback netcode.
