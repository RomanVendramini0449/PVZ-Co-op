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
// free; what he does is his own. He stands on the right in a 3x3 block, the
// way Roman does, and between rests he
//   - lobs a volley of bombs at the plants, each one taking out the plant it
//     lands on (3-5 bombs, 6-8 once he is down to half health);
//   - hops into the lawn, flattens a 3x3 square, and hops back;
//   - charges down one lane, flattening it, runs off the left edge and
//     strolls back in from the right;
//   - late in the fight, throws a doom grenade: it lands, fizzes, and wipes
//     out a 6x6 square.
// An Ice-shroom freezes him where he stands (a volley he has started still
// comes) and freezes every bomb in the air, which then bursts harmlessly.
// Dancing Heavies come along every so often, and at half health he calls in
// two armoured tanks and waits for them to do their job.
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

using namespace Sexy;

namespace
{
	enum DemoState : int32_t
	{
		LD_ENTER,
		LD_IDLE,
		LD_MOVE,           // sliding up or down to the row his next attack needs
		LD_SHOOT,          // a bomb volley, one throw at a time
		LD_HOP_OUT,
		LD_HOP_LANDED,
		LD_HOP_BACK,
		LD_CHARGE_WINDUP,
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

	constexpr int   HOME_COL = 7;               // the middle of his block: columns 6-8
	constexpr int   CENTER_ROW = 2;
	constexpr int   ENTER_TICKS = 290;          // his spawn animation, 58 frames at 20 a second
	constexpr int   SHOOT_TICKS = 50;           // one throw
	constexpr int   SHOOT_RELEASE_TICK = 28;    // the bomb leaves his hand
	constexpr int   HOP_TICKS = 110;
	constexpr int   HOP_LANDED_TICKS = 130;
	constexpr float HOP_HEIGHT = 230.0f;
	constexpr int   WINDUP_TICKS = 80;
	constexpr float CHARGE_SPEED = 5.0f;        // pixels a tick
	constexpr float RETURN_SPEED = 2.5f;
	constexpr float MOVE_SPEED = 2.5f;
	constexpr int   DYING_TICKS = 330;
	constexpr int   DYING_FALL_START = 160;
	constexpr int   DYING_FALL_TICKS = 70;
	constexpr int   DYING_BOOM = DYING_FALL_START + DYING_FALL_TICKS;
	constexpr int   BOMB_MIN_FLIGHT = 95;
	constexpr int   BOMB_MAX_FLIGHT = 125;
	constexpr float BOMB_ARC = 170.0f;
	constexpr int   DOOM_FLIGHT = 160;
	constexpr float DOOM_ARC = 260.0f;
	constexpr int   DOOM_FUSE = 260;            // on the ground, fizzing, before it goes off
	constexpr int   DOOM_COOLDOWN = 3500;
	constexpr int   BOMB_FROZEN_TICKS = 70;
	constexpr int   FIRST_SUMMON_TICKS = 1200;
	constexpr int   SUMMON_MIN_TICKS = 1500;
	constexpr int   SUMMON_MAX_TICKS = 2200;
	constexpr int   TANK_WAIT_MAX_TICKS = 3500;
	constexpr int   PLANT_COLS = 6;             // he keeps columns 6-8 to himself

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
	mCoopBossDoomCooldown = DOOM_COOLDOWN / 2;
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
	return Rect(static_cast<int>(mCoopBossX) - 95, static_cast<int>(mCoopBossY) - 280, 190, 270);
}

bool Zombie::CoopDemoCanBeHit()
{
	if (mCoopBossState == LD_DYING || mCoopBossState == LD_GONE)
		return false;
	// Off the edge of the lawn mid-charge: nothing can reach him there.
	return mCoopBossX > -40.0f && mCoopBossX < 860.0f;
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
	CoopDemoSetState(LD_IDLE, aLate ? RandRangeInt(180, 320) : RandRangeInt(280, 450));
}

// A plant's square, at random, for a bomb or a hop -- like a Bungee Zombie
// picking its target. theTaken lists squares already aimed at (col*8+row).
static bool PickPlantSquare(Board* theBoard, int& theCol, int& theRow, const std::vector<int>& theTaken)
{
	std::vector<int> aSquares;
	for (Plant* aPlant : theBoard->mPlants)
	{
		if (aPlant->mDead || aPlant->mPlantCol >= PLANT_COLS)
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
	int aCol, aRow;
	const bool aHasPlants = PickPlantSquare(mBoard, aCol, aRow, {});

	int aBombs = 40, aHop = aHasPlants ? 25 : 0, aCharge = 25;
	int aDoom = (aLate && mCoopBossDoomCooldown <= 0) ? 20 : 0;
	int aRoll = Rand(aBombs + aHop + aCharge + aDoom);

	if ((aRoll -= aBombs) < 0)
	{
		mCoopBossNextAction = ACTION_BOMBS;
		mCoopBossAttacksLeft = aLate ? RandRangeInt(6, 8) : RandRangeInt(3, 5);
		mCoopBossTargetRow = RandRangeInt(1, 3);
		CoopDemoSetState(LD_MOVE, 0);
	}
	else if ((aRoll -= aHop) < 0)
	{
		// The 3x3 he lands on is centred on the plant, kept on the lawn.
		mCoopBossAttackLane = std::clamp(aCol, 1, PLANT_COLS - 1);
		mCoopBossTargetRow = std::clamp(aRow, 1, 3);
		mCoopBossFromX = mCoopBossX;
		mCoopBossFromY = mCoopBossY;
		mCoopBossToX = CellCenterX(mBoard, mCoopBossAttackLane);
		mCoopBossToY = FeetY(mBoard, mCoopBossTargetRow);
		CoopDemoSetState(LD_HOP_OUT, HOP_TICKS);
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
		aCol = RandRangeInt(2, 3);
		aRow = CENTER_ROW;
	}
	else if (!PickPlantSquare(mBoard, aCol, aRow, aTaken))
	{
		aCol = Rand(PLANT_COLS);
		aRow = Rand(5);
	}

	*aSlot = CoopDemoBomb{};
	aSlot->mActive = true;
	aSlot->mDoom = theDoom;
	aSlot->mCol = aCol;
	aSlot->mRow = aRow;
	// From his raised hand.
	aSlot->mFromX = mCoopBossX - 55.0f;
	aSlot->mFromY = mCoopBossY - 215.0f;
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
		// Six squares across: three to the left of where it fell, two to the right.
		CoopDemoDestroyPlants(theBomb.mCol - 3, theBomb.mCol + 2, theBomb.mRow - 3, theBomb.mRow + 2, false);
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
	for (int i = 0; i < 2; i++)
		mBoard->AddZombieInRow(ZombieType::ZOMBIE_COOP_HEAVY, Rand(5), 0);
	mBoard->DisplayAdvice("The Demoman called in the tanks!", MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
	CoopDemoSetState(LD_WAIT_TANKS, TANK_WAIT_MAX_TICKS);
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

	// Dancing Heavies now and then; two at a time once he is hurting.
	if (mCoopBossState != LD_ENTER && mIceTrapCounter == 0 && --mCoopBossSummonTimer <= 0)
	{
		for (int i = 0; i < (aLate ? 2 : 1); i++)
			mBoard->AddZombieInRow(ZombieType::ZOMBIE_COOP_HEAVY, Rand(5), 0);
		mCoopBossSummonTimer = RandRangeInt(SUMMON_MIN_TICKS, SUMMON_MAX_TICKS);
	}

	// Frozen: he stands still -- but a volley already under way keeps coming.
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
			mBoard->DisplayAdvice("DOOM GRENADE! Freeze it!", MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
		}
		if (mCoopBossTimer <= 0)
			CoopDemoGoIdle();
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
			// Lands like the bus: everything under him is flattened.
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
			if (aPlantX > mCoopBossX - 90.0f && aPlantX < mCoopBossX + 20.0f)
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
		if (mCoopBossX < -220.0f)
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
		CoopArt::DrawSheetFrame(&aG, aSheet, aFrame, aX - mX, aY - mY, 1.0f, false, aRotation);

		// Glows: red before a charge, white when hit or going to pieces.
		int aGlow = 0;
		Color aGlowColor(255, 255, 255);
		if (mCoopBossState == LD_CHARGE_WINDUP)
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
			CoopArt::DrawSheetFrame(&aG, aSheet, aFrame, aX - mX, aY - mY, 1.0f, false, aRotation);
			aG.SetDrawMode(Graphics::DRAWMODE_NORMAL);
		}
	}

	// The bombs, and where each will land.
	for (const CoopDemoBomb& aBomb : mCoopBombs)
	{
		if (!aBomb.mActive)
			continue;

		const bool aLanded = aBomb.mTicks >= aBomb.mFlightTicks;
		if (!aLanded && aBomb.mFrozen == 0)
		{
			Graphics aTargetG(*g);
			aTargetG.SetColorizeImages(true);
			aTargetG.SetColor(aBomb.mDoom ? Color(255, 80, 60, 230) : Color(255, 255, 255, 180));
			const float aScale = aBomb.mDoom ? 2.2f : 1.0f;
			const float aW = IMAGE_BUNGEETARGET->mWidth * aScale, aH = IMAGE_BUNGEETARGET->mHeight * aScale;
			PvzpDrawImageScaledF(&aTargetG, IMAGE_BUNGEETARGET, aBomb.mToX - aW * 0.5f - mX, aBomb.mToY - aH * 0.5f - mY, aScale, aScale);
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
