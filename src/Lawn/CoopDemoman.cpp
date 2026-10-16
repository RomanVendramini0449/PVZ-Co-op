/*
 * Copyright (C) 2026 PVZ-Co-op contributors
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PVZ-Co-op, a two-player co-op mod of PvZ-Portable.
 */

// Adventure Plus 11: the Legendary Demoman (docs/SPEC.md 10.3).
//
// He is a ZOMBIE_BOSS like Dr. Zomboss and Roman, so the boss health bar,
// the award drop and the "everyone else dies with him" ending all come for
// free; what he does is his own. He stands on the right in a 3x3 block,
// flattening whatever is planted under him, and between rests he
//   - lobs a volley of bombs, mostly at random squares (3, then 4-5 once he
//     is down to half health); each takes out whatever it lands on;
//   - marks a 3x3 square, hops onto it, flattens it, and hops back;
//   - marks a lane, charges down it flattening it, runs off the left edge
//     and strolls back in from the right;
//   - late in the fight, throws a doom grenade: it lands, fizzes, and wipes
//     out the 5x5 square it marks.
// Every attack is marked on the lawn before it lands. An Ice-shroom freezes
// him where he stands -- an attack he is winding up waits, though a volley
// he has started still comes -- and freezes every bomb out, which then bursts
// harmlessly. Heavy Weapons Guys come along every so often, and at half
// health he calls in two tanks and waits for them to do their job.
//
// Balance (single player; co-op's two half-rate belts deliver the same): the
// belt brings a plant about every 4 s, so he is tuned to destroy well under
// that -- an attack every 8-12 s, around 1.5-3 plants each -- and his health
// is about four minutes of a typical lawn's fire.
//
// Everything here runs on the gameplay generator, so both co-op players see
// the same fight; drawing uses none.

#include "Zombie.h"
#include "Board.h"
#include "Plant.h"
#include "Challenge.h"
#include "CoopArt.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "../ConstEnums.h"
#include "../PvzpLib/PvzpFoley.h"
#include "../PvzpLib/PvzpCommon.h"
#include "../PvzpLib/PvzpParticle.h"
#include "graphics/Graphics.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace Sexy;

namespace
{
	enum DemoState : int32_t
	{
		LD_ENTER,
		LD_IDLE,
		LD_MOVE,           // sliding up or down to the row his next attack needs
		LD_SHOOT,          // a bomb volley, one throw at a time
		LD_HOP_AIM,        // the square he will land on is marked
		LD_HOP_OUT,
		LD_HOP_LANDED,
		LD_HOP_BACK,
		LD_CHARGE_WINDUP,  // the lane is marked
		LD_CHARGE,
		LD_CHARGE_RETURN,  // walking back in from the right after a charge
		LD_DOOM,           // throwing the doom grenade
		LD_WAIT_TANKS,
		LD_DYING,
		LD_GONE,
	};

	enum DemoAction : int32_t
	{
		ACTION_BOMBS,
		ACTION_CHARGE,
		ACTION_DOOM,
	};

	constexpr float SCALE = 1.3f;               // his art, towering over his 3x3 block
	constexpr int   HOME_COL = 7;               // the middle of his block: columns 6-8
	constexpr int   CENTER_ROW = 2;
	constexpr float BLOCK_HALF_WIDTH = 100.0f;  // a column's middle this close to him is under him
	constexpr int   ENTER_TICKS = 290;          // his spawn animation, 58 frames at 20 a second
	constexpr int   REST_MIN_TICKS = 650;       // between attacks
	constexpr int   REST_MAX_TICKS = 950;
	constexpr int   LATE_REST_MIN_TICKS = 400;  // once he is down to half
	constexpr int   LATE_REST_MAX_TICKS = 650;
	constexpr int   SHOOT_TICKS = 50;           // one throw
	constexpr int   SHOOT_RELEASE_TICK = 28;    // the bomb leaves his hand
	constexpr int   HOP_AIM_TICKS = 140;
	constexpr int   HOP_TICKS = 110;
	constexpr int   HOP_LANDED_TICKS = 130;
	constexpr float HOP_HEIGHT = 230.0f;
	constexpr int   WINDUP_TICKS = 140;
	constexpr float CHARGE_SPEED = 5.0f;        // pixels a tick
	constexpr float RETURN_SPEED = 2.5f;
	constexpr float MOVE_SPEED = 2.5f;
	constexpr int   DYING_TICKS = 330;
	constexpr int   DYING_FALL_START = 160;
	constexpr int   DYING_FALL_TICKS = 70;
	constexpr int   DYING_BOOM = DYING_FALL_START + DYING_FALL_TICKS;
	constexpr int   BOMB_MIN_FLIGHT = 100;
	constexpr int   BOMB_MAX_FLIGHT = 130;
	constexpr float BOMB_ARC = 170.0f;
	constexpr int   BOMB_AIMED_ONE_IN = 4;      // one bomb in four goes for a plant; the rest land anywhere
	constexpr int   DOOM_FLIGHT = 160;
	constexpr float DOOM_ARC = 260.0f;
	constexpr int   DOOM_FUSE = 320;            // on the ground, fizzing, before it goes off
	constexpr int   DOOM_REACH = 2;             // squares each way: a 5x5
	constexpr int   DOOM_COOLDOWN = 1500;       // 15 s: below half, doom grenades are his main threat
	constexpr int   BOMB_FROZEN_TICKS = 70;
	constexpr int   FIRST_SUMMON_TICKS = 1600;
	constexpr int   SUMMON_MIN_TICKS = 1800;
	constexpr int   SUMMON_MAX_TICKS = 2600;
	constexpr int   TANK_WAIT_MAX_TICKS = 3500;
	constexpr int   PLANT_COLS = 6;             // the lawn left of his home block

	float RowHeight(Board* theBoard)
	{
		return static_cast<float>(theBoard->GridToPixelY(0, 1) - theBoard->GridToPixelY(0, 0));
	}

	float CellCenterX(Board* theBoard, int theCol)
	{
		return static_cast<float>(theBoard->GridToPixelX(theCol, 0)) + 40.0f;
	}

	float CellCenterY(Board* theBoard, int theCol, int theRow)
	{
		return static_cast<float>(theBoard->GridToPixelY(theCol, theRow)) + RowHeight(theBoard) * 0.5f;
	}

	// Where his feet stand with his 3x3 block centred on theCenterRow: the
	// bottom of the row below it.
	float FeetY(Board* theBoard, int theCenterRow)
	{
		return static_cast<float>(theBoard->GridToPixelY(0, 0)) + RowHeight(theBoard) * (theCenterRow + 2) - 8.0f;
	}

	float HomeX(Board* theBoard)
	{
		return CellCenterX(theBoard, HOME_COL);
	}

	void BombPosition(const CoopDemoBomb& theBomb, float& theX, float& theY)
	{
		const float t = theBomb.mFlightTicks > 0 ? std::min(static_cast<float>(theBomb.mTicks) / theBomb.mFlightTicks, 1.0f) : 1.0f;
		const float anArc = theBomb.mDoom ? DOOM_ARC : BOMB_ARC;
		theX = theBomb.mFromX + (theBomb.mToX - theBomb.mFromX) * t;
		theY = theBomb.mFromY + (theBomb.mToY - theBomb.mFromY) * t - 4.0f * anArc * t * (1.0f - t);
	}

	// Squares about to be hit, tinted red with an outline, pulsing.
	void DrawDangerSquares(Graphics* g, Board* theBoard, int theCol0, int theCol1, int theRow0, int theRow1,
		int thePulse, float theOffsetX, float theOffsetY)
	{
		const int aHeight = static_cast<int>(RowHeight(theBoard));
		for (int aCol = std::max(theCol0, 0); aCol <= std::min(theCol1, 8); aCol++)
		{
			for (int aRow = std::max(theRow0, 0); aRow <= std::min(theRow1, 4); aRow++)
			{
				const int aX = theBoard->GridToPixelX(aCol, aRow) - static_cast<int>(theOffsetX);
				const int aY = theBoard->GridToPixelY(aCol, aRow) - static_cast<int>(theOffsetY);
				g->SetColor(Color(255, 40, 30, 45 + thePulse));
				g->FillRect(aX + 3, aY + 3, 74, aHeight - 6);
				g->SetColor(Color(255, 90, 60, 150 + thePulse));
				g->DrawRect(aX + 3, aY + 3, 73, aHeight - 7);
			}
		}
	}
}

bool Zombie::IsCoopDemoman()
{
	return mZombieType == ZombieType::ZOMBIE_BOSS && mApp->IsLegendaryDemomanLevel();
}

bool Zombie::IsCoopBoss()
{
	return IsCoopRoman() || IsCoopDemoman();
}

void Zombie::InitCoopDemoman()
{
	mPosX = 0.0f;
	mPosY = 0.0f;
	mZombieRect = Rect(0, 0, 0, 0);  // GetCoopDemoRect follows him instead
	mZombieAttackRect = Rect(0, 0, 0, 0);
	mBodyHealth = COOP_DEMO_HEALTH;
	mZombiePhase = ZombiePhase::PHASE_BOSS_IDLE;
	mRow = CENTER_ROW;

	mCoopBossState = LD_ENTER;
	mCoopBossTimer = ENTER_TICKS;
	mCoopBossAttacksLeft = 0;
	mCoopBossTargetRow = CENTER_ROW;
	mCoopBossAttackLane = 0;
	mCoopBossSummonTimer = FIRST_SUMMON_TICKS;
	mCoopBossNextFire = false;
	mCoopBossNextAction = ACTION_BOMBS;
	mCoopBossDoomCooldown = 0;
	mCoopBossTanksSent = false;
	mCoopBossAnimTicks = 0;
	mCoopBossX = IsOnBoard() ? HomeX(mBoard) : 640.0f;
	mCoopBossY = IsOnBoard() ? FeetY(mBoard, CENTER_ROW) : 470.0f;
	mCoopBossFromX = mCoopBossToX = mCoopBossX;
	mCoopBossFromY = mCoopBossToY = mCoopBossY;
	for (CoopDemoBomb& aBomb : mCoopBombs)
		aBomb = CoopDemoBomb{};
}

Rect Zombie::GetCoopDemoRect()
{
	return Rect(static_cast<int>(mCoopBossX) - 118, static_cast<int>(mCoopBossY) - 360, 236, 350);
}

bool Zombie::CoopDemoCanBeHit()
{
	if (mCoopBossState == LD_DYING || mCoopBossState == LD_GONE)
		return false;
	// Off the edge of the lawn mid-charge: nothing can reach him there.
	return mCoopBossX > -40.0f && mCoopBossX < 860.0f;
}

// Is he standing on this square? Then nothing can be planted there, and
// anything already there is flattened (CoopDemoCrushUnderfoot).
bool Zombie::CoopDemoBlocksSquare(int theCol, int theRow)
{
	switch (mCoopBossState)
	{
	case LD_HOP_OUT:
	case LD_HOP_BACK:
	case LD_CHARGE:
	case LD_DYING:
	case LD_GONE:
		return false;  // in the air, on the run, or going
	default:
		break;
	}
	return std::abs(CellCenterX(mBoard, theCol) - mCoopBossX) <= BLOCK_HALF_WIDTH && std::abs(theRow - mRow) <= 1;
}

void Zombie::CoopDemoCrushUnderfoot()
{
	for (Plant* aPlant : mBoard->mPlants)
	{
		if (aPlant->mDead || !CoopDemoBlocksSquare(aPlant->mPlantCol, aPlant->mRow))
			continue;
		mBoard->mPlantsEaten++;
		if (aPlant->NotOnGround())
			aPlant->Die();
		else
			aPlant->Squish();
	}
}

void Zombie::CoopDemoSetState(int theState, int theTimer)
{
	mCoopBossState = theState;
	mCoopBossTimer = theTimer;
	mCoopBossAnimTicks = 0;
}

void Zombie::CoopDemoGoIdle()
{
	const bool aLate = mBodyHealth * 2 <= mBodyMaxHealth;
	CoopDemoSetState(LD_IDLE, aLate ? RandRangeInt(LATE_REST_MIN_TICKS, LATE_REST_MAX_TICKS) : RandRangeInt(REST_MIN_TICKS, REST_MAX_TICKS));
}

// A plant's square, at random -- like a Bungee Zombie picking its target.
// theTaken lists squares already aimed at (col*8+row).
static bool PickPlantSquare(Board* theBoard, int& theCol, int& theRow, const std::vector<int>& theTaken)
{
	std::vector<int> aSquares;
	for (Plant* aPlant : theBoard->mPlants)
	{
		if (aPlant->mDead)
			continue;
		const int aKey = aPlant->mPlantCol * 8 + aPlant->mRow;
		if (std::find(aSquares.begin(), aSquares.end(), aKey) == aSquares.end() &&
			std::find(theTaken.begin(), theTaken.end(), aKey) == theTaken.end())
			aSquares.push_back(aKey);
	}
	if (aSquares.empty())
		return false;
	// The plants are walked in the board's own order, which both co-op
	// machines share, so the same pick comes out on both.
	const int aKey = aSquares[Rand(static_cast<int>(aSquares.size()))];
	theCol = aKey / 8;
	theRow = aKey % 8;
	return true;
}

void Zombie::CoopDemoPickAttack()
{
	const bool aLate = mBodyHealth * 2 <= mBodyMaxHealth;
	// Once he is down to half he jumps much more, and throws doom grenades.
	const int aBombs = aLate ? 35 : 45, aHop = aLate ? 40 : 25, aCharge = 20;
	const int aDoom = (aLate && mCoopBossDoomCooldown <= 0) ? 40 : 0;
	int aRoll = Rand(aBombs + aHop + aCharge + aDoom);

	if ((aRoll -= aBombs) < 0)
	{
		mCoopBossNextAction = ACTION_BOMBS;
		mCoopBossAttacksLeft = aLate ? RandRangeInt(4, 5) : 3;
		mCoopBossTargetRow = RandRangeInt(1, 3);
		CoopDemoSetState(LD_MOVE, 0);
	}
	else if ((aRoll -= aHop) < 0)
	{
		// A random 3x3 of the lawn, marked before he jumps.
		mCoopBossAttackLane = RandRangeInt(1, PLANT_COLS - 2);
		mCoopBossTargetRow = RandRangeInt(1, 3);
		mCoopBossFromX = mCoopBossX;
		mCoopBossFromY = mCoopBossY;
		mCoopBossToX = CellCenterX(mBoard, mCoopBossAttackLane);
		mCoopBossToY = FeetY(mBoard, mCoopBossTargetRow);
		CoopDemoSetState(LD_HOP_AIM, HOP_AIM_TICKS);
	}
	else if ((aRoll -= aCharge) < 0)
	{
		mCoopBossNextAction = ACTION_CHARGE;
		mCoopBossTargetRow = RandRangeInt(1, 3);
		CoopDemoSetState(LD_MOVE, 0);
	}
	else
	{
		mCoopBossNextAction = ACTION_DOOM;
		mCoopBossTargetRow = CENTER_ROW;
		CoopDemoSetState(LD_MOVE, 0);
	}
}

void Zombie::CoopDemoThrowBomb(bool theDoom)
{
	CoopDemoBomb* aSlot = nullptr;
	std::vector<int> aTaken;
	for (CoopDemoBomb& aBomb : mCoopBombs)
	{
		if (aBomb.mActive)
			aTaken.push_back(aBomb.mCol * 8 + aBomb.mRow);
		else if (aSlot == nullptr)
			aSlot = &aBomb;
	}
	if (aSlot == nullptr)
		return;

	int aCol, aRow;
	if (theDoom)
	{
		aCol = RandRangeInt(1, PLANT_COLS - 2);
		aRow = RandRangeInt(1, 3);
	}
	else if (Rand(BOMB_AIMED_ONE_IN) != 0 || !PickPlantSquare(mBoard, aCol, aRow, aTaken))
	{
		// Anywhere on the lawn left of him.
		aCol = Rand(PLANT_COLS);
		aRow = Rand(5);
	}

	*aSlot = CoopDemoBomb{};
	aSlot->mActive = true;
	aSlot->mDoom = theDoom;
	aSlot->mCol = aCol;
	aSlot->mRow = aRow;
	// From his raised hand.
	aSlot->mFromX = mCoopBossX - 55.0f * SCALE;
	aSlot->mFromY = mCoopBossY - 215.0f * SCALE;
	aSlot->mToX = CellCenterX(mBoard, aCol);
	aSlot->mToY = CellCenterY(mBoard, aCol, aRow) + 10.0f;
	aSlot->mFlightTicks = theDoom ? DOOM_FLIGHT : RandRangeInt(BOMB_MIN_FLIGHT, BOMB_MAX_FLIGHT);
	CoopArt::PlayPackedSound("legend_shoot");
}

// Every plant on a square of the lawn, columns theCol0-theCol1 and rows
// theRow0-theRow1 (clipped to the lawn), destroyed -- flattened if theSquish.
void Zombie::CoopDemoDestroyPlants(int theCol0, int theCol1, int theRow0, int theRow1, bool theSquish)
{
	for (Plant* aPlant : mBoard->mPlants)
	{
		if (aPlant->mDead)
			continue;
		if (aPlant->mPlantCol < theCol0 || aPlant->mPlantCol > theCol1 || aPlant->mRow < theRow0 || aPlant->mRow > theRow1)
			continue;
		mBoard->mPlantsEaten++;
		if (theSquish && !aPlant->NotOnGround())
			aPlant->Squish();
		else
			aPlant->Die();
	}
}

void Zombie::CoopDemoBombLands(CoopDemoBomb& theBomb)
{
	const int aRenderOrder = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0);
	if (theBomb.mDoom)
	{
		CoopDemoDestroyPlants(theBomb.mCol - DOOM_REACH, theBomb.mCol + DOOM_REACH, theBomb.mRow - DOOM_REACH, theBomb.mRow + DOOM_REACH, false);
		mApp->AddPvzpParticle(theBomb.mToX, theBomb.mToY, aRenderOrder, ParticleEffect::PARTICLE_DOOM);
		mApp->PlaySample(SOUND_DOOMSHROOM);
		mBoard->ShakeBoard(3, -4);
	}
	else
	{
		CoopDemoDestroyPlants(theBomb.mCol, theBomb.mCol, theBomb.mRow, theBomb.mRow, false);
		mApp->AddPvzpParticle(theBomb.mToX, theBomb.mToY - 20.0f, aRenderOrder, ParticleEffect::PARTICLE_BOSS_EXPLOSION);
		mBoard->ShakeBoard(1, 2);
	}
	CoopArt::PlayPackedSound("legend_explode");
	theBomb.mActive = false;
}

void Zombie::CoopDemoFreezeBombs()
{
	for (CoopDemoBomb& aBomb : mCoopBombs)
	{
		if (aBomb.mActive && aBomb.mFrozen == 0)
			aBomb.mFrozen = BOMB_FROZEN_TICKS;
	}
}

void Zombie::CoopDemoUpdateBombs()
{
	for (CoopDemoBomb& aBomb : mCoopBombs)
	{
		if (!aBomb.mActive)
			continue;

		// Frozen in the air (or on the ground, fizzing): it holds still, then
		// bursts into ice without hurting anything.
		if (aBomb.mFrozen > 0)
		{
			if (--aBomb.mFrozen == 0)
			{
				float aX, aY;
				BombPosition(aBomb, aX, aY);
				mApp->AddPvzpParticle(aX, aY, Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0), ParticleEffect::PARTICLE_ICE_TRAP_RELEASE);
				mApp->PlayFoley(FoleyType::FOLEY_FROZEN);
				aBomb.mActive = false;
			}
			continue;
		}

		if (aBomb.mTicks < aBomb.mFlightTicks)
		{
			if (++aBomb.mTicks == aBomb.mFlightTicks)
			{
				if (aBomb.mDoom)
				{
					aBomb.mFuse = DOOM_FUSE;
					mApp->PlayFoley(FoleyType::FOLEY_THUMP);
				}
				else
				{
					CoopDemoBombLands(aBomb);
				}
			}
		}
		else if (aBomb.mDoom && --aBomb.mFuse <= 0)
		{
			CoopDemoBombLands(aBomb);
		}
	}
}

void Zombie::CoopDemoSendTanks()
{
	mCoopBossTanksSent = true;
	const int aFirst = Rand(5);
	const int aSecond = (aFirst + 1 + Rand(4)) % 5;
	for (int aRow : { aFirst, aSecond })
	{
		if (Zombie* aTank = mBoard->AddZombieInRow(ZombieType::ZOMBIE_COOP_TANK, aRow, 0))
		{
			aTank->mBodyHealth = COOP_DEMO_TANK_HEALTH;
			aTank->mBodyMaxHealth = COOP_DEMO_TANK_HEALTH;
		}
	}
	CoopDemoSummonHeavy();
	mBoard->DisplayAdvice("The Legendary Demoman called in the tanks!", MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
	CoopDemoSetState(LD_WAIT_TANKS, TANK_WAIT_MAX_TICKS);
}

// His Heavy Weapons Guys are lighter than the ones in the other levels: they
// come while everything else is going on.
void Zombie::CoopDemoSummonHeavy()
{
	if (Zombie* aHeavy = mBoard->AddZombieInRow(ZombieType::ZOMBIE_COOP_HEAVY, Rand(5), 0))
	{
		aHeavy->mBodyHealth = COOP_DEMO_HEAVY_HEALTH;
		aHeavy->mBodyMaxHealth = COOP_DEMO_HEAVY_HEALTH;
	}
}

bool Zombie::CoopDemoTanksAlive()
{
	for (Zombie* aZombie : mBoard->mZombies)
	{
		if (!aZombie->mDead && aZombie->mCoopKind == CoopZombieKind::COOP_KIND_TANK && !aZombie->IsDeadOrDying())
			return true;
	}
	return false;
}

void Zombie::CoopDemoStartDeath()
{
	if (mCoopBossState == LD_DYING || mCoopBossState == LD_GONE)
		return;

	// Bombs still out fizzle.
	for (CoopDemoBomb& aBomb : mCoopBombs)
	{
		if (aBomb.mActive && aBomb.mFrozen == 0)
			aBomb.mFrozen = 1;
	}
	mIceTrapCounter = 0;
	// Somewhere the prize can be picked up, if he went down at the very edge.
	mCoopBossX = std::clamp(mCoopBossX, 140.0f, 700.0f);
	CoopDemoSetState(LD_DYING, DYING_TICKS);
	mApp->PlayFoley(FoleyType::FOLEY_GARGANTUDEATH);
}

void Zombie::UpdateCoopDemoman()
{
	CoopDemoUpdateBombs();

	if (mCoopBossState == LD_GONE)
		return;

	if (mCoopBossState == LD_DYING)
	{
		mCoopBossAnimTicks++;
		const int anElapsed = DYING_TICKS - --mCoopBossTimer;
		if (anElapsed < DYING_FALL_START && anElapsed % 20 == 0)
		{
			// Going to pieces: small blasts all over him.
			const Rect aRect = GetCoopDemoRect();
			const float aX = RandRangeFloat(static_cast<float>(aRect.mX), static_cast<float>(aRect.mX + aRect.mWidth));
			const float aY = RandRangeFloat(static_cast<float>(aRect.mY), static_cast<float>(aRect.mY + aRect.mHeight));
			mApp->AddPvzpParticle(aX, aY, Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0), ParticleEffect::PARTICLE_BOSS_EXPLOSION);
			mApp->PlayFoley(FoleyType::FOLEY_BOSS_EXPLOSION_SMALL);
			mBoard->ShakeBoard(1, 2);
		}
		else if (anElapsed == DYING_BOOM)
		{
			// He hits the ground, and the whole lot goes up.
			mApp->AddPvzpParticle(mCoopBossX + 60.0f, mCoopBossY - 60.0f, Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0), ParticleEffect::PARTICLE_DOOM);
			CoopArt::PlayPackedSound("legend_explode");
			mApp->PlayFoley(FoleyType::FOLEY_THUMP);
			mBoard->ShakeBoard(6, -8);
		}
		else if (anElapsed == DYING_BOOM + 40)  // faded out: the award takes everyone off the board, him included
		{
			BossDie();   // takes everyone else down with him
			DropLoot();  // and the level's award drops where he fell
		}
		if (mCoopBossTimer <= 0)
			mCoopBossState = LD_GONE;
		return;
	}

	const bool aLate = mBodyHealth * 2 <= mBodyMaxHealth;
	if (mCoopBossDoomCooldown > 0)
		mCoopBossDoomCooldown--;

	// A Heavy Weapons Guy now and then.
	if (mCoopBossState != LD_ENTER && mIceTrapCounter == 0 && --mCoopBossSummonTimer <= 0)
	{
		CoopDemoSummonHeavy();
		mCoopBossSummonTimer = RandRangeInt(SUMMON_MIN_TICKS, SUMMON_MAX_TICKS);
	}

	// Whatever is planted where he stands is flattened.
	CoopDemoCrushUnderfoot();

	// Frozen: he stands still, and whatever he was winding up waits -- but a
	// volley already under way keeps coming.
	const bool aFrozen = mIceTrapCounter > 0;
	if (aFrozen && mCoopBossState != LD_SHOOT && mCoopBossState != LD_DOOM)
		return;
	mCoopBossAnimTicks++;

	switch (mCoopBossState)
	{
	case LD_ENTER:
		if (--mCoopBossTimer <= 0)
			CoopDemoGoIdle();
		break;

	case LD_IDLE:
		if (--mCoopBossTimer <= 0)
		{
			if (aLate && !mCoopBossTanksSent)
				CoopDemoSendTanks();
			else
				CoopDemoPickAttack();
		}
		break;

	case LD_WAIT_TANKS:
		if (--mCoopBossTimer <= 0 || !CoopDemoTanksAlive())
			CoopDemoGoIdle();
		break;

	case LD_MOVE:
	{
		const float aTargetY = FeetY(mBoard, mCoopBossTargetRow);
		const float aDiff = aTargetY - mCoopBossY;
		if (std::abs(aDiff) > MOVE_SPEED)
		{
			mCoopBossY += aDiff > 0.0f ? MOVE_SPEED : -MOVE_SPEED;
			break;
		}
		mCoopBossY = aTargetY;
		if (mCoopBossNextAction == ACTION_BOMBS)
			CoopDemoSetState(LD_SHOOT, SHOOT_TICKS);
		else if (mCoopBossNextAction == ACTION_CHARGE)
			CoopDemoSetState(LD_CHARGE_WINDUP, WINDUP_TICKS);
		else
			CoopDemoSetState(LD_DOOM, SHOOT_TICKS);
		break;
	}

	case LD_SHOOT:
		if (--mCoopBossTimer == SHOOT_TICKS - SHOOT_RELEASE_TICK)
			CoopDemoThrowBomb(false);
		if (mCoopBossTimer <= 0)
		{
			if (--mCoopBossAttacksLeft > 0)
				CoopDemoSetState(LD_SHOOT, SHOOT_TICKS);
			else
				CoopDemoGoIdle();
		}
		break;

	case LD_DOOM:
		if (--mCoopBossTimer == SHOOT_TICKS - SHOOT_RELEASE_TICK)
		{
			CoopDemoThrowBomb(true);
			mCoopBossDoomCooldown = DOOM_COOLDOWN;
			mBoard->DisplayAdvice("DOOM GRENADE! Freeze it with an Ice-shroom!", MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
		}
		if (mCoopBossTimer <= 0)
			CoopDemoGoIdle();
		break;

	case LD_HOP_AIM:
		if (--mCoopBossTimer <= 0)
			CoopDemoSetState(LD_HOP_OUT, HOP_TICKS);
		break;

	case LD_HOP_OUT:
	case LD_HOP_BACK:
	{
		const float t = 1.0f - static_cast<float>(--mCoopBossTimer) / HOP_TICKS;
		mCoopBossX = mCoopBossFromX + (mCoopBossToX - mCoopBossFromX) * t;
		mCoopBossY = mCoopBossFromY + (mCoopBossToY - mCoopBossFromY) * t;
		if (mCoopBossTimer > 0)
			break;

		mApp->PlayFoley(FoleyType::FOLEY_THUMP);
		if (mCoopBossState == LD_HOP_OUT)
		{
			// Lands like the bus: the marked square is flattened.
			CoopDemoDestroyPlants(mCoopBossAttackLane - 1, mCoopBossAttackLane + 1, mCoopBossTargetRow - 1, mCoopBossTargetRow + 1, true);
			CoopArt::PlayPackedSound("legend_explode");
			mBoard->ShakeBoard(4, -6);
			CoopDemoSetState(LD_HOP_LANDED, HOP_LANDED_TICKS);
		}
		else
		{
			mBoard->ShakeBoard(2, -3);
			CoopDemoGoIdle();
		}
		break;
	}

	case LD_HOP_LANDED:
		if (--mCoopBossTimer <= 0)
		{
			mCoopBossFromX = mCoopBossX;
			mCoopBossFromY = mCoopBossY;
			mCoopBossToX = HomeX(mBoard);
			mCoopBossToY = FeetY(mBoard, CENTER_ROW);
			CoopDemoSetState(LD_HOP_BACK, HOP_TICKS);
		}
		break;

	case LD_CHARGE_WINDUP:
		if (--mCoopBossTimer <= 0)
		{
			CoopArt::PlayPackedSound("legend_charge");
			CoopDemoSetState(LD_CHARGE, 0);
		}
		break;

	case LD_CHARGE:
	{
		mCoopBossX -= CHARGE_SPEED;
		// Flattens his lane as he goes: whatever his front reaches.
		for (Plant* aPlant : mBoard->mPlants)
		{
			if (aPlant->mDead || aPlant->mRow != mCoopBossTargetRow)
				continue;
			const float aPlantX = CellCenterX(mBoard, aPlant->mPlantCol);
			if (aPlantX > mCoopBossX - 100.0f && aPlantX < mCoopBossX + 20.0f)
			{
				mBoard->mPlantsEaten++;
				if (aPlant->NotOnGround())
					aPlant->Die();
				else
					aPlant->Squish();
			}
		}
		if (mCoopBossAnimTicks % 25 == 0)
			mBoard->ShakeBoard(1, -2);
		if (mCoopBossX < -240.0f)
		{
			// Off the left edge; back in from the right, in the middle.
			mCoopBossX = 1000.0f;
			mCoopBossY = FeetY(mBoard, CENTER_ROW);
			CoopDemoSetState(LD_CHARGE_RETURN, 0);
		}
		break;
	}

	case LD_CHARGE_RETURN:
		mCoopBossX -= RETURN_SPEED;
		if (mCoopBossX <= HomeX(mBoard))
		{
			mCoopBossX = HomeX(mBoard);
			CoopDemoGoIdle();
		}
		break;

	default:
		break;
	}

	// The lanes he can be hit from follow him: the three his block covers.
	const float aRows = (mCoopBossY + 8.0f - static_cast<float>(mBoard->GridToPixelY(0, 0))) / RowHeight(mBoard) - 2.0f;
	mRow = std::clamp(static_cast<int>(std::floor(aRows + 0.5f)), 0, 4);
}

// What is about to be hit, marked on the lawn, and his bombs: drawn over
// everything, apart from his body (DrawCoopDemoman), which stands among the
// zombies at the depth of his feet.
void Zombie::DrawCoopDemoMarks(Graphics* g)
{
	if (IsOnBoard())
	{
		const int aPulse = static_cast<int>(45.0 * (0.5 + 0.5 * sin(mCoopBossAnimTicks * 0.25)));
		Graphics aMarkG(*g);
		if (mCoopBossState == LD_HOP_AIM || mCoopBossState == LD_HOP_OUT)
		{
			DrawDangerSquares(&aMarkG, mBoard, mCoopBossAttackLane - 1, mCoopBossAttackLane + 1,
				mCoopBossTargetRow - 1, mCoopBossTargetRow + 1, aPulse, static_cast<float>(mX), static_cast<float>(mY));
		}
		else if (mCoopBossState == LD_CHARGE_WINDUP || mCoopBossState == LD_CHARGE)
		{
			// The lane up to him, not under him.
			const int aLastCol = std::clamp(static_cast<int>((mCoopBossX - 120.0f) / 80.0f), 0, 8);
			DrawDangerSquares(&aMarkG, mBoard, 0, aLastCol, mCoopBossTargetRow, mCoopBossTargetRow, aPulse,
				static_cast<float>(mX), static_cast<float>(mY));
		}
		for (const CoopDemoBomb& aBomb : mCoopBombs)
		{
			if (aBomb.mActive && aBomb.mDoom && aBomb.mFrozen == 0)
			{
				DrawDangerSquares(&aMarkG, mBoard, aBomb.mCol - DOOM_REACH, aBomb.mCol + DOOM_REACH,
					aBomb.mRow - DOOM_REACH, aBomb.mRow + DOOM_REACH, aPulse, static_cast<float>(mX), static_cast<float>(mY));
			}
		}
	}

	// The bombs, and where each will land.
	for (const CoopDemoBomb& aBomb : mCoopBombs)
	{
		if (!aBomb.mActive)
			continue;

		const bool aLanded = aBomb.mTicks >= aBomb.mFlightTicks;
		if (!aLanded && aBomb.mFrozen == 0 && !aBomb.mDoom)
		{
			Graphics aTargetG(*g);
			aTargetG.SetColorizeImages(true);
			aTargetG.SetColor(Color(255, 255, 255, 200));
			const float aW = static_cast<float>(IMAGE_BUNGEETARGET->mWidth), aH = static_cast<float>(IMAGE_BUNGEETARGET->mHeight);
			PvzpDrawImageScaledF(&aTargetG, IMAGE_BUNGEETARGET, aBomb.mToX - aW * 0.5f - mX, aBomb.mToY - aH * 0.5f - mY, 1.0f, 1.0f);
		}

		float aBombX, aBombY;
		BombPosition(aBomb, aBombX, aBombY);
		const float aScale = aBomb.mDoom ? 2.6f : 1.0f;
		const float aSpin = aLanded ? 0.0f : aBomb.mTicks * (aBomb.mDoom ? 0.06f : 0.18f);
		const int aBombFrame = (aBomb.mTicks + aBomb.mFuse) / 6;

		Graphics aBombG(*g);
		aBombG.SetColorizeImages(true);
		aBombG.SetColor(aBomb.mFrozen > 0 ? Color(120, 190, 255) : Color::White);
		CoopArt::DrawSheetFrame(&aBombG, "demo_bomb", aBombFrame, aBombX - mX, aBombY - mY, aScale, false, aSpin);
		// A doom grenade on the ground flashes faster and faster.
		if (aBomb.mDoom && aLanded && aBomb.mFrozen == 0)
		{
			const int aPeriod = std::max(6, aBomb.mFuse / 8);
			if ((aBomb.mFuse / aPeriod) % 2 == 0)
			{
				aBombG.SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
				aBombG.SetColor(Color(255, 40, 20, 200));
				CoopArt::DrawSheetFrame(&aBombG, "demo_bomb", aBombFrame, aBombX - mX, aBombY - mY, aScale, false, aSpin);
			}
		}
	}
}

void Zombie::DrawCoopDemoman(Graphics* g)
{
	const char* aSheet = "demo_idle";
	int aFrame = mCoopBossAnimTicks * 15 / 100;
	float aRotation = 0.0f;
	int anAlpha = 255;
	float aX = mCoopBossX, aY = mCoopBossY;

	switch (mCoopBossState)
	{
	case LD_ENTER:
		aSheet = "demo_spawn";
		aFrame = std::min(mCoopBossAnimTicks * 20 / 100, CoopArt::SheetFrames(aSheet) - 1);
		break;
	case LD_MOVE:
	case LD_CHARGE_RETURN:
		aSheet = "demo_move";
		aFrame = mCoopBossAnimTicks * 20 / 100;
		break;
	case LD_SHOOT:
	case LD_DOOM:
		aSheet = "demo_shoot";
		aFrame = std::min((SHOOT_TICKS - mCoopBossTimer) * CoopArt::SheetFrames(aSheet) / SHOOT_TICKS, CoopArt::SheetFrames(aSheet) - 1);
		break;
	case LD_HOP_OUT:
	case LD_HOP_BACK:
	{
		aSheet = "demo_fly";
		const float t = 1.0f - static_cast<float>(mCoopBossTimer) / HOP_TICKS;
		aFrame = std::min(static_cast<int>(t * CoopArt::SheetFrames(aSheet)), CoopArt::SheetFrames(aSheet) - 1);
		aY -= 4.0f * HOP_HEIGHT * t * (1.0f - t);
		break;
	}
	case LD_CHARGE:
		aSheet = "demo_charge";
		aFrame = mCoopBossAnimTicks * 24 / 100;
		break;
	case LD_DYING:
	{
		const int anElapsed = DYING_TICKS - mCoopBossTimer;
		if (anElapsed >= DYING_FALL_START)
		{
			// Toppling over backwards, then gone in the blast.
			const float aFall = std::min(static_cast<float>(anElapsed - DYING_FALL_START) / DYING_FALL_TICKS, 1.0f);
			aRotation = aFall * aFall * 1.5f;
			aFrame = 0;
			if (anElapsed > DYING_BOOM)
				anAlpha = std::clamp(255 - (anElapsed - DYING_BOOM) * 255 / 40, 0, 255);
		}
		else
		{
			// Shaking on his feet.
			aX += static_cast<float>((anElapsed / 3) % 2 ? 3 : -3);
		}
		break;
	}
	case LD_GONE:
		anAlpha = 0;
		break;
	default:
		break;
	}

	Graphics aG(*g);
	aG.SetColorizeImages(true);
	if (anAlpha > 0)
	{
		Color aColor(255, 255, 255, anAlpha);
		if (mIceTrapCounter > 0)
			aColor = Color(120, 170, 255, anAlpha);
		aG.SetColor(aColor);
		CoopArt::DrawSheetFrame(&aG, aSheet, aFrame, aX - mX, aY - mY, SCALE, false, aRotation);

		// Glows: red before a charge or hop, white when hit or going to pieces.
		int aGlow = 0;
		Color aGlowColor(255, 255, 255);
		if (mCoopBossState == LD_CHARGE_WINDUP || mCoopBossState == LD_HOP_AIM)
		{
			aGlow = 60 + static_cast<int>(90.0 * (0.5 + 0.5 * sin(mCoopBossAnimTicks * 0.4)));
			aGlowColor = Color(255, 60, 30);
		}
		else if (mCoopBossState == LD_DYING && DYING_TICKS - mCoopBossTimer < DYING_FALL_START)
		{
			aGlow = (mCoopBossTimer / 4) % 2 ? 170 : 30;
		}
		else if (mJustGotShotCounter > 0)
		{
			aGlow = std::min(mJustGotShotCounter * 10, 120);
		}
		if (aGlow > 0)
		{
			aG.SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
			aG.SetColor(Color(aGlowColor.mRed, aGlowColor.mGreen, aGlowColor.mBlue, aGlow * anAlpha / 255));
			CoopArt::DrawSheetFrame(&aG, aSheet, aFrame, aX - mX, aY - mY, SCALE, false, aRotation);
			aG.SetDrawMode(Graphics::DRAWMODE_NORMAL);
		}
	}
}
