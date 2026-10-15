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

## 2. Get a build — the easy way, or the build-it-yourself way

### Easy way: download a prebuilt Windows .exe (no toolchain at all)

Every push to the development branch builds a Windows executable
automatically. You do not need CMake, a compiler, or MSYS2 for this.

1. Go to the repo's **Actions** tab on GitHub.
2. Click the most recent **Co-op build** run with a green tick.
3. Scroll to **Artifacts** and download **`pvz-coop-windows`**.
4. Unzip it. Inside is a single `PvZ-Coop.exe` — it is statically linked,
   so there are no DLLs to copy around.

**This is also the best way to make sure you and your friend are on the exact
same build**, which the co-op handshake requires. Both of you download the same
artifact and you are guaranteed to match.

Then skip to step 3b.

> Note: GitHub requires you to be signed in to download artifacts.

#### Windows will probably warn you about the .exe

Expect **"Windows protected your PC"** (SmartScreen), and possibly a Defender
scan prompt, the first time you run it. This is not a sign anything is wrong:
SmartScreen warns about *any* executable that lacks a paid code-signing
certificate and has no download reputation yet, which describes every freshly
built binary from a small project.

To run it anyway: **More info** -> **Run anyway**.

If you would rather verify than trust: the file was built by GitHub's own
runners from the source in this repository, the build log is public under the
Actions tab, and the artifact page lists a SHA256 digest you can compare
against the file you downloaded.

The same applies if you ever install MSYS2 — its installer is unsigned and
SmartScreen flags it too. But you do not need MSYS2 unless you want to compile
the code yourself.

### Build-it-yourself way

Only needed if you want to change the code yourself. Two options on Windows:

- **MSYS2** — what upstream documents and what the CI uses. The installer is
  unsigned, so SmartScreen tends to flag it.
- **Visual Studio + vcpkg** — Microsoft-signed, so no SmartScreen friction.
  Install the "Desktop development with C++" workload, then open this folder
  in Visual Studio; it reads `CMakeLists.txt` and `vcpkg.json` directly and
  fetches the dependencies itself.

Continue below for MSYS2.

---

## 2b. Install the toolchain

### Windows (MSYS2)

> **Important: this does not work in PowerShell or Command Prompt.** MSYS2
> installs its own separate terminal application, and the build has to happen
> in there. If you see
> `The term 'cmake' is not recognized as the name of a cmdlet`, you are in
> PowerShell and need to switch terminals.

1. Install [MSYS2](https://www.msys2.org/) (accept the default location,
   `C:\msys64`).
2. From the **Start menu**, open **"MSYS2 UCRT64"**. There are several MSYS2
   entries — you want the one that says **UCRT64**, and the prompt should show
   a purple `UCRT64` label. The plain `MSYS` one will not work.
3. Run this (it may ask to close the terminal partway through a first-time
   update; if so, reopen MSYS2 UCRT64 and run it again):

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

**Still inside the MSYS2 UCRT64 terminal.**

Windows drive letters look different in MSYS2: `D:\Documents\PVZ_DECOMP\PVZ-Co-op`
becomes `/d/Documents/PVZ_DECOMP/PVZ-Co-op`. So:

```sh
cd /d/Documents/PVZ_DECOMP/PVZ-Co-op
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Linux or macOS, just `cd` to the folder normally and run the same two
commands.

You get `build/pvz-portable` (`build/PvZ-Coop.exe` on Windows).

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
`PvZ-Coop.exe`, then just run it:

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

### The easy way: the launcher

Double-click **`PVZ-Coop-Launcher.bat`**. A small window opens:

- It finds your Plants vs. Zombies folder by itself: first the folder the
  launcher is in (and the ones above it), so unzipping the mod into your game
  folder just works, then Steam's libraries and the usual install places. If
  it guesses wrong, `Browse` opens the normal Windows file window: pick
  `PlantsVsZombies.exe` (or `main.pak`) and its folder is used. It tells you
  whether it can see `main.pak`.
- Pick **Host a game** or **Join a game**.
- Hosting shows **your own address**, so you can read it straight out to the
  other player, and lets you pick the difficulty: Normal, Hard, Wild, Horde or
  Nightmare (see `docs/SPEC.md` section 8). The host can change it later from
  the main menu, between levels.
- Joining just needs their address typed in. The port is filled in for you.
- Press **Start**. The game launches with the right settings.

Your choices are remembered for next time, in `coop-launcher-settings.txt`
next to the launcher.

The launcher is a plain text script you can open and read. The `.bat` runs it
with `-ExecutionPolicy Bypass`, which applies to that single run and changes
no Windows setting; it is needed only because PowerShell refuses to run script
files by default.

### The manual way: command line

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
| `'cmake' is not recognized...` in PowerShell | The build does not run in PowerShell. Open **MSYS2 UCRT64** from the Start menu, or just download the prebuilt `.exe` from the Actions tab (step 2). |

---

## What I need you to test, and when

I cannot run the game here — no game files, and one machine. From Phase 3
onward, you are the test loop. I will tell you exactly what to check each time.

**Right now:** do steps 1-3, run `--coop-selftest` on each machine, then run
**step 3b** between them. None of it needs game data, and it tells me what your
real connection looks like. Send me the output.
