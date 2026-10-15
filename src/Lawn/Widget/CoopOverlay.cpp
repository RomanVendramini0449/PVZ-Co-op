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

#include "CoopOverlay.h"

#include "../../LawnApp.h"
#include "../../Resources.h"
#include "../../Coop/CoopGame.h"
#include "../Board.h"
#include "../Cutscene.h"
#include <SDL.h>
#include "graphics/Font.h"
#include "graphics/Graphics.h"

using namespace Sexy;

CoopOverlay::CoopOverlay(LawnApp* theApp)
{
	mApp = theApp;
	// Purely a display: the mouse must fall through to whatever is beneath.
	mMouseVisible = false;
	mHasAlpha = true;
	mHasTransparencies = true;
}

void CoopOverlay::Update()
{
	Widget::Update();
	// Cheap, and the alternative is a status line that lags what it reports.
	MarkDirty();
}

void CoopOverlay::DrawBanner(Graphics* g, const std::string& theText, int theCenterY, const Color& theColor)
{
	if (theText.empty())
		return;

	_Font* aFont = Sexy::FONT_BRIANNETOD16;
	const int aWidth = aFont->StringWidth(theText);
	const int aX = (mWidth - aWidth) / 2;
	const int aHeight = aFont->GetHeight();

	g->SetColor(Color(0, 0, 0, 170));
	g->FillRect(aX - 12, theCenterY - aHeight / 2 - 4, aWidth + 24, aHeight + 8);
	g->SetFont(aFont);
	g->SetColor(theColor);
	g->DrawString(theText, aX, theCenterY + aFont->GetAscent() / 2 - 2);
}

void CoopOverlay::Draw(Graphics* g)
{
	Coop::Game& aCoop = Coop::Game::Get();
	if (!aCoop.IsConnected())
		return;

	const Color aNormal(255, 245, 200);
	const Color aWarning(255, 160, 160);

	// A level that was restarted because the games fell out of sync says so
	// for a few seconds, rather than silently starting over.
	if (mApp->mCoopResyncNoticeUntil != 0 && SDL_GetTicks() < mApp->mCoopResyncNoticeUntil)
		DrawBanner(g, "The games fell out of sync, so the level was restarted.", 160, Color(255, 160, 160));

	// In a level: say so whenever the simulation is not moving. Without this a
	// stall and a desync both look exactly like the game freezing.
	if (aCoop.IsInGame())
	{
		// The other player's cursor, above everything (SPEC.md section 7).
		// It lives in board coordinates; the board can be panned (the intro)
		// or shaking, so carry its position across.
		if (mApp->mBoard != nullptr)
		{
			Graphics aBoardSpace(*g);
			aBoardSpace.mTransX += static_cast<float>(mApp->mBoard->mX);
			aBoardSpace.mTransY += static_cast<float>(mApp->mBoard->mY);
			mApp->mBoard->DrawRemoteCursor(&aBoardSpace);
		}

		Coop::Lockstep& aLockstep = aCoop.GetLockstep();
		if (aLockstep.IsDesynced())
		{
			std::string aText = aLockstep.GetDesyncText();
			if (aText.empty())
				aText = "The two games have fallen out of sync.";
			DrawBanner(g, aText, 128, aWarning);
		}
		else if (aLockstep.ShouldShowWaiting())
		{
			DrawBanner(g, "Waiting for the other player...", 128, aNormal);
		}
		else if (aCoop.IsFollower() && mApp->GetDialog(Dialogs::DIALOG_GAME_OVER))
		{
			DrawBanner(g, "Waiting for the host to try again...", mHeight - 22, aNormal);
		}
		else if (aCoop.IsFollower() && mApp->mSeedChooserScreen && mApp->mBoard
			&& mApp->mBoard->mCutScene && mApp->mBoard->mCutScene->mSeedChoosing)
		{
			DrawBanner(g, "Pick seeds together. The host presses Let's Rock!", mHeight - 22, aNormal);
		}
		return;
	}

	if (mApp->mBoard != nullptr)
		return;  // a level being torn down or built; nothing useful to say

	const Coop::Difficulty aDifficulty = aCoop.IsFollower() ? mApp->mCoopShownDifficulty : aCoop.GetNextDifficulty();
	const Coop::DifficultyRules& aRules = Coop::GetDifficultyRules(aDifficulty);
	const bool aDifficultyHard = aDifficulty != Coop::Difficulty::Normal;
	const std::string aDifficultyText = aDifficultyHard
		? std::string("Difficulty: ") + aRules.mName + "  (" + aRules.mSummary + ")"
		: "Difficulty: Normal";

	if (aCoop.IsFollower())
	{
		// The joining player goes wherever the host goes. Each of these
		// screens is the host's to leave, so say what is being waited on.
		if (mApp->GetDialog(Dialogs::DIALOG_STORE))
			DrawBanner(g, "The host is shopping. Money and upgrades are shared.", mHeight - 22, aNormal);
		else if (mApp->mAwardScreen)
			DrawBanner(g, "Waiting for the host to continue...", mHeight - 22, aNormal);
		else if (mApp->mCreditScreen)
			return;
		else if (mApp->mGameSelector)
		{
			DrawBanner(g, "Connected. Waiting for the host to start a level...", mHeight - 50, aNormal);
			DrawBanner(g, aDifficultyText, mHeight - 22, aDifficultyHard ? aWarning : aNormal);
		}
		return;
	}

	// The host, on the main menu: confirm the other player is here, and what
	// the next level will be played at.
	if (mApp->mGameSelector && !mApp->mAwardScreen && !mApp->mCreditScreen && !mApp->GetDialog(Dialogs::DIALOG_STORE))
	{
		DrawBanner(g, aCoop.IsPeerReady()
			? "Co-op: the other player is connected"
			: "Co-op: waiting for the other player to reach the menu...", mHeight - 50, aNormal);
		DrawBanner(g, aDifficultyText, mHeight - 22, aDifficultyHard ? aWarning : aNormal);
	}
}
