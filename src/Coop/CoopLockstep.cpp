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
#include <cstdarg>
#include <cstdio>

namespace Coop
{

void Lockstep::Begin(Session* theSession, const SessionParams& theParams, PlayerId theLocalPlayer, uint32_t theEpoch)
{
	Reset();

	mSession = theSession;
	mEpoch = theEpoch;
	mLocalPlayer = theLocalPlayer;
	mInputDelay = theParams.mInputDelay;
	if (mInputDelay < MIN_INPUT_DELAY)
		mInputDelay = MIN_INPUT_DELAY;         // too little could never be delivered in time
	if (mInputDelay > MAX_INPUT_DELAY)
		mInputDelay = MAX_INPUT_DELAY;

	// The peer's first batch is for tick mInputDelay, so the opening ticks
	// have no remote input by construction. Seed them as settled-and-empty,
	// otherwise both machines would wait on each other forever at tick 0.
	for (uint32_t aTick = 0; aTick < mInputDelay; ++aTick)
		mRemoteByTick[aTick] = {};

	// Matching the prefill: the first batch we owe the peer is for tick
	// mInputDelay, since it already treats everything before that as settled.
	mNextSendTick = mInputDelay;

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
	mNextSendTick = 0;
	mLastSentTick = 0;
	mHasSentAny = false;
	mSentForCurrentTick = false;
	mStalling = false;
	mStallBegan = {};
	mLastDelayRaise = {};
	mStallEvents = 0;
	mTotalStallMs = 0;
	mSmoothTicks = 0;
	mMinHeadroom = 0;
	mLocalChecksums.clear();
	mDesynced = false;
	mDesyncText.clear();
}

uint32_t Lockstep::CurrentStallMs() const
{
	if (!mStalling)
		return 0;
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - mStallBegan).count());
}

void Lockstep::ReportDesync(const std::string& theReason)
{
	if (mDesynced)
		return;  // the first cause is the useful one

	mDesynced = true;
	mDesyncText = theReason;
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
	while (mSession->PopCommandBatch(aBatch, mEpoch))
	{
		// Trust the batch header for the tick, not the per-command field: the
		// header is what the sender committed to.
		for (Command& aCommand : aBatch.mCommands)
			aCommand.mTick = aBatch.mTick;
		mRemoteByTick[aBatch.mTick] = std::move(aBatch.mCommands);
	}

	uint32_t aTick = 0;
	uint32_t aRemoteChecksum = 0;
	while (mSession->PopChecksum(aTick, aRemoteChecksum, mEpoch))
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
		// Send every tick up to currentTick + delay, in order, with no holes.
		//
		// The holes matter enormously. The peer cannot run tick N until it
		// has our batch for N, so a single skipped tick blocks it forever.
		// Raising the delay mid-stall used to do exactly that: the target
		// jumped from lastSent+1 to currentTick+newDelay, skipping everything
		// between. The peer then stalled on the first missing tick, raised
		// *its* delay, skipped ticks of its own, and both sides deadlocked
		// permanently. It never showed up over loopback, because without a
		// stall the delay never rises.
		//
		// So catching up emits the intervening ticks as empty batches rather
		// than jumping over them. Local input rides the last one, which is
		// the tick the input delay actually refers to.
		const uint32_t aDesiredTick = mCurrentTick + mInputDelay;
		bool aSendOk = true;

		while (aSendOk && mNextSendTick <= aDesiredTick)
		{
			const bool aIsInputTick = (mNextSendTick == aDesiredTick);
			static const std::vector<Command> kEmpty;

			if (aIsInputTick)
			{
				for (Command& aCommand : mPendingLocal)
					aCommand.mTick = mNextSendTick;
			}

			const std::vector<Command>& aPayload = aIsInputTick ? mPendingLocal : kEmpty;
			aSendOk = mSession->SendCommandBatch(mNextSendTick, aPayload, mEpoch);
			if (!aSendOk)
				break;

			// Schedule our own input for the same tick we just told the peer
			// to run it on. Applying it locally any earlier would put this
			// machine ahead of the other and desync the game.
			if (aIsInputTick && !mPendingLocal.empty())
			{
				mLocalByTick[mNextSendTick] = mPendingLocal;
				mPendingLocal.clear();
			}

			mLastSentTick = mNextSendTick;
			mHasSentAny = true;
			++mNextSendTick;
		}

		if (aSendOk)
			mSentForCurrentTick = true;
	}

	// We can only simulate a tick once the peer's input for it has landed.
	auto aFound = mRemoteByTick.find(mCurrentTick);
	if (aFound == mRemoteByTick.end())
	{
		const auto aNow = std::chrono::steady_clock::now();
		if (!mStalling)
		{
			mStalling = true;
			mStallBegan = aNow;
			mLastDelayRaise = aNow;
			++mStallEvents;
		}
		mSmoothTicks = 0;

		// A sustained stall means the link is slower than our delay assumes.
		// Raising it costs a little input lag and buys back smoothness, and
		// is safe to do unilaterally: every command carries the tick it runs
		// on, so the peer applies it at the same tick regardless.
		//
		// Paced by elapsed time, not by how often this function is called.
		// The caller polls in a tight loop, so counting calls raised the
		// delay dozens of steps during a single hiccup and pinned it at the
		// ceiling for the rest of the session.
		const auto aSinceRaise = std::chrono::duration_cast<std::chrono::milliseconds>(
			aNow - mLastDelayRaise).count();
		if (aSinceRaise >= static_cast<long long>(STALL_MS_BEFORE_RAISE)
			&& mInputDelay < MAX_INPUT_DELAY)
		{
			++mInputDelay;
			mLastDelayRaise = aNow;
		}
		return false;
	}

	if (mStalling)
	{
		mStalling = false;
		mTotalStallMs += static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - mStallBegan).count());
	}

	// How far ahead of need the peer's input is arriving: the ticks after
	// this one that are already here. That is the slack a lower delay would
	// spend.
	uint32_t aHeadroom = 0;
	while (aHeadroom < HEADROOM_BEFORE_LOWER && mRemoteByTick.count(mCurrentTick + aHeadroom + 1) != 0)
		++aHeadroom;
	if (mSmoothTicks == 0 || aHeadroom < mMinHeadroom)
		mMinHeadroom = aHeadroom;

	// Give the delay back once the link has behaved for a while, so one bad
	// moment does not cost input lag for the rest of the session -- but only
	// if there was slack the whole time. Lowering it is safe: mNextSendTick
	// only ever advances, so we simply coast without sending until
	// currentTick + delay catches up with it again.
	++mSmoothTicks;
	if (mSmoothTicks >= SMOOTH_TICKS_BEFORE_LOWER)
	{
		if (mMinHeadroom >= HEADROOM_BEFORE_LOWER && mInputDelay > MIN_INPUT_DELAY)
			--mInputDelay;
		mSmoothTicks = 0;
	}

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
			mSession->SendChecksum(mCurrentTick, theChecksum, mEpoch);
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
			aHostStep.QueueLocalCommand(CommandType::MouseDown, 3, 2, 1);
			aHostQueued = true;
		}
		if (!aClientQueued && aClientStep.GetCurrentTick() == CLIENT_INPUT_TICK)
		{
			aClientStep.QueueLocalCommand(CommandType::MouseUp, 7, 4, 0);
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
		aHostStep.GetStallEvents(), aClientStep.GetStallEvents(),
		aHostStep.GetInputDelay(), aClientStep.GetInputDelay());

	// Now the case that loopback never produces on its own: a real stall.
	// Freezing one side forces the other to wait, raise its input delay, and
	// then catch up. That path used to deadlock both machines permanently,
	// and it only ever appeared over a link with real latency, so it is
	// reproduced deliberately here.
	{
		constexpr uint32_t STALL_TARGET = TARGET_TICKS + 200;
		const uint32_t aDelayBefore = aHostStep.GetInputDelay();

		// Host keeps running while the client is frozen: no pumping, so its
		// batches stop arriving and the host starts waiting.
		// Real time has to pass now, because the delay is paced in
		// milliseconds rather than in polls.
		const auto aFreezeUntil = std::chrono::steady_clock::now()
			+ std::chrono::milliseconds(600);
		while (std::chrono::steady_clock::now() < aFreezeUntil)
		{
			aHostSession.Update();
			if (aHostStep.BeginTick())
			{
				aHostSim.ApplyTick(aHostStep.GetCurrentTick(), aHostStep.GetTickCommands());
				aHostStep.EndTick(aHostSim.Checksum());
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		if (aHostStep.GetStallEvents() == 0)
		{
			printf("lockstep selftest: FAIL expected the host to stall while the client was frozen\n");
			return 1;
		}
		if (aHostStep.GetInputDelay() <= aDelayBefore)
		{
			printf("lockstep selftest: FAIL expected the delay to rise during a 600ms stall\n");
			return 1;
		}
		// A 600 ms stall should cost a handful of steps, not slam the ceiling.
		if (aHostStep.GetInputDelay() >= Lockstep::MAX_INPUT_DELAY)
		{
			printf("lockstep selftest: FAIL delay hit the ceiling (%u) after one short stall\n",
				aHostStep.GetInputDelay());
			return 1;
		}
		printf("lockstep selftest: host stalled %u time(s), delay %u -> %u\n",
			aHostStep.GetStallEvents(), aDelayBefore, aHostStep.GetInputDelay());

		// Unfreeze. Both must now catch up and finish in step. Before the fix
		// this hung forever: the raised delay had skipped tick numbers, and
		// the peer blocks on the first batch it never receives.
		for (int aIteration = 0; aIteration < 400000; ++aIteration)
		{
			if (aHostStep.GetCurrentTick() >= STALL_TARGET
				&& aClientStep.GetCurrentTick() >= STALL_TARGET)
				break;

			aHostSession.Update();
			aClientSession.Update();

			if (aHostStep.GetCurrentTick() < STALL_TARGET && aHostStep.BeginTick())
			{
				aHostSim.ApplyTick(aHostStep.GetCurrentTick(), aHostStep.GetTickCommands());
				aHostStep.EndTick(aHostSim.Checksum());
			}
			if (aClientStep.GetCurrentTick() < STALL_TARGET && aClientStep.BeginTick())
			{
				aClientSim.ApplyTick(aClientStep.GetCurrentTick(), aClientStep.GetTickCommands());
				aClientStep.EndTick(aClientSim.Checksum());
			}
			if (aHostStep.IsDesynced() || aClientStep.IsDesynced())
				break;
		}

		if (aHostStep.GetCurrentTick() < STALL_TARGET || aClientStep.GetCurrentTick() < STALL_TARGET)
		{
			printf("lockstep selftest: FAIL deadlocked after a stall (host %u, client %u of %u)\n",
				aHostStep.GetCurrentTick(), aClientStep.GetCurrentTick(), STALL_TARGET);
			return 1;
		}
		if (aHostSim.mState != aClientSim.mState)
		{
			printf("lockstep selftest: FAIL diverged across a stall (%u vs %u)\n",
				aHostSim.mState, aClientSim.mState);
			return 1;
		}
		printf("lockstep selftest: recovered from the stall, states still agree (0x%08X)\n",
			aHostSim.mState);
	}

	// A level transition, with the race that real play produces: the host
	// enters the next level first and starts ticking while the client is
	// still finishing the old one, so the old level's batches and checksums
	// are still in flight in both directions when the new one begins. Tick
	// numbers restart at 0, so without epochs those stale frames are read as
	// the new level's -- a false desync, or a wait for a tick never re-sent.
	{
		constexpr uint32_t NEXT_EPOCH = 2;
		constexpr uint32_t LEVEL_TICKS = 300;

		aHostStep.Begin(&aHostSession, aHostSession.GetParams(), PlayerId::Host, NEXT_EPOCH);
		aHostSim = FakeSim();

		// The client keeps simulating the old level for a while, pumping out
		// old-epoch traffic the host has to throw away.
		const auto aLingerUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
		while (std::chrono::steady_clock::now() < aLingerUntil)
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
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// Now the client follows. Its old epoch is over.
		aClientStep.Begin(&aClientSession, aClientSession.GetParams(), PlayerId::Client, NEXT_EPOCH);
		aClientSim = FakeSim();

		bool aQueued = false;
		for (int aIteration = 0; aIteration < 400000; ++aIteration)
		{
			if (aHostStep.GetCurrentTick() >= LEVEL_TICKS && aClientStep.GetCurrentTick() >= LEVEL_TICKS)
				break;

			aHostSession.Update();
			aClientSession.Update();

			if (!aQueued && aClientStep.GetCurrentTick() == 20)
			{
				aClientStep.QueueLocalCommand(CommandType::MouseDown, 1, 1, 1);
				aQueued = true;
			}
			if (aHostStep.GetCurrentTick() < LEVEL_TICKS && aHostStep.BeginTick())
			{
				aHostSim.ApplyTick(aHostStep.GetCurrentTick(), aHostStep.GetTickCommands());
				aHostStep.EndTick(aHostSim.Checksum());
			}
			if (aClientStep.GetCurrentTick() < LEVEL_TICKS && aClientStep.BeginTick())
			{
				aClientSim.ApplyTick(aClientStep.GetCurrentTick(), aClientStep.GetTickCommands());
				aClientStep.EndTick(aClientSim.Checksum());
			}
			if (aHostStep.IsDesynced() || aClientStep.IsDesynced())
				break;
		}

		if (aHostStep.IsDesynced() || aClientStep.IsDesynced())
		{
			printf("lockstep selftest: FAIL false desync across a level transition: %s%s\n",
				aHostStep.GetDesyncText().c_str(), aClientStep.GetDesyncText().c_str());
			return 1;
		}
		if (aHostStep.GetCurrentTick() < LEVEL_TICKS || aClientStep.GetCurrentTick() < LEVEL_TICKS)
		{
			printf("lockstep selftest: FAIL stuck after a level transition (host %u, client %u of %u)\n",
				aHostStep.GetCurrentTick(), aClientStep.GetCurrentTick(), LEVEL_TICKS);
			return 1;
		}
		if (aHostSim.mState != aClientSim.mState)
		{
			printf("lockstep selftest: FAIL diverged across a level transition (%u vs %u)\n",
				aHostSim.mState, aClientSim.mState);
			return 1;
		}
		printf("lockstep selftest: level transition with traffic in flight, states agree (0x%08X)\n",
			aHostSim.mState);
	}

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

// ---------------------------------------------------------------------------
// Two-machine network test
// ---------------------------------------------------------------------------

namespace Coop
{

namespace
{
	// The net test has to be readable live -- the host needs to see its own
	// IP address while it is still waiting -- AND end up in a file the player
	// can send on. Redirecting the console to a file gives up the first, and
	// batch files have no tee, so the program writes both itself.
	FILE* gNetTestLog = nullptr;

	void LogPrintf(const char* theFormat, ...)
	{
		va_list aArgs;

		va_start(aArgs, theFormat);
		vprintf(theFormat, aArgs);
		va_end(aArgs);
		fflush(stdout);  // unbuffered, so progress shows as it happens

		if (gNetTestLog != nullptr)
		{
			va_start(aArgs, theFormat);
			vfprintf(gNetTestLog, theFormat, aArgs);
			va_end(aArgs);
			fflush(gNetTestLog);
		}
	}
}

int RunNetTest()
{
#if !COOP_SOCKETS_AVAILABLE
	LogPrintf("coop nettest: co-op is not available on this platform\n");
	return 1;
#else
	const LaunchOptions& aOptions = GetLaunchOptions();
	const bool aIsHost = aOptions.mWantHost;

	gNetTestLog = fopen(aIsHost ? "coop-nettest-host.txt" : "coop-nettest-join.txt", "w");

	Session aSession;
	if (aIsHost)
	{
		if (!aSession.StartHost(aOptions.mPort, aOptions.mDifficulty, 1))
		{
			LogPrintf("coop nettest: FAILED to host: %s\n", aSession.GetStatusText().c_str());
			return 1;
		}

		// Save the host a trip to ipconfig: tell them exactly what their
		// friend has to type.
		const std::vector<std::string> aAddresses = Socket::GetLocalAddresses();
		LogPrintf("\n");
		if (aAddresses.empty())
		{
			LogPrintf("coop nettest: could not work out this machine's IP address.\n");
			LogPrintf("coop nettest: run 'ipconfig' and look for the 192.168.x.x one.\n");
		}
		else
		{
			LogPrintf("==================================================\n");
			LogPrintf(" Tell the other player to connect to:\n");
			for (const std::string& aAddress : aAddresses)
				LogPrintf("     %s:%u\n", aAddress.c_str(), aOptions.mPort);
			LogPrintf("==================================================\n");
		}
		printf("\ncoop nettest: waiting for the other player to join...\n");
	}
	else
	{
		LogPrintf("coop nettest: connecting to %s:%u...\n", aOptions.mJoinHost.c_str(), aOptions.mPort);
		if (!aSession.StartJoin(aOptions.mJoinHost, aOptions.mPort))
		{
			LogPrintf("coop nettest: FAILED to connect: %s\n", aSession.GetStatusText().c_str());
			return 1;
		}
	}

	// Two minutes is enough for a person to start the other side by hand.
	const auto aDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
	while (!aSession.IsConnected())
	{
		aSession.Update();
		if (aSession.GetState() == Session::State::Failed)
		{
			LogPrintf("coop nettest: FAILED: %s\n", aSession.GetStatusText().c_str());
			return 1;
		}
		if (std::chrono::steady_clock::now() > aDeadline)
		{
			LogPrintf("coop nettest: FAILED: timed out waiting for the other player.\n");
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	LogPrintf("coop nettest: connected. %s\n", aSession.GetStatusText().c_str());
	LogPrintf("coop nettest: build and game files match on both machines.\n");

	Lockstep aStep;
	aStep.Begin(&aSession, aSession.GetParams(),
		aIsHost ? PlayerId::Host : PlayerId::Client);

	// Mirrors the real game: a 100 Hz fixed tick, a fake simulation whose
	// state both sides must agree on, and input every half second.
	constexpr uint32_t TARGET_TICKS = 1000;   // 10 seconds
	uint32_t aState = 12345u;
	uint32_t aSlowestTickMs = 0;

	const auto aStart = std::chrono::steady_clock::now();
	auto aNextTickAt = aStart;

	while (aStep.GetCurrentTick() < TARGET_TICKS)
	{
		aSession.Update();

		if (!aSession.IsConnected())
		{
			LogPrintf("coop nettest: FAILED: %s\n", aSession.GetStatusText().c_str());
			return 1;
		}
		if (aStep.IsDesynced())
		{
			LogPrintf("coop nettest: FAILED: %s\n", aStep.GetDesyncText().c_str());
			return 1;
		}

		const auto aNow = std::chrono::steady_clock::now();
		if (aNow < aNextTickAt)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(200));
			continue;
		}

		if ((aStep.GetCurrentTick() % 50) == 0)
			aStep.QueueLocalCommand(CommandType::MouseDown,
				static_cast<int16_t>(aStep.GetCurrentTick() % 9),
				static_cast<int16_t>(aStep.GetCurrentTick() % 5), 1);

		const auto aTickStart = std::chrono::steady_clock::now();
		if (aStep.BeginTick())
		{
			aState = aState * 31u + aStep.GetCurrentTick();
			for (const Command& aCommand : aStep.GetTickCommands())
			{
				aState = aState * 31u + static_cast<uint32_t>(aCommand.mType);
				aState = aState * 31u + static_cast<uint32_t>(aCommand.mX);
				aState = aState * 31u + static_cast<uint32_t>(aCommand.mY);
				aState = aState * 31u + static_cast<uint32_t>(aCommand.mPlayer);
			}

			ChecksumBuilder aBuilder;
			aBuilder.Add(aState);
			aStep.EndTick(aBuilder.Get());

			aNextTickAt += std::chrono::milliseconds(10);

			const auto aTickMs = static_cast<uint32_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - aTickStart).count());
			if (aTickMs > aSlowestTickMs)
				aSlowestTickMs = aTickMs;
		}
		else if (aStep.ShouldShowWaiting())
		{
			// Throttled: this is called every spin of a stall, and one line
			// each buried the useful output under hundreds of near-identical
			// ones. Console writes are slow enough on Windows to make the
			// stall worse, too.
			LogPrintf("coop nettest: waiting for the other player (%u ms, delay %u)...\n",
				aStep.CurrentStallMsPublic(), aStep.GetInputDelay());
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}

		if (std::chrono::steady_clock::now() > aStart + std::chrono::seconds(90))
		{
			LogPrintf("coop nettest: FAILED: gave up at tick %u of %u.\n",
				aStep.GetCurrentTick(), TARGET_TICKS);
			return 1;
		}
	}

	const auto aElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - aStart).count();

	// In lockstep the two machines finish within a few ticks of each other,
	// not simultaneously. Whoever gets there first must keep the socket open
	// while the other chews through its last ticks -- hanging up immediately
	// shows up on the peer as "the other player disconnected" on an otherwise
	// perfect run. The real game needs the same courtesy at level end.
	const auto aLingerUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < aLingerUntil && aSession.IsConnected())
	{
		aSession.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	printf("\ncoop nettest: ran %u ticks in %lld ms\n",
		TARGET_TICKS, static_cast<long long>(aElapsedMs));
	LogPrintf("coop nettest: final state 0x%08X (must match on BOTH machines)\n", aState);
	LogPrintf("coop nettest: waited %u time(s) for the other player, %llu ms in total\n",
		aStep.GetStallEvents(), static_cast<unsigned long long>(aStep.GetTotalStallMs()));
	LogPrintf("coop nettest: slowest single tick %u ms\n", aSlowestTickMs);
	LogPrintf("coop nettest: input delay settled at %u ticks (%u ms of planting lag)\n",
		aStep.GetInputDelay(), aStep.GetInputDelay() * 10);

	// 10 seconds of ticks; a tenth of that spent waiting is a rough link.
	if (aStep.GetTotalStallMs() > 1000)
		LogPrintf("coop nettest: NOTE this link is jittery -- expect some input lag.\n");
	else
		LogPrintf("coop nettest: link looks good.\n");

	aSession.Disconnect("nettest done");
	LogPrintf("coop nettest: PASS\n");
	LogPrintf("\n(this output was also saved to %s)\n",
		aIsHost ? "coop-nettest-host.txt" : "coop-nettest-join.txt");

	if (gNetTestLog != nullptr)
	{
		fclose(gNetTestLog);
		gNetTestLog = nullptr;
	}
	return 0;
#endif
}

} // namespace Coop
