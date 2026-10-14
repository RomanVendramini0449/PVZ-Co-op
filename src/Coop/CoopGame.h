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

	Difficulty GetDifficulty() const { return mSession.GetParams().mDifficulty; }
	bool IsHardMode() const { return GetDifficulty() == Difficulty::Hard; }

	// Called when a lockstepped level starts and ends.
	void BeginLevel();
	void EndLevel();

	// Pumped from the game loop even outside a level, so handshakes progress.
	void Update();
};

} // namespace Coop

#endif
