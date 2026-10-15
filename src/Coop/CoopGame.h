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

#ifndef __COOPGAME_H__
#define __COOPGAME_H__

#include "CoopLockstep.h"
#include "CoopProtocol.h"
#include "CoopSession.h"

namespace Coop
{

// The one place the game asks "are we in co-op, and if so what is going on".
// A single instance owns the connection and the tick scheduler; everything in
// Lawn/ talks to it through Get().
//
// IsInGame() is the gate used all over Board: it is false in single player, so
// every co-op branch costs one bool test and changes nothing.
class Game
{
private:
	Session  mSession;
	Lockstep mLockstep;
	bool     mInGame = false;

	// The level both machines are currently building or playing. On the host
	// this is filled in as it enters a level; on the client it arrives over
	// the wire.
	LevelSetup mLevelSetup;
	bool       mHasLevelSetup = false;
	// Host only: the epoch the next level will carry. Starts above zero so
	// a level is never confused with the "no level yet" state.
	uint32_t   mNextEpoch = 1;
	// The difficulty the host has chosen for upcoming levels. The handshake
	// value is only the starting point; the host can change it between
	// levels from the main menu.
	Difficulty mDifficulty = Difficulty::Normal;
	bool       mDifficultyOverridden = false;

public:
	static Game& Get();

	Session&  GetSession() { return mSession; }
	Lockstep& GetLockstep() { return mLockstep; }

	bool IsConnected() const { return mSession.IsConnected(); }
	// True only while a lockstepped level is actually running.
	bool IsInGame() const { return mInGame && mSession.IsConnected(); }

	PlayerId GetLocalPlayer() const { return mSession.GetLocalPlayer(); }
	PlayerId GetRemotePlayer() const { return mSession.GetRemotePlayer(); }
	int GetLocalPlayerIndex() const { return static_cast<int>(GetLocalPlayer()); }
	int GetRemotePlayerIndex() const { return static_cast<int>(GetRemotePlayer()); }

	bool IsHost() const { return mSession.IsHost(); }
	// Host: is the other player past their title screen and able to follow
	// us into a level? Starting one before that leaves them behind -- and
	// their title screen, when they do click through it, goes to the menu and
	// takes the level down with it.
	bool IsPeerReady() const { return mSession.IsPeerReady(); }
	// True on the machine that follows: it does not drive the campaign.
	bool IsFollower() const { return mSession.IsConnected() && !mSession.IsHost(); }

	// The live difficulty: what the current level was started with, which is
	// what both machines simulate. Outside a level, what the next one will use.
	Difficulty GetDifficulty() const
	{
		if (mHasLevelSetup)
			return mLevelSetup.mDifficulty;
		return mDifficultyOverridden ? mDifficulty : mSession.GetParams().mDifficulty;
	}
	// What the live difficulty does to sun, zombie health and zombie count.
	// Normal's rules (nothing changes) outside co-op.
	const DifficultyRules& GetRules() const
	{
		return GetDifficultyRules(IsConnected() ? GetDifficulty() : Difficulty::Normal);
	}
	// Host, between levels. Takes effect from the next level entered.
	void SetNextDifficulty(Difficulty theDifficulty) { mDifficulty = theDifficulty; mDifficultyOverridden = true; }
	Difficulty GetNextDifficulty() const { return mDifficultyOverridden ? mDifficulty : mSession.GetParams().mDifficulty; }

	// Host-driven campaign flow (SPEC.md section 3).
	//
	// The host announces each level it enters; the client builds the same one
	// instead of navigating its own menus. Without this the two machines are
	// simply two unrelated games that happen to share a socket.
	void HostEnterLevel(const LevelSetup& theSetup);
	void HostLeaveLevel();
	// Host: tell the client which screen we are on outside a level.
	void HostShowScreen(HostScreen theScreen, int32_t theArg, const LevelSetup& theProfile)
	{
		mSession.SendHostScreen(theScreen, theArg, theProfile);
	}
	// Client: the next step of the host's flow, in order. An EnterLevel event
	// also becomes the current level setup.
	bool PopFlowEvent(FlowEvent& theEventOut);

	bool              HasLevelSetup() const { return mHasLevelSetup; }
	const LevelSetup& GetLevelSetup() const { return mLevelSetup; }
	void              SetLevelSetup(const LevelSetup& theSetup);
	void              ClearLevelSetup() { mHasLevelSetup = false; }

	// Called when a lockstepped level starts and ends.
	void BeginLevel();
	void EndLevel();

	// Pumped from the game loop even outside a level, so handshakes progress.
	void Update();

private:
	void ReportStatsIfAsked();
public:
};

// Acts on --host / --join: opens the connection and blocks until the
// handshake finishes or it gives up, printing progress. Returns true when a
// session is ready, false when co-op was not requested or could not start.
bool EstablishSessionFromLaunchOptions();

} // namespace Coop

#endif
