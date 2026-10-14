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

} // namespace Coop
