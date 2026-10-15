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

#include "CoopGame.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <thread>

namespace Coop
{

Game& Game::Get()
{
	static Game sGame;
	return sGame;
}

void Game::BeginLevel()
{
	if (!mSession.IsConnected())
		return;

	mLockstep.Begin(&mSession, mSession.GetParams(), mSession.GetLocalPlayer(),
		mHasLevelSetup ? mLevelSetup.mEpoch : 0);
	mInGame = true;
}

void Game::SetLevelSetup(const LevelSetup& theSetup)
{
	mLevelSetup = theSetup;
	mHasLevelSetup = true;
}

void Game::HostEnterLevel(const LevelSetup& theSetup)
{
	LevelSetup aSetup = theSetup;
	aSetup.mEpoch = mNextEpoch++;
	aSetup.mDifficulty = GetNextDifficulty();
	SetLevelSetup(aSetup);
	mSession.SendEnterLevel(aSetup);
}

void Game::HostLeaveLevel()
{
	mHasLevelSetup = false;
	mSession.SendLeaveLevel();
}

bool Game::PopFlowEvent(FlowEvent& theEventOut)
{
	if (!mSession.PopFlowEvent(theEventOut))
		return false;
	if (theEventOut.mKind == FlowEvent::Kind::EnterLevel)
		SetLevelSetup(theEventOut.mSetup);
	return true;
}

void Game::EndLevel()
{
	mInGame = false;
	mLockstep.Reset();
}

void Game::Update()
{
	mSession.Update();

	// Losing the peer mid-level ends the lockstepped portion immediately;
	// continuing to simulate against a vanished player would just build up
	// divergence to no purpose.
	if (mInGame && !mSession.IsConnected())
		EndLevel();

	ReportStatsIfAsked();
}

// Diagnostics for smoothness complaints. With PVZ_COOP_STATS set, prints how
// often and how long the simulation waited on the other machine, every five
// seconds of a level. Every wait is a visible hitch, so this turns "it
// stutters" into a number that can be compared between builds and links.
void Game::ReportStatsIfAsked()
{
	static const bool sEnabled = getenv("PVZ_COOP_STATS") != nullptr;
	if (!sEnabled || !IsInGame())
		return;

	static auto sLastReport = std::chrono::steady_clock::now();
	static uint32_t sLastEvents = 0;
	static uint64_t sLastStallMs = 0;
	static uint32_t sLastTick = 0;
	const auto aNow = std::chrono::steady_clock::now();
	if (aNow - sLastReport < std::chrono::seconds(5))
		return;

	const uint32_t aEvents = mLockstep.GetStallEvents();
	const uint64_t aStallMs = mLockstep.GetTotalStallMs();
	const uint32_t aTick = mLockstep.GetCurrentTick();
	// A new level resets the lockstep's counters; start the deltas over.
	if (aEvents < sLastEvents || aTick < sLastTick)
	{
		sLastEvents = 0;
		sLastStallMs = 0;
		sLastTick = 0;
	}

	const double aSeconds = std::chrono::duration<double>(aNow - sLastReport).count();
	printf("co-op stats: tick %u (+%u in %.1fs = %.0f/s), delay %u, waits +%u, waited +%llu ms\n",
		aTick, aTick - sLastTick, aSeconds, (aTick - sLastTick) / aSeconds,
		mLockstep.GetInputDelay(), aEvents - sLastEvents,
		static_cast<unsigned long long>(aStallMs - sLastStallMs));
	fflush(stdout);

	sLastReport = aNow;
	sLastEvents = aEvents;
	sLastStallMs = aStallMs;
	sLastTick = aTick;
}

bool EstablishSessionFromLaunchOptions()
{
	const LaunchOptions& aOptions = GetLaunchOptions();
	if (!aOptions.mWantHost && !aOptions.mWantJoin)
	{
		// Single player: --difficulty still applies, to this one game.
		if (aOptions.mDifficulty != Difficulty::Normal)
			Game::Get().SetNextDifficulty(aOptions.mDifficulty);
		return false;
	}

	Session& aSession = Game::Get().GetSession();

	if (aOptions.mWantHost)
	{
		printf("co-op: hosting on port %u...\n", aOptions.mPort);
		fflush(stdout);
		if (!aSession.StartHost(aOptions.mPort, aOptions.mDifficulty, 1))
		{
			printf("co-op: %s\n", aSession.GetStatusText().c_str());
			return false;
		}

		const std::vector<std::string> aAddresses = Socket::GetLocalAddresses();
		if (!aAddresses.empty())
		{
			printf("\n==================================================\n");
			printf(" Tell the other player to connect to:\n");
			for (const std::string& aAddress : aAddresses)
				printf("     %s:%u\n", aAddress.c_str(), aOptions.mPort);
			printf("==================================================\n\n");
		}
		printf("co-op: waiting for the other player to join...\n");
		fflush(stdout);
	}
	else
	{
		printf("co-op: connecting to %s:%u...\n", aOptions.mJoinHost.c_str(), aOptions.mPort);
		fflush(stdout);
		if (!aSession.StartJoin(aOptions.mJoinHost, aOptions.mPort))
		{
			printf("co-op: %s\n", aSession.GetStatusText().c_str());
			return false;
		}
	}

	// Blocking here is deliberate: the game has not started yet, and both
	// machines must agree on the seed and difficulty before anything that
	// uses them runs.
	const auto aDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
	while (!aSession.IsConnected())
	{
		aSession.Update();
		if (aSession.GetState() == Session::State::Failed)
		{
			printf("co-op: %s\n", aSession.GetStatusText().c_str());
			return false;
		}
		if (std::chrono::steady_clock::now() > aDeadline)
		{
			printf("co-op: gave up waiting for the other player.\n");
			aSession.Disconnect("timed out");
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	printf("co-op: connected. %s\n", aSession.GetStatusText().c_str());
	printf("co-op: difficulty %s, shared seed %u\n",
		GetDifficultyRules(aSession.GetParams().mDifficulty).mName,
		aSession.GetParams().mRngSeed);
	fflush(stdout);
	return true;
}

} // namespace Coop
