/*
 * Copyright (C) 2026 PVZ-Co-op contributors
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PVZ-Co-op, a two-player co-op mod of PvZ-Portable.
 *
 * PVZ-Co-op is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PVZ-Co-op is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PVZ-Co-op. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __COOPPROTOCOL_H__
#define __COOPPROTOCOL_H__

#include <cstdint>
#include <vector>

namespace Coop
{

// Bumped on any wire-format change. Peers with different versions are rejected
// at handshake rather than allowed to desync later.
constexpr uint32_t PROTOCOL_VERSION = 7;

constexpr uint16_t DEFAULT_PORT = 1337;

// A frame larger than this is treated as a protocol violation rather than
// allocated, so a hostile or corrupt peer cannot exhaust memory.
constexpr uint32_t MAX_FRAME_BYTES = 64 * 1024;

// Ticks of input delay. Input sampled on tick T executes on tick T + delay on
// BOTH machines. See docs/ARCHITECTURE.md. 3 ticks = 30 ms, sized for LAN;
// Phase 2 raises it adaptively when the link stalls.
constexpr uint32_t DEFAULT_INPUT_DELAY = 3;

// How often the peers compare simulation checksums. At 100 Hz this is 4 times
// a second: frequent enough to catch a desync before it is visible, cheap
// enough to be free.
constexpr uint32_t CHECKSUM_INTERVAL_TICKS = 25;

enum class MsgType : uint8_t
{
	Hello = 1,        // client -> host: version and content hashes
	Welcome = 2,      // host -> client: accepted, here are the session parameters
	Reject = 3,       // host -> client: refused, with a reason
	Ready = 4,        // client -> host: parameters applied, ready to simulate
	Commands = 5,     // both ways: one tick's input, even when empty
	Checksum = 6,     // both ways: simulation checksum for desync detection
	Bye = 7,          // either way: clean disconnect
	EnterLevel = 8,   // host -> client: build this level, with the host's profile
	LeaveLevel = 9,   // host -> client: level over, follow me out
	HostScreen = 10,  // host -> client: the screen the host is on, outside a level
	PeerReady = 11,   // client -> host: past the title screen, ready to follow
};

// Where the host is when it is not in a level. The client shows the same
// screen, view-only: outside a level the host drives and the client follows
// (SPEC.md section 3), so the joining player sees the award, the store and the
// credits rather than an unexplained menu.
enum class HostScreen : uint8_t
{
	None = 0,     // in a level, or somewhere with nothing to mirror
	Menu = 1,     // main menu (the game selector)
	Award = 2,    // arg = AwardType
	Store = 3,    // arg = store page
	Credits = 4,
};

enum class RejectReason : uint8_t
{
	None = 0,
	ProtocolMismatch = 1,  // different mod version
	BuildMismatch = 2,     // different binary; lockstep cannot be trusted
	AssetMismatch = 3,     // different main.pak / properties
	AlreadyConnected = 4,  // this build is strictly two players
};

const char* GetRejectReasonText(RejectReason theReason);

enum class Difficulty : uint8_t
{
	Normal = 0,
	Hard = 1,  // sun x3/5 (25 -> 15), zombie health x1.5 (SPEC.md section 8)
};

// Player index. Doubles as the index into Board::mSeedBank[2], so the host is
// always seed bank 0 and the joining player always seed bank 1.
enum class PlayerId : uint8_t
{
	Host = 0,
	Client = 1,
};

constexpr int PLAYER_COUNT = 2;

// What one player did on one tick. Deliberately small and fixed-size: this is
// the only gameplay data that crosses the wire, so the whole of co-op rides on
// these being applied identically on both machines.
//
// These are raw pointer events rather than semantic actions ("plant a
// peashooter at 3,4"), which is worth explaining because the semantic version
// is the obvious design.
//
// Board already turns a click into the right action, and that logic reads the
// active player's cursor and seed bank through Board::mCursorObject and
// Board::mSeedBank. Since those are now per-player views that
// SetActivePlayer() swings, replaying a raw click with the sender's player
// active runs that same logic against the right player's state. So a single
// code path covers planting, the shovel, collecting sun, Cob Cannon fire,
// conveyor levels and everything else, with no duplicated hit-testing to keep
// in sync with upstream -- and nothing to forget when a new interaction is
// added.
enum class CommandType : uint8_t
{
	None = 0,       // nothing happened this tick; still sent, to advance the peer
	MouseMove = 1,  // pointer moved: drives this player's cursor
	MouseDown = 2,  // arg = click count (negative is a right click, as upstream)
	MouseUp = 3,    // arg = click count
	EnterMenu = 4,  // left the board; hide this player's cursor
	LeaveMenu = 5,  // back on the board; show it again
	// The seed chooser is shared (SPEC.md section 4), so it rides the same
	// scheduler as the board: either player's pick is applied on an agreed
	// tick on both machines, which keeps the one chosen line-up identical.
	ChooserPick = 6,  // clicked a seed packet on the chooser
	StartPlaying = 7, // host pressed "Let's Rock!"; arg = chosen-seed digest
	Key = 8,          // a key that acts on the game: skip the intro, advance
	                  // Crazy Dave, let go of what you hold. arg = KeyCode
	DebugWin = 9,     // test script only: win the level, on both machines
	DebugLose = 10,   // test script only: lose it -- "the zombies ate your brains"
};

struct Command
{
	uint32_t    mTick = 0;    // the tick this executes on, on both machines
	PlayerId    mPlayer = PlayerId::Host;
	CommandType mType = CommandType::None;
	int16_t     mX = 0;
	int16_t     mY = 0;
	int16_t     mArg = 0;     // packet index / item id / unused
};

// One tick's input from one player. Sent EVERY tick, empty or not: an empty
// batch is what tells the peer "tick N is settled, you may advance". Without
// it, a player sitting still would stall the other's simulation forever.
struct CommandBatch
{
	// Which level this belongs to. Tick numbers restart at 0 every level, so
	// without this a batch still in flight from the level just left is
	// indistinguishable from the new level's batch for the same tick -- and
	// gets applied to it, or worse, blocks waiting on a tick that will never
	// be sent again.
	uint32_t             mEpoch = 0;
	uint32_t             mTick = 0;
	std::vector<Command> mCommands;
};

// How many store items a profile carries. Must match PlayerInfo::mPurchases.
constexpr int PURCHASE_COUNT = 80;

// What the client needs in order to build the same level as the host.
//
// The second half of this is the host's profile, and it is worth saying why it
// travels. The simulation reads the local profile in a lot of places that look
// innocent -- how many seed slots the bank has, which seeds exist at all,
// whether the roof gets lawn mowers, how much sun a level opens with. Two
// players are never at the same point in their own campaigns, so those reads
// would answer differently on each machine and the two lawns would not even
// start the same shape. The client therefore borrows the host's profile for
// the session (SPEC.md section 10.4) and never writes it to disk.
struct LevelSetup
{
	uint32_t mEpoch = 0;        // bumped by the host for every level entered
	uint32_t mLevel = 1;        // campaign level being started
	int32_t  mGameMode = 0;     // GameMode enum
	uint32_t mRngSeed = 0;      // reseeds the global MTRand on both sides
	// Part of the simulation (sun and zombie health), so it travels with
	// every level rather than only at handshake: the host can change it from
	// the main menu between levels.
	Difficulty mDifficulty = Difficulty::Normal;
	// How the host's previous level ended (BoardResult). A level retried
	// after a loss skips its intro -- Crazy Dave's talk included -- so both
	// machines must know it was a retry, or one sits in the intro while the
	// other plays.
	int32_t  mPrevBoardResult = 0;

	int32_t  mProfileLevel = 1;
	int32_t  mCoins = 0;
	uint32_t mFinishedAdventure = 0;
	uint32_t mPurchases[PURCHASE_COUNT] = {};
};

// One step of the host-driven campaign flow, as the client receives it. These
// arrive as separate messages but must be applied strictly in order: "the host
// went back to the menu" followed by "the host entered a level" in a single
// network read is a level to build, and applying them backwards tears it down.
struct FlowEvent
{
	enum class Kind : uint8_t { EnterLevel, LeaveLevel, Screen };

	Kind       mKind = Kind::Screen;
	LevelSetup mSetup;                      // the level, or the host's profile
	HostScreen mScreen = HostScreen::None;  // Kind::Screen only
	int32_t    mArg = 0;                    // Kind::Screen only
};

// Everything that must match on both machines before tick 0. Mismatch here is
// the difference between a shared game and two diverging ones.
struct SessionParams
{
	Difficulty mDifficulty = Difficulty::Normal;
	uint32_t   mStartLevel = 1;   // campaign level; tutorial levels are skipped
	uint32_t   mRngSeed = 0;      // seeds the single global MTRand on both sides
	uint32_t   mInputDelay = DEFAULT_INPUT_DELAY;
	uint32_t   mUnlockFlags = 0;  // host's progression; drives the seed chooser
};

} // namespace Coop

#endif
