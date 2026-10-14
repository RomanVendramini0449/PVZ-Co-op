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

	mLockstep.Begin(&mSession, mSession.GetParams(), mSession.GetLocalPlayer());
	mInGame = true;
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
}

bool EstablishSessionFromLaunchOptions()
{
	const LaunchOptions& aOptions = GetLaunchOptions();
	if (!aOptions.mWantHost && !aOptions.mWantJoin)
		return false;

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
		aSession.GetParams().mDifficulty == Difficulty::Hard ? "HARD" : "normal",
		aSession.GetParams().mRngSeed);
	fflush(stdout);
	return true;
}

} // namespace Coop
