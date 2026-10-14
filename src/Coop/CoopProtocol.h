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

namespace Coop
{

// Bumped on any wire-format change. Peers with different versions are rejected
// at handshake rather than allowed to desync later.
constexpr uint32_t PROTOCOL_VERSION = 1;

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
	Commands = 5,     // both ways: per-tick input (Phase 2)
	Checksum = 6,     // both ways: simulation checksum for desync detection
	Bye = 7,          // either way: clean disconnect
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
	Hard = 1,  // sun x0.5, zombie health x1.5 (SPEC.md section 8)
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
enum class CommandType : uint8_t
{
	None = 0,          // nothing happened this tick; still sent, to advance the peer
	MouseMove = 1,     // cursor position, for drawing the remote player's cursor
	SelectSeed = 2,    // pick up a seed packet (arg = packet index)
	PlantAt = 3,       // place the held plant at (x, y)
	Shovel = 4,        // dig up whatever is at (x, y)
	CollectItem = 5,   // click a sun or coin (arg = item id)
	CobCannonFire = 6, // aim a primed Cob Cannon at (x, y)
	CancelHeld = 7,    // right-click / put the held plant back
	EnterMenu = 8,     // left the board; hide this player's remote cursor
	LeaveMenu = 9,     // back on the board; show it again
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
