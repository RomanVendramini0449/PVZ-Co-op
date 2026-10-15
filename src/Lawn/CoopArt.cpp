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

#include "CoopArt.h"

#include "../LawnApp.h"
#include "../PvzpLib/PvzpCommon.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "misc/SexyMatrix.h"

#include "../PvzpLib/Definition.h"
#include "../PvzpLib/Reanimator.h"
#include <cmath>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <unordered_map>

using namespace Sexy;

// Generated from assets/coop/*.png (see CMakeLists.txt).
extern const unsigned char gCoopArt_luca[];
extern const std::size_t gCoopArt_luca_size;
extern const unsigned char gCoopArt_cheese[];
extern const std::size_t gCoopArt_cheese_size;
extern const unsigned char gCoopArt_roman[];
extern const std::size_t gCoopArt_roman_size;
extern const unsigned char gCoopArt_yeti_spray[];
extern const std::size_t gCoopArt_yeti_spray_size;
extern const unsigned char gCoopArt_cursor_fill[];
extern const std::size_t gCoopArt_cursor_fill_size;
extern const unsigned char gCoopArt_cursor_outline[];
extern const std::size_t gCoopArt_cursor_outline_size;
extern const unsigned char gCoopArt_kernel_squash[];
extern const std::size_t gCoopArt_kernel_squash_size;
extern const unsigned char gCoopArt_philippe_pepper[];
extern const std::size_t gCoopArt_philippe_pepper_size;
extern const unsigned char gCoopArt_adventure_plus_background[];
extern const std::size_t gCoopArt_adventure_plus_background_size;
extern const unsigned char gCoopArt_adventure_plus_narrator[];
extern const std::size_t gCoopArt_adventure_plus_narrator_size;
extern const unsigned char gCoopArt_adventure_plus_night_background[];
extern const std::size_t gCoopArt_adventure_plus_night_background_size;
struct CoopArtPart { const char* mName; const unsigned char* mData; std::size_t mSize; };
extern const CoopArtPart gCoopZombieParts[];
extern const int gCoopZombiePartCount;

namespace
{
	// The cheese is drawn a little over half its art size: a wedge a bit
	// bigger than a pea.
	constexpr float CHEESE_SCALE = 0.65f;

	// Luca's height on the lawn, in a plant's 80x80 frame: a touch taller than
	// a Peashooter, standing on the same spot.
	constexpr float LUCA_HEIGHT = 86.0f;
	constexpr float LUCA_BASE_X = 40.0f;
	constexpr float LUCA_BASE_Y = 79.0f;

	// Never freed: they live as long as the game, and the renderer they
	// belong to is torn down at exit regardless.
	Image* gLucaImage = nullptr;
	Image* gCheeseImage = nullptr;
	Image* gRomanImage = nullptr;
	Image* gYetiSprayImage = nullptr;
	Image* gCursorFillImage = nullptr;
	Image* gCursorOutlineImage = nullptr;
	Image* gKernelSquashImage = nullptr;
	Image* gPhilippePepperImage = nullptr;
	Image* gAdventurePlusBackground = nullptr;
	Image* gAdventurePlusNightBackground = nullptr;
	Image* gAdventurePlusNarrator = nullptr;
	bool   gLoaded = false;

	// The arrow's tip, in the cursor art (assets/coop/cursor_*.png).
	constexpr float CURSOR_TIP_X = 3.0f;
	constexpr float CURSOR_TIP_Y = 3.0f;

	// The skins' height on the lawn, standing where the plant they replace
	// stands: Kernel a touch taller than a Squash's body, Philippe about a
	// Jalapeno's (his art is drawn on a slant, so it is nearly square).
	constexpr float KERNEL_HEIGHT = 84.0f;
	constexpr float PHILIPPE_HEIGHT = 78.0f;

	void LoadArt()
	{
		if (gLoaded)
			return;
		gLoaded = true;
		gLucaImage = gLawnApp->GetImageFromMemory(gCoopArt_luca, gCoopArt_luca_size, "coop/luca.png");
		gCheeseImage = gLawnApp->GetImageFromMemory(gCoopArt_cheese, gCoopArt_cheese_size, "coop/cheese.png");
		gRomanImage = gLawnApp->GetImageFromMemory(gCoopArt_roman, gCoopArt_roman_size, "coop/roman.png");
		gYetiSprayImage = gLawnApp->GetImageFromMemory(gCoopArt_yeti_spray, gCoopArt_yeti_spray_size, "coop/yeti_spray.png");
		gCursorFillImage = gLawnApp->GetImageFromMemory(gCoopArt_cursor_fill, gCoopArt_cursor_fill_size, "coop/cursor_fill.png");
		gCursorOutlineImage = gLawnApp->GetImageFromMemory(gCoopArt_cursor_outline, gCoopArt_cursor_outline_size, "coop/cursor_outline.png");
		gKernelSquashImage = gLawnApp->GetImageFromMemory(gCoopArt_kernel_squash, gCoopArt_kernel_squash_size, "coop/kernel_squash.png");
		gPhilippePepperImage = gLawnApp->GetImageFromMemory(gCoopArt_philippe_pepper, gCoopArt_philippe_pepper_size, "coop/philippe_pepper.png");
		if (gCoopArt_adventure_plus_narrator_size > 0)
		{
			gAdventurePlusNarrator = gLawnApp->GetImageFromMemory(gCoopArt_adventure_plus_narrator,
				gCoopArt_adventure_plus_narrator_size, "coop/adventure_plus_narrator.png");
		}
		if (gCoopArt_adventure_plus_night_background_size > 0)
		{
			gAdventurePlusNightBackground = gLawnApp->GetImageFromMemory(gCoopArt_adventure_plus_night_background,
				gCoopArt_adventure_plus_night_background_size, "coop/adventure_plus_night_background.png");
		}
		if (gCoopArt_adventure_plus_background_size > 0)
		{
			gAdventurePlusBackground = gLawnApp->GetImageFromMemory(gCoopArt_adventure_plus_background,
				gCoopArt_adventure_plus_background_size, "coop/adventure_plus_background.png");
		}
	}

	// A picture standing in for a plant: theHeight tall at scale 1, its base
	// at the middle of the bottom of the plant's 80x80 frame.
	void DrawStandIn(Graphics* g, Image* theImage, float theHeight, float theX, float theY,
		float theScaleX, float theScaleY, float theSquashX, float theSquashY, int theGlow)
	{
		if (theImage == nullptr)
			return;

		const float aImageScale = theHeight / static_cast<float>(theImage->mHeight);
		const float aScaleX = aImageScale * theScaleX * theSquashX;
		const float aScaleY = aImageScale * theScaleY * theSquashY;

		// Anchored at the base, so a squash bends it about where it stands.
		const float aBaseX = theX + 40.0f * theScaleX;
		const float aBaseY = theY + 79.0f * theScaleY;
		const float aLeft = aBaseX - theImage->mWidth * aScaleX * 0.5f;
		const float aTop = aBaseY - theImage->mHeight * aScaleY;

		Graphics aG(*g);
		aG.SetLinearBlend(true);
		PvzpDrawImageScaledF(&aG, theImage, aLeft, aTop, aScaleX, aScaleY);

		if (theGlow > 0)
		{
			// A plant's glow is the plant drawn again, additively.
			aG.SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
			aG.SetColorizeImages(true);
			aG.SetColor(Color(255, 255, 255, std::min(theGlow, 255)));
			PvzpDrawImageScaledF(&aG, theImage, aLeft, aTop, aScaleX, aScaleY);
		}
	}
}

Image* CoopArt::LucaImage()
{
	LoadArt();
	return gLucaImage;
}

Image* CoopArt::CheeseImage()
{
	LoadArt();
	return gCheeseImage;
}

void CoopArt::DrawLuca(Graphics* g, float theX, float theY, float theScaleX, float theScaleY,
	float theSquashX, float theSquashY, int theGlow)
{
	Image* anImage = LucaImage();
	if (anImage == nullptr)
		return;

	const float aImageScale = LUCA_HEIGHT / static_cast<float>(anImage->mHeight);
	const float aScaleX = aImageScale * theScaleX * theSquashX;
	const float aScaleY = aImageScale * theScaleY * theSquashY;

	// Anchored at the base, so a squash bends it about where it stands.
	const float aBaseX = theX + LUCA_BASE_X * theScaleX;
	const float aBaseY = theY + LUCA_BASE_Y * theScaleY;
	const float aLeft = aBaseX - anImage->mWidth * aScaleX * 0.5f;
	const float aTop = aBaseY - anImage->mHeight * aScaleY;

	Graphics aG(*g);
	aG.SetLinearBlend(true);
	PvzpDrawImageScaledF(&aG, anImage, aLeft, aTop, aScaleX, aScaleY);

	if (theGlow > 0)
	{
		// The Sunflower's glow is the plant drawn again, additively.
		aG.SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
		aG.SetColorizeImages(true);
		aG.SetColor(Color(255, 255, 255, std::min(theGlow, 255)));
		PvzpDrawImageScaledF(&aG, anImage, aLeft, aTop, aScaleX, aScaleY);
	}
}

void CoopArt::DrawCheese(Graphics* g, float theCenterX, float theCenterY, float theRotation)
{
	Image* anImage = CheeseImage();
	if (anImage == nullptr)
		return;

	Graphics aG(*g);
	aG.SetLinearBlend(true);

	// Rotated about its centre: the matrix places the image's centre.
	SexyMatrix3 aTransform;
	PvzpScaleRotateTransformMatrix(aTransform, theCenterX + g->mTransX, theCenterY + g->mTransY,
		theRotation, CHEESE_SCALE, CHEESE_SCALE);
	const Rect aSrcRect(0, 0, anImage->mWidth, anImage->mHeight);
	const Color& aColor = aG.mColorizeImages ? aG.mColor : Color::White;
	PvzpBltMatrix(&aG, anImage, aTransform, aG.mClipRect, aColor, aG.mDrawMode, aSrcRect);
}

Image* CoopArt::RomanImage()
{
	LoadArt();
	return gRomanImage;
}

void CoopArt::DrawRoman(Graphics* g, float theCenterX, float theBottomY, float theHeight, const Color& theGlow, int theAlpha)
{
	Image* anImage = RomanImage();
	if (anImage == nullptr || theAlpha <= 0)
		return;

	const float aScale = theHeight / static_cast<float>(anImage->mHeight);
	const float aLeft = theCenterX - anImage->mWidth * aScale * 0.5f;
	const float aTop = theBottomY - anImage->mHeight * aScale;

	Graphics aG(*g);
	aG.SetLinearBlend(true);
	if (theAlpha < 255)
	{
		aG.SetColorizeImages(true);
		aG.SetColor(Color(255, 255, 255, theAlpha));
	}
	PvzpDrawImageScaledF(&aG, anImage, aLeft, aTop, aScale, aScale);

	if (theGlow.mAlpha > 0)
	{
		aG.SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
		aG.SetColorizeImages(true);
		aG.SetColor(Color(theGlow.mRed, theGlow.mGreen, theGlow.mBlue, theGlow.mAlpha * theAlpha / 255));
		PvzpDrawImageScaledF(&aG, anImage, aLeft, aTop, aScale, aScale);
	}
}

void CoopArt::DrawCursor(Graphics* g, float theX, float theY, const Color& theColor)
{
	LoadArt();
	if (gCursorFillImage == nullptr || gCursorOutlineImage == nullptr)
		return;

	Graphics aG(*g);
	aG.SetLinearBlend(true);
	aG.SetDrawMode(Graphics::DRAWMODE_NORMAL);
	const float aLeft = theX - CURSOR_TIP_X;
	const float aTop = theY - CURSOR_TIP_Y;
	aG.SetColorizeImages(false);
	aG.DrawImageF(gCursorOutlineImage, aLeft, aTop);
	// The fill is white; colourising tints it to the player's colour.
	aG.SetColorizeImages(true);
	aG.SetColor(Color(theColor.mRed, theColor.mGreen, theColor.mBlue, 255));
	aG.DrawImageF(gCursorFillImage, aLeft, aTop);
}

static Image* SkinImage(CoopArt::Skin theSkin)
{
	return theSkin == CoopArt::Skin::KernelSquash ? gKernelSquashImage : gPhilippePepperImage;
}

void CoopArt::DrawSkin(Graphics* g, Skin theSkin, float theX, float theY, float theScaleX, float theScaleY,
	float theSquashX, float theSquashY, int theGlow)
{
	LoadArt();
	const float aHeight = theSkin == Skin::KernelSquash ? KERNEL_HEIGHT : PHILIPPE_HEIGHT;
	DrawStandIn(g, SkinImage(theSkin), aHeight, theX, theY, theScaleX, theScaleY, theSquashX, theSquashY, theGlow);
}

void CoopArt::DrawSkinIcon(Graphics* g, Skin theSkin, float theCenterX, float theBottomY, float theHeight)
{
	LoadArt();
	Image* anImage = SkinImage(theSkin);
	if (anImage == nullptr)
		return;

	// The caller's mode and colour, so the shop's hover highlight works.
	const float aScale = theHeight / static_cast<float>(anImage->mHeight);
	Graphics aG(*g);
	aG.SetLinearBlend(true);
	PvzpDrawImageScaledF(&aG, anImage, theCenterX - anImage->mWidth * aScale * 0.5f, theBottomY - theHeight, aScale, aScale);
}

Image* CoopArt::AdventurePlusBackground(bool theNight)
{
	LoadArt();
	// No night picture yet: the game's own night lawn, not the day board.
	return theNight ? gAdventurePlusNightBackground : gAdventurePlusBackground;
}

Image* CoopArt::SubstituteZombiePart(Image* theOriginal)
{
	if (gCoopZombiePartCount == 0 || gLawnApp == nullptr || !gLawnApp->IsAdventurePlusLevel() || gLawnApp->mBoard == nullptr)
		return nullptr;

	// Built on first use: each part file is named after the image it
	// replaces ("Zombie_head.png" replaces IMAGE_REANIM_ZOMBIE_HEAD), so ask
	// for that image the way the animations do, and remember the pair.
	static std::unordered_map<Image*, Image*> sParts;
	static bool sBuilt = false;
	if (!sBuilt)
	{
		sBuilt = true;
		for (int i = 0; i < gCoopZombiePartCount; i++)
		{
			const CoopArtPart& aPart = gCoopZombieParts[i];
			std::string anId = "IMAGE_REANIM_";
			for (const char* aChar = aPart.mName; *aChar; aChar++)
				anId.push_back(static_cast<char>(toupper(static_cast<unsigned char>(*aChar))));
			Image* anOriginal = nullptr;
			if (!DefinitionLoadImage(&anOriginal, anId) || anOriginal == nullptr)
				continue;
			Image* aReplacement = gLawnApp->GetImageFromMemory(aPart.mData, aPart.mSize, std::string("coop/zombie-parts/") + aPart.mName);
			if (aReplacement != nullptr)
				sParts[anOriginal] = aReplacement;
		}
	}

	const auto anIt = sParts.find(theOriginal);
	return anIt == sParts.end() ? nullptr : anIt->second;
}

Image* CoopArt::AdventurePlusNarrator()
{
	LoadArt();
	return gAdventurePlusNarrator;
}

void CoopArt::DrawNarrator(Graphics* g, Reanimation* theDaveReanim, Image* theImage, bool theTalking, unsigned int theCounter)
{
	// Dave's body, at rest, stands at x -56 in his animation; it starts far
	// off to the left and slides in, and slides back out to leave. Following
	// it moves the stand-in in and out on the same beats.
	const int aBodyTrack = theDaveReanim->FindTrackIndex("Dave_body1");
	if (aBodyTrack < 0)
		return;
	ReanimatorTransform aBody;
	theDaveReanim->GetCurrentTransform(aBodyTrack, &aBody);
	const float aSlide = aBody.mTransX + 56.0f;

	// About Dave's size: standing on the bottom of the screen, left of the
	// speech bubble.
	constexpr float HEIGHT = 470.0f;
	constexpr float CENTER_X = 150.0f;
	const float aScale = HEIGHT / static_cast<float>(theImage->mHeight);
	float aSquashX = 1.0f, aSquashY = 1.0f;
	if (theTalking)
	{
		const float aWave = static_cast<float>(sin(theCounter * 0.35));
		aSquashX = 1.0f + 0.012f * aWave;
		aSquashY = 1.0f - 0.018f * aWave;
	}
	const float aWidth = theImage->mWidth * aScale * aSquashX;
	const float aHeight = theImage->mHeight * aScale * aSquashY;

	Graphics aG(*g);
	aG.SetLinearBlend(true);
	PvzpDrawImageScaledF(&aG, theImage, CENTER_X + aSlide - aWidth * 0.5f, 600.0f - aHeight, aScale * aSquashX, aScale * aSquashY);
}

void CoopArt::DrawYetiSpray(Graphics* g, float theCenterX, float theBottomY, float theHeight)
{
	LoadArt();
	Image* anImage = gYetiSprayImage;
	if (anImage == nullptr)
		return;

	// Drawn with the caller's mode and colour, so the shop's hover highlight
	// (an additive second pass) works on it like on any other item.
	const float aScale = theHeight / static_cast<float>(anImage->mHeight);
	Graphics aG(*g);
	aG.SetLinearBlend(true);
	PvzpDrawImageScaledF(&aG, anImage, theCenterX - anImage->mWidth * aScale * 0.5f, theBottomY - theHeight, aScale, aScale);
}
