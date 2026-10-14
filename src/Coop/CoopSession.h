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

#ifndef __COOPSESSION_H__
#define __COOPSESSION_H__

#include "CoopProtocol.h"
#include "CoopSocket.h"

#include <deque>
#include <utility>
#include <string>
#include <vector>

namespace Coop
{

// Content fingerprints. Lockstep only holds if both machines run the same code
// over the same data, so a mismatch is refused at handshake instead of
// producing a game that quietly drifts apart.
uint64_t HashOwnExecutable();
uint64_t HashGameAssets();

// Owns the connection and the handshake. One instance lives on LawnApp.
// Update() is pumped from the game loop and never blocks.
class Session
{
public:
	enum class State
	{
		Idle,          // not in co-op
		Hosting,       // listening, nobody yet
		Connecting,    // dialling the host
		Handshaking,   // connected, agreeing on parameters
		Connected,     // agreed; simulation may run
		Failed,        // see GetStatusText()
		Disconnected,  // clean end
	};

private:
	State         mState = State::Idle;
	bool          mIsHost = false;
	Socket        mListener;
	Socket        mPeer;
	SessionParams mParams;
	std::string   mStatusText;

	// Framing. TCP is a byte stream, so frames are length-prefixed and
	// reassembled here; a frame can arrive split or glued to the next one.
	std::vector<char>   mInbound;
	std::vector<char>   mOutbound;
	std::deque<CommandBatch> mPendingBatches;
	std::deque<std::pair<uint32_t, uint32_t>> mPendingChecksums;  // tick, checksum

	uint64_t      mLocalBuildHash = 0;
	uint64_t      mLocalAssetHash = 0;
	bool          mSentHello = false;

	void SetFailed(const std::string& theReason);
	bool SendFrame(const void* theData, uint32_t theLen);
	bool FlushOutbound();
	bool PumpInbound();
	bool ProcessFrames();
	bool HandleFrame(const char* theData, uint32_t theLen);

	bool SendHello();
	bool SendWelcome();
	bool SendReject(RejectReason theReason);
	bool SendReady();

public:
	Session();
	~Session();

	// theStartLevel is the first campaign level; tutorial levels are skipped.
	bool StartHost(uint16_t thePort, Difficulty theDifficulty, uint32_t theStartLevel);
	bool StartJoin(const std::string& theHost, uint16_t thePort);

	// Pump from the game loop. Drives accept, connect, handshake and receive.
	void Update();

	void Disconnect(const std::string& theReason);

	State       GetState() const { return mState; }
	bool        IsActive() const { return mState != State::Idle; }
	bool        IsConnected() const { return mState == State::Connected; }
	bool        IsHost() const { return mIsHost; }
	// Which seed bank and cursor belong to this machine's player.
	PlayerId    GetLocalPlayer() const { return mIsHost ? PlayerId::Host : PlayerId::Client; }
	PlayerId    GetRemotePlayer() const { return mIsHost ? PlayerId::Client : PlayerId::Host; }
	const SessionParams& GetParams() const { return mParams; }
	const std::string&   GetStatusText() const { return mStatusText; }

	// Per-tick input exchange. Sent every tick, empty or not (see CommandBatch).
	bool SendCommandBatch(uint32_t theTick, const std::vector<Command>& theCommands);
	bool PopCommandBatch(CommandBatch& theBatchOut);

	// Simulation checksums, compared to catch a desync early.
	bool SendChecksum(uint32_t theTick, uint32_t theChecksum);
	bool PopChecksum(uint32_t& theTickOut, uint32_t& theChecksumOut);
};

// Parsed from the command line, consumed when the title screen comes up so
// testing a connection needs no clicking.
struct LaunchOptions
{
	bool        mWantHost = false;
	bool        mWantJoin = false;
	std::string mJoinHost;
	uint16_t    mPort = DEFAULT_PORT;
	Difficulty  mDifficulty = Difficulty::Normal;
	bool        mSelfTest = false;
};

LaunchOptions& GetLaunchOptions();
// Consumes argv. Returns false if the arguments were malformed.
bool ParseLaunchOptions(int argc, char** argv, std::string& theErrorOut);
// Loopback host+client handshake, no game assets needed. Returns 0 on success.
int RunSelfTest();

} // namespace Coop

#endif
