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

#include <algorithm>
#include <cstddef>

using namespace Sexy;

// Generated from assets/coop/*.png (see CMakeLists.txt).
extern const unsigned char gCoopArt_luca[];
extern const std::size_t gCoopArt_luca_size;
extern const unsigned char gCoopArt_cheese[];
extern const std::size_t gCoopArt_cheese_size;

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
	bool   gLoaded = false;

	void LoadArt()
	{
		if (gLoaded)
			return;
		gLoaded = true;
		gLucaImage = gLawnApp->GetImageFromMemory(gCoopArt_luca, gCoopArt_luca_size, "coop/luca.png");
		gCheeseImage = gLawnApp->GetImageFromMemory(gCoopArt_cheese, gCoopArt_cheese_size, "coop/cheese.png");
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
