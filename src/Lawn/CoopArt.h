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

#ifndef __COOPART_H__
#define __COOPART_H__

namespace Sexy
{
	class Color;
	class Graphics;
	class Image;
}
class Reanimation;

// The mod's own art: the Luca plant and the cheese it shoots. The PNGs live in
// assets/coop and are compiled into the executable (see CMakeLists.txt), so
// nothing has to be copied next to the game.
//
// Both are stored larger than they are drawn on the lawn, so they stay smooth
// when the award screen draws them large.
namespace CoopArt
{
	// Built on first use, once the renderer exists; nullptr if one failed to
	// decode, and every caller copes with that by drawing nothing.
	Sexy::Image* LucaImage();
	Sexy::Image* CheeseImage();

	// Luca standing in a plant's 80x80 frame whose top-left is (theX, theY),
	// the frame scaled by theScaleX/Y (a seed packet draws plants at half
	// size). theSquashX/Y bend it about its base for the idle sway and the
	// shooting recoil. theGlow (0-255) is the brightening a Sunflower shows
	// as it makes sun.
	void DrawLuca(Sexy::Graphics* g, float theX, float theY, float theScaleX, float theScaleY,
		float theSquashX = 1.0f, float theSquashY = 1.0f, int theGlow = 0);

	// The cheese, centred on (theCenterX, theCenterY), turned by theRotation.
	void DrawCheese(Sexy::Graphics* g, float theCenterX, float theCenterY, float theRotation);

	// Roman, the boss of Luca vs. Roman, standing on (theCenterX, theBottomY)
	// and theHeight tall. theGlow is drawn over him additively; theAlpha
	// fades him in.
	Sexy::Image* RomanImage();

	// The Yeti Spray can, standing on (theCenterX, theBottomY), theHeight tall.
	void DrawYetiSpray(Sexy::Graphics* g, float theCenterX, float theBottomY, float theHeight);
	void DrawRoman(Sexy::Graphics* g, float theCenterX, float theBottomY, float theHeight,
		const Sexy::Color& theGlow, int theAlpha);

	// The other player's pointer: one arrow, its tip on (theX, theY), filled
	// with their colour inside a dark outline.
	void DrawCursor(Sexy::Graphics* g, float theX, float theY, const Sexy::Color& theColor);

	// Shop skins (bought from Crazy Dave; purely a look). Kernel stands in for
	// the Squash, Philippe for the Jalapeno. Drawn like Luca: in a plant's
	// 80x80 frame whose top-left is (theX, theY), scaled by theScaleX/Y, and
	// bent about the base by theSquashX/Y. theGlow brightens it (0-255).
	enum class Skin { KernelSquash, PhilippePepper };
	void DrawSkin(Sexy::Graphics* g, Skin theSkin, float theX, float theY, float theScaleX, float theScaleY,
		float theSquashX = 1.0f, float theSquashY = 1.0f, int theGlow = 0);
	// The skin at shop size, standing on (theCenterX, theBottomY).
	void DrawSkinIcon(Sexy::Graphics* g, Skin theSkin, float theCenterX, float theBottomY, float theHeight);

	// Adventure Plus's own lawn, 1400x600 like the day lawn it replaces;
	// nullptr until assets/coop/adventure_plus_background.png exists. The
	// night one (the boss battle) is adventure_plus_night_background.png.
	Sexy::Image* AdventurePlusBackground(bool theNight);

	// Adventure Plus: the repainted zombie or Crazy Dave part
	// (assets/coop/zombie-parts, assets/coop/dave-parts) to
	// draw in place of theOriginal, or nullptr to draw it as it is. Always
	// nullptr outside an Adventure Plus level, and cheap when there are none.
	Sexy::Image* SubstituteZombiePart(Sexy::Image* theOriginal);

	// Adventure Plus's stand-in for Crazy Dave (assets/coop/
	// adventure_plus_narrator.png); nullptr while there is none.
	Sexy::Image* AdventurePlusNarrator();
	// Draws it where Dave's (hidden) animation has him, bobbing while he talks.
	void DrawNarrator(Sexy::Graphics* g, Reanimation* theDaveReanim, Sexy::Image* theImage, bool theTalking, unsigned int theCounter);
}

#endif
