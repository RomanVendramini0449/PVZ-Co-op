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
2. Host picks difficulty (Normal / Hard / Wild / Horde / Nightmare, see §8) and
   a port, then waits.
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
- **Money / coins** — one pool
- **Store upgrades** — shared; bought once, both benefit
- All zombies, plants, projectiles, grid items, lawnmowers, wave state
- The chosen seed line-up (which seeds are in the bank)

### Per-player (needs splitting)
- **Sun** — **not shared.** Each player has their own pool and their own
  suns. Every sun the game makes is made once per player: a sunflower (whoever
  planted it) pops one sun for each of you, the sky drops one for each of you,
  each somewhere different. Only a sun's owner can collect it. The other
  player's suns are drawn greyed out and see-through, and vanish when they pick
  them up, so you can watch what they are doing without being able to take
  it. Their total shows under the Menu button ("Partner's sun" / "Host's
  sun"). Everyone starts a level with that level's starting sun.
  - `Board::mSunMoney` is always the *active* player's sun --
    `SetActivePlayer()` parks it in `mCoopSunMoney[]` and takes out the next
    player's, exactly as `mSeedBank` swings -- so every existing call site
    spends the sun of whoever is acting. Anything that must not depend on
    which player is active (the checksum, the partner display) asks
    `GetPlayerSunMoney(player)`.
  - Copies are made in `Board::AddCoin` (every sun's way onto the board);
    `Coin::mCoopOwner` says whose it is and `Coin::ScoreCoin` pays the owner,
    not the active player (which is always player 0 in the simulation).
  - Zombiquarium, Beghouled (both), the slot machine, I, Zombie and Last
    Stand keep **one** pool: there sun is a score, a puzzle's purse or a
    per-round budget rather than income (`Board::CoopModeSplitsSun`).
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

### Names and colours

Players are named, not "the host" and "the other player": every message,
the window title, the partner's sun counter and the disconnect notice use the
name each player typed for their profile ("Roman (host)", "Luca"), falling
back to "Host" / "Player 2" until it is known. It is the host's house, so
the house sign reads "Roman's House" on both screens, and Crazy Dave greets
the host.

Each player picks a colour on the main menu (a row of swatches under Delete
All Saves; saved in the settings as `CoopColor`). It is their cursor on the
other player's screen, their name on the welcome sign, and -- for the host
-- the house sign. Names and colours travel in a `PlayerInfo` message, sent
once connected and again whenever either changes.

The remote cursor itself is one anti-aliased arrow (`assets/coop/cursor_*.png`:
a white fill tinted to the player's colour inside a dark outline). It used to
be stamped from two polygons whose outlines overlapped, which read as two
pointers on top of each other.

---

## 8. Difficulty

Chosen by the host -- in the launcher, or with the main menu's difficulty
button between levels -- and identical on both machines (it is part of the
simulation). It works in single player too ("Play alone" in the launcher, or
`--difficulty` with no `--host`/`--join`). The table lives in `Coop::GetDifficultyRules()`.

| | Sun from every source | Zombie toughness | Zombies |
|---|---|---|---|
| **Normal** | ×1 | ×1 | ×1 |
| **Hard** | **×3/5** (a 25-sun is worth 15) | **×1.2** | ×1 |
| **Wild** | ×1 | **1× to 2×**, rolled per zombie | ×1 |
| **Horde** | ×1 | **1× to 2×**, rolled per zombie | **×1.5** |
| **Nightmare** | **×3/5** | **1.2× to 2×**, rolled per zombie | **×1.5** |
| **Inferno** | **×3/5** | **1.5× to 2.5×**, rolled per zombie | **×2** |
| **Apocalypse** | **×2/5** (a 25-sun is worth 10) | **2× to 3×**, rolled per zombie | **×2.5** |

Inferno also makes every seed packet recharge 25% slower, Apocalypse 50%
(`DifficultyRules::mRechargePercent`, applied when a packet is used).

- **Sun ×3/5 is universal** — sunflowers, sky sun, Sun-shroom, Twin Sunflower,
  everything: a regular sun is worth 15, a small one 9, a large one 30.
  Implemented in `Coin::GetSunValue()`, where a sun's value is decided, so a
  sun is counted the same while it flies to the bank as when it lands. (It was
  first cut on arrival, in `Board::AddSunMoney()`, while sun in flight was
  counted at 25: packets lit up as affordable, then went dark when the sun
  landed short.) 3/5 divides every sun value exactly; 1/2 lost half a point
  to rounding on every 25-sun.
- **Toughness** is health: applied at spawn to `mBodyHealth`, `mHelmHealth`,
  `mShieldHealth`, `mFlyingHealth` before the maximums are taken. The rolled
  modes draw each zombie's percentage from the gameplay generator, so both
  machines roll the same. Bosses take the low end instead of a roll.
- **More zombies** (×1.5 and up): after each wave is picked, the extra are added,
  each a copy of one already in the wave (never a second flag or boss). The
  fraction carries to the next wave, so even one-zombie waves grow.
- Everything is **integer rational math** (`x * 3 / 5`, `x * percent / 100`). No
  floats — a float multiplier here could desync the two machines.
- A level's starting sun is left alone, so the sun cut cannot make a level
  unwinnable before it begins.

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
  9990, 30 seconds to set up, then two waves: two Gargantuars lined up in
  every lane, then three -- 25 in all. The second comes 10 seconds after the
  first is entirely gone (2 minutes at most), so it meets what the first left
  standing, lawn mowers included. Every win pays **$10,000**; the first also
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

**Luca vs. Roman** (`GAMEMODE_COOP_ROMAN`), unlocked along with Luca: a boss
fight on the day lawn. The conveyor brings Luca (who makes no sun here),
Jalapenos and Ice-shrooms, one belt per player. Roman fills a 3x3 block over the
last three columns, where nothing can be planted; every few seconds he slides to
a lane, rolls a fireball or an iceball down it -- an Ice-shroom puts out fire, a
Jalapeno in the lane melts ice -- and slides back to the middle, and now and
then he calls up a couple of zombies. Plants only reach him from the three
lanes he covers. He is Dr. Zomboss's zombie type underneath (health bar and
balls included) with 60,000 health. Every win pays **$5,000**. His art is
`assets/coop/roman.png`.

**Yeti Spray** ($3,000 in Crazy Dave's shop) turns level 4-10 into a Yeti
stampede, but only once bought: 50 Yetis on top of the level's own zombies,
20 of them in the very first wave and the rest split between the flag waves
(a full wave passes its share on to the next). Every loss on 4-10 cuts the
count by a fifth for the next try; beating 4-10 resets it. Yetis still run off
after a while if nobody stops them.

**Adventure Plus** (`GAMEMODE_COOP_ADVENTURE_PLUS`), a row of eleven levels
under the bonus levels, open from the start. Each is a 20-wave level on the
day lawn's layout, drawn with `assets/coop/adventure_plus_background.png`
when that file exists (1400x600, the day lawn's frame -- see
`art-templates/adventure-plus/README.txt`) and the day lawn when it does not.
Each level has a name, its own zombie mix and a zombie or two it sends in
numbers (`gAdventurePlusLevels` in Board.cpp): 1 You're On Your Way to Dust
Bowl (buckets), 2 Yummy, Yummy Brains (footballs, buckets), 3 Up and Under
(ladders, diggers), 4 Yeti Season (Yetis; doubled with Yeti Spray), 5 Team
Fortress Bowling (Wall-nut Bowling; footballs always take three balls),
6 Heavy Weapons Guy (Gargantuars, ladders, diggers), 7 Red Spy in the Base,
8 The Robots Are Coming (Zambonis), 9 Tunnel Vision (diggers, pogos),
10 Everything, Everywhere, and 11 Legendary Demoman (below). Budgets are
bigger than the campaign's, and zombies have a quarter more health.

Zombie budgets follow the campaign's replay formula (2/5 of a point a wave)
plus a small per-level bonus and the zombies each level sends on purpose:
about one and a half to two and a half times a replayed campaign level, and
zombies have a tenth more health. Bowling follows the campaign's own bowling
budget, stretched over 20 waves; its footballs come only on purpose (six in
all, three balls each). The arithmetic is in
`tools/zombie-art/wave_model.py`.

Two enemies of its own, spawned as `ZOMBIE_COOP_TANK` / `ZOMBIE_COOP_HEAVY`
and turned into the zombie each is built on, with `Zombie::mCoopKind` saying
what it really is (so they keep that zombie's rules), drawn from packed
animations instead of its reanimation:
- **Tank** (on the Zomboni): drives and flattens plants like one but leaves no
  ice, has 1,800 health, and blows up when beaten. If it reaches the end of its
  lane it blows itself up instead of getting in: the lane's lawn mower goes, and
  every plant within one square of it.
- **Heavy Weapons Guy** (on the Football Zombie, dancing): as fast, no helmet,
  1,300 health, eats the same, drawn a fifth bigger. He has no death animation
  of his own: he topples over backwards, bounces once and fades.
The Heavy turns up from level 2 (a few) and in 6, 7, 9 and 10; the tank in 8
and 10.

Two plants are earned here, kept as add-ons beside the seed chooser's grid
like Luca: **GRS** (beating level 5; a fume attack through the lane, green
bubbles) and **Noah** (Crazy Dave hands him over at the start of level 8;
the Starfruit's five directions, firing goblin heads).

**Level 11, Legendary Demoman** (`CoopDemoman.cpp`): a boss fight at night in
the rain, with thunder. Dave's talk ends with a Bungee Zombie taking him, his
name replaces Ready-Set-Plant, and his own music plays. The conveyor brings
Luca (no sun here), Noah, GRS, Jalapenos and Ice-shrooms. He stands in a 3x3
block on the right and flattens anything planted under him; everywhere else
can be planted, his other lanes' columns included. He has 60,000 health
(tuned so the belt's plant every 4 s outpaces what he destroys), rests 6.5-9.5 s
between attacks (4-6.5 s below half health, when he also hops far more and
throws a doom grenade every 15-20 s or so), and every attack is marked on the
lawn first:
- bombs: 3 (later 4-5), most at random squares, one in four at a plant;
- a hop onto a marked 3x3, which he flattens, then back;
- a charge down a marked lane, off the left edge and back in from the right;
- below half health, a doom grenade that wipes the marked 5x5 after a fuse.
An Ice-shroom freezes him in place (an attack he is winding up waits) and
freezes his bombs, which burst harmlessly. A light Heavy Weapons Guy (900)
comes every 18-26 s; at half health he calls in two 2,500-health tanks and
waits for them. He topples over and blows up. Every win pays **$20,000**. He is drawn among
the zombies at the depth of the top row of his block, so whatever walks through
his lanes is drawn over him; his bombs and the marks go over everything. Below
half health the belt brings twice the Ice-shrooms, to answer the doom
grenades. In his level the rain plays at 35% and his music 3 dB over the final
boss's tune.

Losing in Adventure Plus has no house to walk into: the left side of the lawn
blows up, lane by lane and then all at once, and the game is over.

Each level can open with a talk in Crazy Dave's style
(`LawnApp::SetupAdventurePlusDialogue`, lines written into the string table
as `CRAZY_DAVE_4000 + 100 * level + n`; level 1 has one, and a line can depend
on the game, such as whether you own Luca). Skipped straight after a loss.
`assets/coop/adventure_plus_narrator.png`, when it exists, stands in for Dave
in these talks: his animation still runs unseen and the picture follows it in
and out, bobbing as it talks.

Zombie parts can be repainted for Adventure Plus only: PNGs in
`assets/coop/zombie-parts/`, named like the part they replace, are drawn in
its place by `Reanimation::DrawTrack` (via `CoopArt::SubstituteZombiePart`),
so the game's own animations move them. See `art-templates/zombies/README.txt`. A win drops a trophy, goes back to the
menu and marks the level gold on Level Select
(`mPurchases[STORE_ITEM_ADVENTURE_PLUS_BEATEN]`, one bit per level). Which
level is played travels in `LevelSetup::mPlusLevel`.

**Crazy Dave's shop** has a fifth page for the mod's own wares, always open:
Yeti Spray, and two skins at $2,000 each -- **Kernel** (drawn in place of the
Squash) and **Philippe** (in place of the Jalapeno). A skin is only a look:
the plant underneath plays exactly as before, its reanimation running unseen,
and the art follows what it is doing (Kernel crouches, leaps and lands flat;
Philippe swells as his fuse burns). Seed packets and the plant in hand wear
it too.

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
