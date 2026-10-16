/*
 * Copyright (C) 2026 PVZ-Co-op contributors
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PVZ-Co-op, a two-player co-op mod of PvZ-Portable.
 */

#ifndef __COOPPACKED_H__
#define __COOPPACKED_H__

#include <cstddef>

// The Adventure Plus art and sounds prepared by
// tools/zombie-art/pack_coop_assets.py, built into the executable by the
// CoopPackedData.cpp it generates. Both tables end with a null name.
struct CoopPackedFile
{
	const char*          mName;
	const unsigned char* mData;
	std::size_t          mSize;
};

// A sprite sheet: mCount frames of mFrameW x mFrameH in rows of mCols. Each
// frame is the original artwork's (mCropX, mCropY) corner onward, scaled by
// mScale; (mAnchorX, mAnchorY) is, in the original artwork, the point that
// stands on the ground.
struct CoopSheetMeta
{
	const char* mName;
	int         mFrameW;
	int         mFrameH;
	int         mCols;
	int         mCount;
	float       mScale;
	int         mCropX;
	int         mCropY;
	int         mAnchorX;
	int         mAnchorY;
};

extern const CoopPackedFile gCoopPackedFiles[];
extern const CoopSheetMeta gCoopSheetMetas[];

#endif
