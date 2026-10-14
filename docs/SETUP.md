# Setup: building and running the mod

**Short answer to "do I pull it into my PvZ folder?" — no.**

This mod is a **separate program** that *reads* your Plants vs. Zombies data
files. You clone it somewhere of its own (your Documents folder, wherever), build
it, and then point it at your existing PvZ install. Nothing is copied into, or
changed in, your PvZ folder. Your Steam copy stays exactly as it is.

Think of it like a media player and a video file: the mod is the player, your
PvZ install is the file it opens.

---

## What you need

- Your legally purchased **PvZ: Game of the Year Edition** (Steam is fine).
  We only read `main.pak` and `properties/` out of it.
- A compiler toolchain (below).
- **Both players need the same build.** The handshake fingerprints the
  executable and refuses to connect if they differ — that is deliberate, because
  mismatched builds would silently desync the game. Easiest path: one of you
  builds it and sends the other the resulting `.exe`.

---

## 1. Get the code

```sh
git clone https://github.com/RomanVendramini0449/PVZ-Co-op.git
cd PVZ-Co-op
git checkout claude/fervent-bohr-3njwft
```

To pull down later changes:

```sh
git pull origin claude/fervent-bohr-3njwft
```

---

## 2. Install the toolchain

### Windows (MSYS2)

Install [MSYS2](https://www.msys2.org/), then open the **UCRT64** terminal
(not the plain MSYS one — this matters) and run:

```sh
pacman -S --needed base-devel mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-libopenmpt \
  mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
```

### Linux (Debian/Ubuntu)

```sh
sudo apt install cmake ninja-build libjpeg-dev libopenmpt-dev libpng-dev libsdl2-dev
```

### macOS

```sh
brew install cmake dylibbundler jpeg-turbo libopenmpt libpng ninja sdl2
```

---

## 3. Build

```sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

You get `build/pvz-portable` (`build/pvz-portable.exe` on Windows).

### Check the netcode without the game

This needs no game files at all and is the fastest way to confirm your build is
sane:

```sh
./build/pvz-portable --coop-selftest
```

Expected:

```
coop selftest: handshake OK
coop selftest: params agree (seed=... difficulty=... delay=3)
coop selftest: command round-trip OK
coop selftest: PASS
```

---

## 3b. Test the connection between your two machines

**This is the most useful thing you can do right now**, and it still needs no
game files. It runs the real lockstep loop at the real 100 Hz tick rate over
your actual network, so it proves the link works before the game is involved.

On the **host** machine:

```sh
./build/pvz-portable --coop-nettest --host --port 1337
```

On the **other** machine (replace with the host's IP):

```sh
./build/pvz-portable --coop-nettest --join 192.168.1.42:1337
```

Both should print something like:

```
coop nettest: connected. Player connected.
coop nettest: build and game files match on both machines.

coop nettest: ran 1000 ticks in 9990 ms
coop nettest: final state 0xEF658E63 (must match on BOTH machines)
coop nettest: stalled 0 ticks total, slowest tick 0 ms
coop nettest: input delay settled at 3 ticks (30 ms)
coop nettest: link looks good.
coop nettest: PASS
```

**What to check, in order of importance:**

1. **`final state` is the same hex number on both machines.** This is the whole
   ballgame. It means both machines ran 1000 ticks of simulation from the same
   inputs and landed on bit-identical results. If these differ, tell me the two
   numbers.
2. **`stalled`** — 0 on a LAN. A few hundred means the link is jittery.
3. **`input delay settled at`** — starts at 3 ticks and climbs on its own if the
   connection needs it. Whatever it ends at is roughly the input lag you will
   feel when planting.

Send me all of that output from both machines and I will size the defaults to
your actual connection.

---

## 4. Point it at your PvZ install

Find your game folder: in Steam, right-click **Plants vs. Zombies: GOTY
Edition** → **Manage** → **Browse local files**. It is usually something like:

```
C:\Program Files (x86)\Steam\steamapps\common\Plants vs. Zombies GOTY\
```

That folder contains `main.pak` and a `properties\` folder. Those are the two
things we need.

**You have two options. Pick one.**

### Option A — point at it (recommended, touches nothing)

```sh
./build/pvz-portable -resdir "C:\Program Files (x86)\Steam\steamapps\common\Plants vs. Zombies GOTY"
```

Your PvZ folder is read-only as far as we are concerned. Nothing is written
there, nothing is modified, Steam stays happy.

### Option B — copy the data next to the executable

Copy `main.pak` and the whole `properties\` folder into `build/`, next to
`pvz-portable.exe`, then just run it:

```sh
./build/pvz-portable
```

Both work. **A is cleaner** because you can delete the whole mod folder later and
your game is untouched.

> **Saves and settings do not go in either folder.** They live in your OS's app
> data directory (`%APPDATA%\io.github.wszqkzqk\PvZPortable\` on Windows), so
> this mod cannot corrupt your real PvZ save.

---

## 5. Play together

**Host:**

```sh
./build/pvz-portable --host --port 1337 --difficulty hard
```

**Joiner:**

```sh
./build/pvz-portable --join 192.168.1.42:1337
```

Replace `192.168.1.42` with the host's IP. On the same network, the host finds
theirs with `ipconfig` (Windows) or `ip addr` (Linux) — the `192.168.x.x` one.

Over the internet, the host needs to forward TCP port 1337 on their router.
There is no matchmaking server; this is direct IP by design.

`--difficulty hard` is host-only and locks for the whole session: **sun is
halved and zombies have 1.5× health.** The joiner inherits it automatically.

> These flags exist so testing needs no clicking. The **Host Game** / **Join
> Game** menu buttons come with the lobby UI.

---

## When something goes wrong

| What you see | What it means |
|---|---|
| "You are running a different build..." | You and your friend built from different commits. Both rebuild from the same one, or share the one `.exe`. |
| "Your game files differ from the host's..." | Different PvZ versions. Both need GOTY. |
| "Could not host on port 1337" | Something else is using the port, or a firewall is blocking it. Try `--port 1338`. |
| "Could not reach the host" | Wrong IP, firewall, or port not forwarded. Check you can `ping` them first. |
| Game starts but is a black screen / missing art | `-resdir` is pointing at the wrong folder, or `main.pak` is not there. |

---

## What I need you to test, and when

I cannot run the game here — no game files, and one machine. From Phase 3
onward, you are the test loop. I will tell you exactly what to check each time.

**Right now:** do steps 1-3, run `--coop-selftest` on each machine, then run
**step 3b** between them. None of it needs game data, and it tells me what your
real connection looks like. Send me the output.
