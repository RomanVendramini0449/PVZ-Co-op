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

#include "CoopLockstep.h"

#include <cmath>

namespace Coop
{

void Lockstep::Begin(Session* theSession, const SessionParams& theParams, PlayerId theLocalPlayer)
{
	Reset();

	mSession = theSession;
	mLocalPlayer = theLocalPlayer;
	mInputDelay = theParams.mInputDelay;
	if (mInputDelay == 0)
		mInputDelay = 1;                       // a delay of 0 could never be delivered in time
	if (mInputDelay > MAX_INPUT_DELAY)
		mInputDelay = MAX_INPUT_DELAY;

	// The peer's first batch is for tick mInputDelay, so the opening ticks
	// have no remote input by construction. Seed them as settled-and-empty,
	// otherwise both machines would wait on each other forever at tick 0.
	for (uint32_t aTick = 0; aTick < mInputDelay; ++aTick)
		mRemoteByTick[aTick] = {};

	mRunning = true;
}

void Lockstep::Reset()
{
	mSession = nullptr;
	mRunning = false;
	mCurrentTick = 0;
	mInputDelay = DEFAULT_INPUT_DELAY;
	mPendingLocal.clear();
	mRemoteByTick.clear();
	mLocalByTick.clear();
	mTickCommands.clear();
	mLastSentTick = 0;
	mHasSentAny = false;
	mSentForCurrentTick = false;
	mStallTicks = 0;
	mTotalStallTicks = 0;
	mStallSinceDelayRaise = 0;
	mLocalChecksums.clear();
	mDesynced = false;
	mDesyncText.clear();
}

void Lockstep::QueueLocalCommand(CommandType theType, int16_t theX, int16_t theY, int16_t theArg)
{
	if (!mRunning)
		return;

	Command aCommand;
	aCommand.mPlayer = mLocalPlayer;
	aCommand.mType = theType;
	aCommand.mX = theX;
	aCommand.mY = theY;
	aCommand.mArg = theArg;
	// mTick is stamped at send time, when the target tick is final.
	mPendingLocal.push_back(aCommand);
}

void Lockstep::DrainSession()
{
	if (mSession == nullptr)
		return;

	CommandBatch aBatch;
	while (mSession->PopCommandBatch(aBatch))
	{
		// Trust the batch header for the tick, not the per-command field: the
		// header is what the sender committed to.
		for (Command& aCommand : aBatch.mCommands)
			aCommand.mTick = aBatch.mTick;
		mRemoteByTick[aBatch.mTick] = std::move(aBatch.mCommands);
	}

	uint32_t aTick = 0;
	uint32_t aRemoteChecksum = 0;
	while (mSession->PopChecksum(aTick, aRemoteChecksum))
	{
		auto aFound = mLocalChecksums.find(aTick);
		if (aFound == mLocalChecksums.end())
			continue;  // ours for that tick is gone or not computed yet

		if (aFound->second != aRemoteChecksum && !mDesynced)
		{
			mDesynced = true;
			mDesyncText = "The two games have fallen out of sync (tick "
				+ std::to_string(aTick) + "). This is a bug, not your connection.";
		}
		mLocalChecksums.erase(aFound);
	}

	// Anything older than the live tick can never be compared again.
	while (!mLocalChecksums.empty() && mLocalChecksums.begin()->first + 1000 < mCurrentTick)
		mLocalChecksums.erase(mLocalChecksums.begin());
}

bool Lockstep::BeginTick()
{
	if (!mRunning || mSession == nullptr || !mSession->IsConnected())
		return false;
	if (mDesynced)
		return false;  // simulating past a desync only makes the divergence worse

	DrainSession();

	// Send this tick's input before checking whether we can advance: if both
	// machines are waiting, it is each other's batch they are waiting for.
	//
	// Exactly once per tick, though. While stalled this function is called
	// over and over at the same tick, and sending each time would bury the
	// peer in batches for ticks it has not reached. One per tick is also all
	// correctness needs: a stalled machine has already sent far enough ahead
	// for the peer to keep moving and eventually unblock it.
	if (!mSentForCurrentTick)
	{
		uint32_t aTargetTick = mCurrentTick + mInputDelay;
		if (mHasSentAny && aTargetTick <= mLastSentTick)
			aTargetTick = mLastSentTick + 1;  // never re-send a tick already committed

		for (Command& aCommand : mPendingLocal)
			aCommand.mTick = aTargetTick;

		if (mSession->SendCommandBatch(aTargetTick, mPendingLocal))
		{
			// Schedule our own input for the same tick we just told the peer
			// to run it on. Applying it locally any earlier would put this
			// machine a few ticks ahead of the other and desync the game.
			if (!mPendingLocal.empty())
				mLocalByTick[aTargetTick] = mPendingLocal;

			mPendingLocal.clear();
			mLastSentTick = aTargetTick;
			mHasSentAny = true;
			mSentForCurrentTick = true;
		}
	}

	// We can only simulate a tick once the peer's input for it has landed.
	auto aFound = mRemoteByTick.find(mCurrentTick);
	if (aFound == mRemoteByTick.end())
	{
		++mStallTicks;
		++mTotalStallTicks;
		++mStallSinceDelayRaise;

		// A persistent stall means the link is slower than our delay assumes.
		// Raising it costs a little input lag and buys back smoothness. This
		// is safe to do unilaterally: every command carries the tick it
		// executes on, so the peer applies it at the same tick regardless.
		//
		// mStallTicks itself is left alone so the "waiting for player" notice
		// still fires on a long stall rather than being reset by a raise.
		if (mStallSinceDelayRaise >= STALL_BEFORE_DELAY_RAISE && mInputDelay < MAX_INPUT_DELAY)
		{
			++mInputDelay;
			mStallSinceDelayRaise = 0;
		}
		return false;
	}

	mStallTicks = 0;
	mStallSinceDelayRaise = 0;

	// Both players' input for this tick: ours and theirs.
	mTickCommands.clear();

	auto aLocalFound = mLocalByTick.find(mCurrentTick);
	if (aLocalFound != mLocalByTick.end())
	{
		for (const Command& aCommand : aLocalFound->second)
			mTickCommands.push_back(aCommand);
		mLocalByTick.erase(aLocalFound);
	}
	for (const Command& aCommand : aFound->second)
		mTickCommands.push_back(aCommand);
	mRemoteByTick.erase(aFound);

	// Order by player, never by which machine we happen to be. "Local first"
	// would mean the host applies host-then-client while the client applies
	// client-then-host, and two plants racing for one tile would land
	// differently on the two machines.
	std::stable_sort(mTickCommands.begin(), mTickCommands.end(),
		[](const Command& a, const Command& b) {
			return static_cast<uint8_t>(a.mPlayer) < static_cast<uint8_t>(b.mPlayer);
		});

	return true;
}

void Lockstep::EndTick(uint32_t theChecksum)
{
	if (!mRunning)
		return;

	if (theChecksum != 0 && (mCurrentTick % CHECKSUM_INTERVAL_TICKS) == 0)
	{
		mLocalChecksums[mCurrentTick] = theChecksum;
		if (mSession != nullptr)
			mSession->SendChecksum(mCurrentTick, theChecksum);
	}

	mSentForCurrentTick = false;
	++mCurrentTick;
}

// ---------------------------------------------------------------------------
// ChecksumBuilder
// ---------------------------------------------------------------------------

void ChecksumBuilder::Add(uint32_t theValue)
{
	mValue ^= theValue;
	mValue *= 16777619u;
	mValue ^= (mValue >> 13);
}

void ChecksumBuilder::Add(int32_t theValue)
{
	Add(static_cast<uint32_t>(theValue));
}

void ChecksumBuilder::AddQuantised(float theValue, float theScale)
{
	// Round to a fixed grid so a last-bit float difference does not read as a
	// desync, while anything that would actually change play still does.
	const float aScaled = theValue * theScale;
	if (!std::isfinite(aScaled))
	{
		Add(static_cast<uint32_t>(0xDEADBEEFu));
		return;
	}
	Add(static_cast<int32_t>(std::lround(aScaled)));
}

} // namespace Coop

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

#include <cstdio>
#include <thread>
#include <chrono>

namespace Coop
{

namespace
{
	// A stand-in for the game simulation: deterministic, and sensitive to both
	// the tick number and the input applied, which is exactly what a real
	// desync would perturb.
	struct FakeSim
	{
		uint32_t mState = 12345u;

		void ApplyTick(uint32_t theTick, const std::vector<Command>& theCommands)
		{
			mState = mState * 31u + theTick;
			for (const Command& aCommand : theCommands)
			{
				mState = mState * 31u + static_cast<uint32_t>(aCommand.mType);
				mState = mState * 31u + static_cast<uint32_t>(aCommand.mX);
				mState = mState * 31u + static_cast<uint32_t>(aCommand.mY);
				mState = mState * 31u + static_cast<uint32_t>(aCommand.mPlayer);
			}
		}

		uint32_t Checksum() const
		{
			ChecksumBuilder aBuilder;
			aBuilder.Add(mState);
			return aBuilder.Get();
		}
	};

	bool ConnectPair(Session& theHost, Session& theClient, uint16_t thePort)
	{
		if (!theHost.StartHost(thePort, Difficulty::Normal, 1))
			return false;
		if (!theClient.StartJoin("127.0.0.1", thePort))
			return false;
		for (int i = 0; i < 3000; ++i)
		{
			theHost.Update();
			theClient.Update();
			if (theHost.IsConnected() && theClient.IsConnected())
				return true;
			if (theHost.GetState() == Session::State::Failed
				|| theClient.GetState() == Session::State::Failed)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}
}

int RunLockstepSelfTest()
{
	printf("lockstep selftest: starting\n");

#if !COOP_SOCKETS_AVAILABLE
	printf("lockstep selftest: SKIP (sockets unavailable on this platform)\n");
	return 0;
#else
	Session aHostSession;
	Session aClientSession;
	if (!ConnectPair(aHostSession, aClientSession, 34568))
	{
		printf("lockstep selftest: FAIL could not connect\n");
		return 1;
	}

	Lockstep aHostStep;
	Lockstep aClientStep;
	aHostStep.Begin(&aHostSession, aHostSession.GetParams(), PlayerId::Host);
	aClientStep.Begin(&aClientSession, aClientSession.GetParams(), PlayerId::Client);

	FakeSim aHostSim;
	FakeSim aClientSim;

	constexpr uint32_t TARGET_TICKS = 300;
	// Inputs injected mid-run, to prove remote commands land on the same tick
	// on both machines rather than merely arriving.
	constexpr uint32_t HOST_INPUT_TICK = 40;
	constexpr uint32_t CLIENT_INPUT_TICK = 55;
	bool aHostQueued = false;
	bool aClientQueued = false;

	for (int aIteration = 0; aIteration < 400000; ++aIteration)
	{
		if (aHostStep.GetCurrentTick() >= TARGET_TICKS
			&& aClientStep.GetCurrentTick() >= TARGET_TICKS)
			break;

		aHostSession.Update();
		aClientSession.Update();

		if (!aHostQueued && aHostStep.GetCurrentTick() == HOST_INPUT_TICK)
		{
			aHostStep.QueueLocalCommand(CommandType::PlantAt, 3, 2, 1);
			aHostQueued = true;
		}
		if (!aClientQueued && aClientStep.GetCurrentTick() == CLIENT_INPUT_TICK)
		{
			aClientStep.QueueLocalCommand(CommandType::Shovel, 7, 4, 0);
			aClientQueued = true;
		}

		if (aHostStep.GetCurrentTick() < TARGET_TICKS && aHostStep.BeginTick())
		{
			aHostSim.ApplyTick(aHostStep.GetCurrentTick(), aHostStep.GetTickCommands());
			aHostStep.EndTick(aHostSim.Checksum());
		}
		if (aClientStep.GetCurrentTick() < TARGET_TICKS && aClientStep.BeginTick())
		{
			aClientSim.ApplyTick(aClientStep.GetCurrentTick(), aClientStep.GetTickCommands());
			aClientStep.EndTick(aClientSim.Checksum());
		}

		if (aHostStep.IsDesynced() || aClientStep.IsDesynced())
			break;
	}

	// Check for a desync first: it is the more specific explanation, and
	// reporting it as "did not reach N ticks" would hide the real cause.
	if (aHostStep.IsDesynced() || aClientStep.IsDesynced())
	{
		printf("lockstep selftest: FAIL unexpected desync at host tick %u: %s%s\n",
			aHostStep.GetCurrentTick(),
			aHostStep.GetDesyncText().c_str(), aClientStep.GetDesyncText().c_str());
		return 1;
	}
	if (aHostStep.GetCurrentTick() < TARGET_TICKS || aClientStep.GetCurrentTick() < TARGET_TICKS)
	{
		printf("lockstep selftest: FAIL did not reach %u ticks (host %u, client %u)\n",
			TARGET_TICKS, aHostStep.GetCurrentTick(), aClientStep.GetCurrentTick());
		printf("  host session:   %s\n", aHostSession.GetStatusText().c_str());
		printf("  client session: %s\n", aClientSession.GetStatusText().c_str());
		return 1;
	}
	if (aHostSim.mState != aClientSim.mState)
	{
		printf("lockstep selftest: FAIL simulations diverged (%u vs %u)\n",
			aHostSim.mState, aClientSim.mState);
		return 1;
	}
	printf("lockstep selftest: %u ticks in step, states agree (0x%08X)\n",
		TARGET_TICKS, aHostSim.mState);
	printf("lockstep selftest: stalls host=%u client=%u, delay host=%u client=%u\n",
		aHostStep.GetTotalStallTicks(), aClientStep.GetTotalStallTicks(),
		aHostStep.GetInputDelay(), aClientStep.GetInputDelay());

	// Now prove the detector actually fires: corrupt one side's state and keep
	// running. A desync detector that never reports is worse than none.
	aClientSim.mState ^= 0x5A5A5A5Au;

	bool aCaught = false;
	for (int aIteration = 0; aIteration < 400000 && !aCaught; ++aIteration)
	{
		aHostSession.Update();
		aClientSession.Update();

		if (aHostStep.BeginTick())
		{
			aHostSim.ApplyTick(aHostStep.GetCurrentTick(), aHostStep.GetTickCommands());
			aHostStep.EndTick(aHostSim.Checksum());
		}
		if (aClientStep.BeginTick())
		{
			aClientSim.ApplyTick(aClientStep.GetCurrentTick(), aClientStep.GetTickCommands());
			aClientStep.EndTick(aClientSim.Checksum());
		}
		aCaught = aHostStep.IsDesynced() || aClientStep.IsDesynced();
	}

	if (!aCaught)
	{
		printf("lockstep selftest: FAIL a deliberate desync went undetected\n");
		return 1;
	}
	printf("lockstep selftest: desync detected as expected\n");

	aClientSession.Disconnect("selftest done");
	aHostSession.Disconnect("selftest done");

	printf("lockstep selftest: PASS\n");
	return 0;
#endif
}

} // namespace Coop
