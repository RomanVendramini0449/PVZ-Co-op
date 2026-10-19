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
	// Why a session that was connected stopped being so, for telling the
	// player in their own terms rather than the socket's.
	enum class EndReason
	{
		None,
		PeerLeft,   // the other player quit or left
		LocalLeft,  // this player left
		Lost,       // the link dropped
	};

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
	EndReason     mEndReason = EndReason::None;
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
	struct PendingChecksum { uint32_t mEpoch; uint32_t mTick; uint32_t mValue; };
	std::deque<PendingChecksum> mPendingChecksums;
	std::deque<uint32_t>        mPendingDesyncs;
	// Host-driven campaign flow (SPEC.md section 3). Queued rather than
	// applied on arrival: the client acts on these from the game loop, where
	// building a board is safe.
	std::deque<FlowEvent>    mPendingFlow;
	// Host: whether the client has finished loading and clicked through its
	// title screen. Until then it is not on any screen that can follow us.
	bool                     mPeerReady = false;
	bool                     mSentReady = false;
	// The other player's name and colour (0x00RRGGBB), from their PlayerInfo.
	std::string              mRemoteName;
	uint32_t                 mRemoteColor = 0;
	bool                     mHasRemoteInfo = false;

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
	EndReason   GetEndReason() const { return mEndReason; }
	bool        IsActive() const { return mState != State::Idle; }
	bool        IsConnected() const { return mState == State::Connected; }
	bool        IsHost() const { return mIsHost; }
	// Which seed bank and cursor belong to this machine's player.
	PlayerId    GetLocalPlayer() const { return mIsHost ? PlayerId::Host : PlayerId::Client; }
	PlayerId    GetRemotePlayer() const { return mIsHost ? PlayerId::Client : PlayerId::Host; }
	const SessionParams& GetParams() const { return mParams; }
	const std::string&   GetStatusText() const { return mStatusText; }

	// Per-tick input exchange. Sent every tick, empty or not (see CommandBatch).
	//
	// Pop only ever returns items for the epoch asked for. Older ones are
	// discarded on the way -- they belong to a level both machines have left.
	// Newer ones stay queued, because the peer can enter the next level a
	// moment before we do and its first batches must survive until we arrive.
	bool SendCommandBatch(uint32_t theTick, const std::vector<Command>& theCommands, uint32_t theEpoch = 0, uint8_t theSenderHeadroom = 0);
	bool PopCommandBatch(CommandBatch& theBatchOut, uint32_t theEpoch = 0);

	// Simulation checksums, compared to catch a desync early.
	bool SendChecksum(uint32_t theTick, uint32_t theChecksum, uint32_t theEpoch = 0);
	bool PopChecksum(uint32_t& theTickOut, uint32_t& theChecksumOut, uint32_t theEpoch = 0);

	// Tell the peer we have detected a desync, and collect theirs.
	bool SendDesync(uint32_t theTick);
	bool PopDesync(uint32_t& theTickOut);

	// Host-driven campaign flow. Only the host sends these; only the client
	// has anything to pop.
	bool SendEnterLevel(const LevelSetup& theSetup);
	bool SendLeaveLevel();
	// theProfile carries the host's campaign state, so a mirrored award or
	// store shows the right plant, coins and upgrades.
	bool SendHostScreen(HostScreen theScreen, int32_t theArg, const LevelSetup& theProfile);
	// Client: the next flow step, in the order the host took them.
	bool PopFlowEvent(FlowEvent& theEventOut);

	// Client: say we are past the title screen. Sent once per session.
	bool SendPeerReady();

	// Either side: this player's name and colour (0x00RRGGBB). Sent once
	// connected and again whenever either changes.
	bool SendPlayerInfo(const std::string& theName, uint32_t theColor);
	bool               HasRemoteInfo() const { return mHasRemoteInfo; }
	const std::string& GetRemoteName() const { return mRemoteName; }
	uint32_t           GetRemoteColor() const { return mRemoteColor; }
	// Host: has the client said so?
	bool IsPeerReady() const { return mPeerReady; }
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
	bool        mNetTest = false;
};

LaunchOptions& GetLaunchOptions();
// Consumes argv. Returns false if the arguments were malformed.
//
// Co-op flags are stripped out as they are read: the game parses argv itself
// and treats anything it does not recognise as a fatal error, so it must only
// ever see the arguments that are its own. Call GetGameArgs() afterwards for
// the filtered list to hand to the game.
bool ParseLaunchOptions(int argc, char** argv, std::string& theErrorOut);
// argv with every co-op flag and its value removed; argv[0] is kept.
std::vector<char*>& GetGameArgs();
// Loopback host+client handshake, no game assets needed. Returns 0 on success.
int RunSelfTest();

// Headless two-machine connection test driven by --host / --join. Runs the
// real lockstep loop at the real tick rate over the real network and reports
// stalls and latency, so a link can be checked before the game is involved.
// Needs no game assets. Returns 0 on success.
int RunNetTest();

} // namespace Coop

#endif
