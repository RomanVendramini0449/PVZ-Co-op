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

#include "CoopProtocol.h"

#include <cstring>

namespace Coop
{

const char* GetRejectReasonText(RejectReason theReason)
{
	switch (theReason)
	{
	case RejectReason::ProtocolMismatch:
		return "You are running a different version of the co-op mod.";
	case RejectReason::BuildMismatch:
		return "You are running a different build. Co-op needs the exact same build on both machines.";
	case RejectReason::AssetMismatch:
		return "Your game files differ from the host's. Both players need the same version of the game.";
	case RejectReason::AlreadyConnected:
		return "That game already has two players.";
	case RejectReason::None:
	default:
		return "Connection refused.";
	}
}

static const DifficultyRules gDifficultyRules[DIFFICULTY_COUNT] = {
	// name          key            summary                                                              sun   health    count recharge
	{ "Normal",     "normal",     "the game as it comes",                                                 1, 1, 100, 100, 100, 100 },
	{ "Hard",       "hard",       "each sun worth 15, zombies 1.2x tougher",                              3, 5, 120, 120, 100, 100 },
	{ "Wild",       "wild",       "normal sun, every zombie 1x to 2x tougher",                            1, 1, 100, 200, 100, 100 },
	{ "Horde",      "horde",      "1.5x the zombies, each 1x to 2x tougher",                              1, 1, 100, 200, 150, 100 },
	{ "Nightmare",  "nightmare",  "sun worth 15, 1.5x the zombies, each 1.2x to 2x tougher",              3, 5, 120, 200, 150, 100 },
	{ "Inferno",    "inferno",    "sun worth 15, 2x the zombies, each 1.5x to 2.5x tougher, slow recharge", 3, 5, 150, 250, 200, 125 },
	{ "Apocalypse", "apocalypse", "sun worth 10, 2.5x the zombies, each 2x to 3x tougher, slower recharge", 2, 5, 200, 300, 250, 150 },
};

const DifficultyRules& GetDifficultyRules(Difficulty theDifficulty)
{
	const int aIndex = static_cast<int>(theDifficulty);
	if (aIndex < 0 || aIndex >= DIFFICULTY_COUNT)
		return gDifficultyRules[0];
	return gDifficultyRules[aIndex];
}

Difficulty GetNextDifficultyInCycle(Difficulty theDifficulty)
{
	const int aNext = (static_cast<int>(theDifficulty) + 1) % DIFFICULTY_COUNT;
	return static_cast<Difficulty>(aNext);
}

bool ParseDifficulty(const char* theKey, Difficulty& theDifficultyOut)
{
	for (int i = 0; i < DIFFICULTY_COUNT; i++)
	{
		if (std::strcmp(theKey, gDifficultyRules[i].mKey) == 0)
		{
			theDifficultyOut = static_cast<Difficulty>(i);
			return true;
		}
	}
	return false;
}

} // namespace Coop
