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

#ifndef __COOPLOCKSTEP_H__
#define __COOPLOCKSTEP_H__

#include "CoopProtocol.h"
#include "CoopSession.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace Coop
{

// Drives the simulation clock so both machines run the same ticks with the
// same inputs.
//
// The contract, once per logic tick:
//
//     if (lockstep.BeginTick())        // false = stalled, do not simulate
//     {
//         for (cmd : lockstep.GetTickCommands()) ApplyToBoard(cmd);
//         Board::Update();
//         lockstep.EndTick(ChecksumOfBoard());
//     }
//
// Input sampled during tick T is scheduled for tick T + delay on BOTH
// machines, which is what gives the network time to deliver it.
class Lockstep
{
public:
	static constexpr uint32_t MAX_INPUT_DELAY = 20;     // 200 ms
	// Never drop below this: some delay is always needed to cover the link.
	static constexpr uint32_t MIN_INPUT_DELAY = 2;      // 20 ms
	// Everything below is measured in milliseconds of real stalling, NOT in
	// calls to BeginTick(). The caller polls in a tight loop, so counting
	// calls measured how fast the loop spins rather than how bad the link is:
	// a single hiccup was enough to ratchet the delay to its ceiling in
	// microseconds, where it then stayed for the whole session.
	static constexpr uint32_t STALL_MS_BEFORE_RAISE = 120;
	// Tell the player something is wrong only once it is worth saying.
	static constexpr uint32_t STALL_MS_BEFORE_WARNING = 500;
	// Smooth ticks before giving the delay back, so a brief hiccup does not
	// cost input lag for the rest of the session. 400 ticks is four seconds.
	static constexpr uint32_t SMOOTH_TICKS_BEFORE_LOWER = 400;

private:
	Session*    mSession = nullptr;
	bool        mRunning = false;
	uint32_t    mCurrentTick = 0;
	uint32_t    mInputDelay = DEFAULT_INPUT_DELAY;
	PlayerId    mLocalPlayer = PlayerId::Host;

	// Input this machine's player has generated but not yet sent.
	std::vector<Command> mPendingLocal;
	// The peer's input, by the tick it executes on. A tick may only be
	// simulated once its entry exists.
	std::map<uint32_t, std::vector<Command>> mRemoteByTick;
	// Our own input, by the tick it executes on. Scheduled exactly like the
	// peer's: this machine must apply its own commands on the same tick the
	// peer applies them, not immediately.
	std::map<uint32_t, std::vector<Command>> mLocalByTick;
	// Both players' input for the tick currently being simulated.
	std::vector<Command> mTickCommands;

	// The next tick we owe the peer a batch for. The peer blocks until it has
	// a batch for every tick in order, so this only ever advances by one and
	// never skips: see the send loop in BeginTick().
	uint32_t    mNextSendTick = 0;
	uint32_t    mLastSentTick = 0;
	bool        mHasSentAny = false;
	// Exactly one batch goes out per simulated tick. Sending once per
	// BeginTick() instead would flood the peer whenever we stall.
	bool        mSentForCurrentTick = false;

	// Stall accounting, in real time rather than poll counts.
	bool        mStalling = false;
	std::chrono::steady_clock::time_point mStallBegan{};
	std::chrono::steady_clock::time_point mLastDelayRaise{};
	uint32_t    mStallEvents = 0;      // distinct times we had to wait
	uint64_t    mTotalStallMs = 0;     // how long we waited in total
	uint32_t    mSmoothTicks = 0;      // consecutive ticks with no waiting

	uint32_t    CurrentStallMs() const;

	// Our own checksums, kept until the peer's arrive to compare against.
	std::map<uint32_t, uint32_t> mLocalChecksums;
	bool        mDesynced = false;
	std::string mDesyncText;

	void DrainSession();

public:
	// theParams comes from the handshake, so both machines start identically.
	void Begin(Session* theSession, const SessionParams& theParams, PlayerId theLocalPlayer);
	void Reset();

	// Record something this player did. Scheduled automatically.
	void QueueLocalCommand(CommandType theType, int16_t theX, int16_t theY, int16_t theArg = 0);

	// Returns true when the simulation may advance. False means we are waiting
	// on the peer: keep rendering, do not touch game state.
	bool BeginTick();
	const std::vector<Command>& GetTickCommands() const { return mTickCommands; }
	// theChecksum fingerprints the simulation after this tick; pass 0 to skip.
	void EndTick(uint32_t theChecksum);

	bool     IsRunning() const { return mRunning; }
	uint32_t GetCurrentTick() const { return mCurrentTick; }
	uint32_t GetInputDelay() const { return mInputDelay; }
	PlayerId GetLocalPlayer() const { return mLocalPlayer; }

	bool     IsStalled() const { return mStalling; }
	// True once we have been waiting long enough to be worth mentioning.
	bool     ShouldShowWaiting() const { return CurrentStallMs() >= STALL_MS_BEFORE_WARNING; }
	uint32_t GetStallEvents() const { return mStallEvents; }
	uint64_t GetTotalStallMs() const { return mTotalStallMs; }
	uint32_t CurrentStallMsPublic() const { return CurrentStallMs(); }

	bool               IsDesynced() const { return mDesynced; }
	const std::string& GetDesyncText() const { return mDesyncText; }
};

// Order-independent mixing, so a checksum does not depend on container order.
// Only gameplay state belongs in one of these: feeding it anything cosmetic
// (particles, draw-only randomness) would report desyncs that do not matter.
class ChecksumBuilder
{
private:
	uint32_t mValue = 2166136261u;

public:
	void Add(int32_t theValue);
	void Add(uint32_t theValue);
	// Quantised deliberately: tiny float differences that never affect
	// gameplay should not trip the detector.
	void AddQuantised(float theValue, float theScale = 16.0f);
	uint32_t Get() const { return mValue; }
};

// Runs two Lockstep instances against each other over loopback and checks
// that they advance in step, agree tick for tick, and that a deliberate
// divergence is actually caught. Returns 0 on success.
int RunLockstepSelfTest();

} // namespace Coop

#endif
