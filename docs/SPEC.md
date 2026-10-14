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

The opening tutorial is skipped -- see section 10. Every campaign level is
playable, including 1-1; it is the hand-holding that goes, not the levels.

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

### Draw order: on top of everything

The remote cursor is drawn **above all game content and all UI** -- never
behind a plant, a zombie, the seed bank, the progress meter or a falling sun.
It is the one thing on screen that must never be occluded, because its whole
job is telling you where your friend is about to act.

Mechanically this is already a solved problem in the engine. Board draws most
things into a render list sorted by `MakeRenderOrder(layer, gridY, offset)`,
but the **local** cursor is deliberately drawn afterwards, outside that sorted
pass, near the end of `Board::Draw`. The remote cursor goes in the same place.
It does not join the sorted list at all, so nothing can sort above it.

If it ever does need a sort key instead, the layer enum runs up through
`RENDER_LAYER_UI_TOP` (700000), `RENDER_LAYER_ABOVE_UI` (800000) and
`RENDER_LAYER_SCREEN_FADE` (900000), so there is plenty of headroom.

It tracks the other player's pointer continuously, not just on click, via the
`MouseMove` command -- so you can watch them line a shot up, hesitate, and
change their mind, not just see where they landed.

> **One deliberate exception:** your *own* cursor draws above theirs. If the
> two overlap, you should never lose track of your own.

**Latency note, so it is not a surprise:** the remote cursor is as old as the
input delay, around 30 ms on a LAN. It shows where they were a few ticks ago,
not where they are this instant. Drawing it ahead of its commands would mean
it could point somewhere their click never landed, which is worse than being
slightly behind.

---

## 8. Difficulty

Host-set at session creation, locked for the whole session, identical on both
machines (it is part of the simulation).

| | Normal | Hard |
|---|---|---|
| Sun from every source | ×1 | **×3/5** (a 25-sun is worth 15) |
| Zombie strength | ×1 | **×1.5** |

- **Sun ×3/5 is universal** — sunflowers, sky sun, Sun-shroom, Twin Sunflower,
  everything: a regular sun is worth 15, a small one 9, a large one 30.
  Implemented at `Board::AddSunMoney()`, a single chokepoint with 4 call sites.
  It was ×0.5 originally, but 25 / 2 rounds down to 12 in integer math, so
  every sun lost half a point; 3/5 divides every sun value exactly.
- **Zombies ×1.5 "stronger" means tougher**: 1.5× health, so they take 1.5×
  more damage to kill. Applied at spawn to `mBodyHealth`, `mBodyMaxHealth`,
  `mHelmHealth`, `mShieldHealth`, `mFlyingHealth` (all plain `int32_t`).
- Both use **integer rational math** (`x * 3 / 5`, `x * 3 / 2`). No floats — a float
  multiplier here could desync the two machines.
- Level starting sun and any scripted grants get audited so Hard does not make a
  level mathematically unwinnable.

---

## 10. Menu, tutorial, level select and saving

### 10.1 Everything except the campaign is greyed out

`GameSelector` owns a named button per mode, so this is a disable call each.

| Button | State |
|---|---|
| Adventure | **Enabled** -- the only way in |
| Mini-games, Puzzle, Survival | Greyed out |
| Zen Garden, Zombatar, Achievements | Greyed out |
| Store (Dave's Twiddydinks) | **Enabled** -- shared money and upgrades (section 5) |
| Options, Quit, Change User | **Enabled** |

Greyed out rather than hidden, so it is obvious the mod removed them on purpose
rather than the game being broken.

> **Open question:** the Almanac is harmless and read-only. Left enabled for
> now. Say if you want it greyed too.

### 10.2 Tutorial skipped

`TutorialState` is an enum whose zero value is `TUTORIAL_OFF`, with the
hand-holding sequences numbered after it:
`TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER`, `..._PLANT_PEASHOOTER`,
`..._REFRESH_PEASHOOTER`, then the same for the Sunflower in level 2, plus the
shovel and "more sun" sequences.

The mod pins the state to `TUTORIAL_OFF` and never enters those sequences, so
the arrows, the "click here to plant" prompts and the forced pauses never
appear. Level 1-1 is still played -- you just play it instead of being walked
through it.

This matters more in co-op than single player: the tutorial expects exactly one
player to click exactly one thing in a fixed order, which two people cannot
satisfy.

### 10.3 Level select

The host picks any campaign level to start from, 1-1 through 5-10, rather than
only continuing from saved progress.

The handshake already carries `SessionParams::mStartLevel`, so the plumbing is
there and this is a UI on top of it. Host-only, like every other
outside-a-level decision (section 3), and locked once the level begins.

Under the campaign grid sit two **bonus levels**, which are not part of the
campaign and leave the saved progress alone:

- **Gargantuar Onslaught** (`GAMEMODE_COOP_ONSLAUGHT`): a day lawn, sun held at
  9990, 45 seconds to set up, then two waves 30 seconds apart, each three
  Gargantuars lined up in every lane -- 30 in all. Beating it the first time
  awards **Luca** with the usual "you got a new plant" screen.
- **Dev Sandbox** (`GAMEMODE_COOP_SANDBOX`): a pool lawn, every plant in the
  chooser (upgrades and Luca included), sun held at 9990, no recharge, and a
  long stream of mixed zombies to try things against.

**Luca** is a Peashooter underneath -- its reanimation runs unseen, so it aims
and fires exactly like one -- drawn with its own art (`assets/coop`, compiled
into the executable). It fires cheese (30 damage to a pea's 20), makes sun on a
Sunflower's clock with the Sunflower's glow, and costs 300. The unlock lives in
`mPurchases[STORE_ITEM_UNLOCK_LUCA]`, so it saves with the profile and reaches
the joining player along with the host's upgrades.

### 10.3b Delete All Saves

A button in the menu's top-right corner wipes every profile's progress, after a
confirmation, and starts again as the same player. Settings are kept. The
joining player does not get it: they are playing on the host's save.

### 10.4 Saving progress

**Between levels (the important one).** Beating a level saves: which level is
next, money, store upgrades, and unlocked seeds. Quit and come back and the
campaign resumes where you left off. The base game already does this through
`ProfileMgr::SyncState`; co-op rides the **host's** profile, and the joining
player's own profile is never touched.

**Mid-level.** Harder, and it needs saying why. The engine can serialise a live
board (`SaveGame.cpp` writes the whole thing), but in co-op a snapshot has to
cover **both** players' seed banks and cursors, and the save currently stores
only player 0's. Doable, but it is a format change and extra surface for
desyncs, so it comes after the campaign is playable end to end.

Until then a dropped session replays the level it was on, which is cheap
because the level restarts deterministically from its seed.

## 9. Non-goals for this build

Worth stating so they do not creep in: no dedicated servers, no more than two
players, no spectators, no mid-level join, no cross-platform play (both players
run the same build on the same CPU architecture), and no rollback netcode.

Mid-level saving is deferred rather than refused -- see section 10.4.
