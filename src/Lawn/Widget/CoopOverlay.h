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

#ifndef __COOPOVERLAY_H__
#define __COOPOVERLAY_H__

#include "widget/Widget.h"

class LawnApp;

// Every piece of co-op status text, drawn above everything else on screen.
//
// It lives in its own widget, kept at the front of the widget manager, because
// the things it reports happen on every screen -- the menu, the award, the
// store, the board, and over any dialog open on top of those. Drawn from inside
// any one of them, some art or some dialog always ends up on top of it, and
// "waiting for the other player" hidden behind a menu reads as a frozen game.
//
// It takes no input: the mouse passes straight through it.
class CoopOverlay : public Sexy::Widget
{
public:
	LawnApp* mApp;

public:
	explicit CoopOverlay(LawnApp* theApp);

	void Update() override;
	void Draw(Sexy::Graphics* g) override;

private:
	void DrawBanner(Sexy::Graphics* g, const std::string& theText, int theCenterY, const Sexy::Color& theColor);
};

#endif
