/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

#include <time.h>
#include <cstdarg>
#include <cstdio>
#include <format>
#include "LawnApp.h"
#include "Coop/CoopGame.h"
#include "Lawn/Widget/CoopOverlay.h"
#include "Resources.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/Cutscene.h"
#include "GameConstants.h"
#include "ProjectVersion.h"
#include "Lawn/Challenge.h"
#include "Lawn/ZenGarden.h"
#include "PvzpLib/Trail.h"
#include "Lawn/System/Music.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/PvzpDebug.h"
#include "PvzpLib/PvzpFoley.h"
#include "PvzpLib/Attachment.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/PoolEffect.h"
#include "Lawn/System/ProfileMgr.h"
#include "Lawn/Widget/GameButton.h"
#include "PvzpLib/Reanimator.h"
#include "Lawn/Widget/UserDialog.h"
#include "Lawn/System/TypingCheck.h"
#include "PvzpLib/PvzpParticle.h"
#include "Lawn/Widget/AwardScreen.h"
#include "Lawn/Widget/TitleScreen.h"
#include "Lawn/Widget/StoreScreen.h"
#include "Lawn/Widget/CheatDialog.h"
#include "Lawn/Widget/GameSelector.h"
#include "Lawn/Widget/CreditScreen.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/FilterEffect.h"
#include "graphics/Graphics.h"
#include "PvzpLib/PvzpStringFile.h"
#include "Lawn/Widget/AlmanacDialog.h"
#include "Lawn/Widget/NewUserDialog.h"
#include "Lawn/Widget/ContinueDialog.h"
#include "Lawn/System/ReanimationLawn.h"
#include "Lawn/Widget/ChallengeScreen.h"
#include "Lawn/Widget/NewOptionsDialog.h"
#include "Lawn/Widget/ZombatarTOS.h"
#include "Lawn/Widget/SeedChooserScreen.h"
#include "widget/WidgetManager.h"
#include "misc/ResourceManager.h"
#include <algorithm>

#include "widget/Checkbox.h"
#include "widget/Dialog.h"
#include "SexyAppFramework/resource.h"

bool gIsPartnerBuild = false;
bool gSlowMo = false;
bool gFastMo = false;
LawnApp* gLawnApp = nullptr;
int gSlowMoCounter = 0;

static bool HasUnshownAchievements(PlayerInfo* thePlayerInfo)
{
	if (thePlayerInfo == nullptr)
	{
		return false;
	}

	for (int i = 0; i < MAX_ACHIEVEMENTS; i++)
	{
		if (thePlayerInfo->mEarnedAchievements[i] && !thePlayerInfo->mShownAchievements[i])
		{
			return true;
		}
	}

	return false;
}

bool LawnGetCloseRequest()
{
	if (gLawnApp == nullptr)
		return false;

	return gLawnApp->mCloseRequest;
}

bool LawnHasUsedCheatKeys()
{
	return gLawnApp && gLawnApp->mPlayerInfo && gLawnApp->mPlayerInfo->mHasUsedCheatKeys;
}

LawnApp::LawnApp()
{
	// Replace the base-class resource manager with the PvZP-capable subclass.
	mResourceManager = std::make_unique<PvzpResourceManager>(this);

	mBoard = nullptr;
	mMustacheMode = false;
	mSuperMowerMode = false;
	mFutureMode = false;
	mPinataMode = false;
	mDanceMode = false;
	mDaisyMode = false;
	mSukhbirMode = false;
	mGameScene = GameScenes::SCENE_LOADING;
	mCloseRequest = false;
	mWidth = BOARD_WIDTH;
	mHeight = BOARD_HEIGHT;
	mFullscreenBits = 32;
	mAppCounter = 0;
	mAppRandSeed = time(0);
	mTrialType = TrialType::TRIALTYPE_NONE;
	mDebugTrialLocked = false;
	mMuteSoundsForCutscene = false;
	mMusicVolume = 0.85;
	mSfxVolume = 0.5525;
	mAutoStartLoadingThread = false;
	mDebugKeysEnabled = false;
	mProdName = "io.github.wszqkzqk.pvz-portable";
	mProductVersion = PVZP_VERSION;
	mBuildNum = PVZP_BUILD_NUMBER;
	mCommitDate = PVZP_COMMIT_DATE;
	// The product name is the save and registry path, so it stays as it is --
	// changing it would lose the player's existing profile.
	mTitle = CoopWindowTitle();
	mCustomCursorsEnabled = false;
	mPlayerInfo = nullptr;
	mLastLevelStats = std::make_unique<LevelStats>();
	mFirstTimeGameSelector = true;
	mGameMode = GameMode::GAMEMODE_ADVENTURE;
	mEasyPlantingCheat = false;
	mAutoEnable3D = true;
	Pvzp_SWTri_AddAllDrawTriFuncs();
	mLoadingZombiesThreadCompleted = true;
	mGamesPlayed = 0;
	mMaxExecutions = 0;
	mMaxPlays = 0;
	mMaxTime = 0;
	mCompletedLoadingThreadTasks = 0;
	mProfileMgr = std::make_unique<ProfileMgr>();
	mRegisterResourcesLoaded = false;
	mCheatKeys = false;
	mCrazyDaveReanimID = ReanimationID::REANIMATIONID_NULL;
	mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_OFF;
	mCrazyDaveBlinkCounter = 0;
	mCrazyDaveBlinkReanimID = ReanimationID::REANIMATIONID_NULL;
	mCrazyDaveMessageIndex = -1;
}

LawnApp::~LawnApp()
{
	while (!mDialogMap.empty())
	{
		KillDialog(mDialogMap.begin()->first);
	}

	if (mBoard)
	{
		mBoardResult = BoardResult::BOARDRESULT_QUIT_APP;
		WriteCurrentUserConfig();
		KillBoard();
	}
	ProcessSafeDeleteList();

	if (mTitleScreen)
	{
		mWidgetManager->RemoveWidget(mTitleScreen.get());
	}

	if (mGameSelector)
	{
		mWidgetManager->RemoveWidget(mGameSelector.get());
	}
	if (mChallengeScreen)
	{
		mWidgetManager->RemoveWidget(mChallengeScreen.get());
	}
	if (mSeedChooserScreen)
	{
		mWidgetManager->RemoveWidget(mSeedChooserScreen.get());
	}
	if (mAwardScreen)
	{
		mWidgetManager->RemoveWidget(mAwardScreen.get());
	}
	if (mCreditScreen)
	{
		mWidgetManager->RemoveWidget(mCreditScreen.get());
	}
	if (mCoopOverlay)
	{
		mWidgetManager->RemoveWidget(mCoopOverlay.get());
	}

	// Must die before the definition tables and allocators are freed below.
	mGameSelector.reset();
	mEffectSystem.reset();
	mReanimatorCache.reset();

	FilterEffectDisposeForApp();
	PvzpParticleFreeDefinitions();
	ReanimatorFreeDefinitions();
	TrailFreeDefinitions();
	FreeGlobalAllocators();
	UpdateRegisterInfo();

	mResourceManager->DeleteResources("");
}

void LawnApp::Shutdown()
{
	if (!mLoadingThreadCompleted)
	{
		mLoadingFailed = true;
		SexyAppBase::Shutdown();
		return;
	}

	if (!mShutdown)
	{
		SexyAppBase::Shutdown();
	}
}

void LawnApp::ShutdownHook()
{
	// Say goodbye rather than just vanishing, so the other player gets a
	// clear reason instead of a timeout.
	if (Coop::Game::Get().IsConnected())
		Coop::Game::Get().GetSession().Disconnect("The other player quit the game.");

	// Save mid-level game while the music is still alive, before Shutdown() stops it.
	if (mBoard)
	{
		mBoardResult = BoardResult::BOARDRESULT_QUIT_APP;
		mBoard->TryToSaveGame();
	}
}

void LawnApp::KillBoard()
{
	FinishModelessDialogs();
	KillSeedChooserScreen();
	if (mBoard)
	{
		if (mPlayerInfo && (
			mBoardResult == BoardResult::BOARDRESULT_WON ||
			mBoardResult == BoardResult::BOARDRESULT_LOST ||
			mBoardResult == BoardResult::BOARDRESULT_RESTART ||
			mBoardResult == BoardResult::BOARDRESULT_CHEAT))
		{
			std::string aFileName = GetSavedGameName(mGameMode, mPlayerInfo->mId);
			EraseFile(aFileName);
			std::string aLegacyFileName = GetLegacySavedGameName(mGameMode, mPlayerInfo->mId);
			EraseFile(aLegacyFileName);
		}

		// Leaving the level stops the lockstep; the connection itself stays
		// up so the players can carry on to the next one. Now, while the board
		// goes, not in its destructor: that runs a frame later, after a retry
		// has started the next level's clock (see Board::~Board).
		Coop::Game::Get().EndLevel();

		mBoard->DisposeBoard();
		mWidgetManager->RemoveWidget(mBoard);
		SafeDeleteWidget(mBoard);
		mBoard = nullptr;
	}

	SetCursor(CURSOR_POINTER);
}

bool LawnApp::CanPauseNow()
{
	if (mBoard == nullptr)
		return false;

	// See Board::Pause: nothing in co-op may stop the clock.
	if (Coop::Game::Get().IsConnected())
		return false;

	if (mSeedChooserScreen && mSeedChooserScreen->mMouseVisible)
		return false;

	if (mBoard->mBoardFadeOutCounter >= 0)
		return false;

	if (mCrazyDaveState != CrazyDaveState::CRAZY_DAVE_OFF)
		return false;

	if (mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
		return false;

	return GetDialogCount() <= 0;
}

void LawnApp::GotFocus()
{
}

void LawnApp::LostFocus()
{
#if (defined(__ANDROID__) && !defined(__TERMUX__)) || defined(__IPHONEOS__)
	if (!mCheatKeys && CanPauseNow())
	{
		DoPauseDialog();
	}
#endif
}

void LawnApp::WriteToRegistry()
{
	// While the client is borrowing the host's campaign, mPlayerInfo is not
	// its own progress, so writing it out would overwrite the joining
	// player's save with the host's (SPEC.md section 10.4 -- their profile is
	// never touched).
	if (mPlayerInfo && !mCoopOwnProfile)
	{
		RegistryWriteString("CurUser", mPlayerInfo->mName);
		mPlayerInfo->SaveDetails();
	}

	SexyAppBase::WriteToRegistry();
}

void LawnApp::ReadFromRegistry()
{
	SexyApp::ReadFromRegistry();
}

bool LawnApp::WriteCurrentUserConfig()
{
	// Same reason as WriteToRegistry: borrowed progress does not get saved.
	if (mPlayerInfo && !mCoopOwnProfile)
		mPlayerInfo->SaveDetails();

	return true;
}

void LawnApp::PreNewGame(GameMode theGameMode, bool theLookForSavedGame)
{
	// Only the host starts levels in co-op; the client is put into them.
	if (Coop::Game::Get().IsFollower())
		return;

	//if (NeedRegister())
	//{
	//	ShowGameSelector();
	//	return;
	//}

	mGameMode = theGameMode;

	// Co-op has no mid-level save yet: the format stores one player's seed
	// bank and cursor, not two (SPEC.md section 10.4). Resuming one would put
	// the host in a half-finished board the client knows nothing about, so a
	// co-op session always starts the level fresh -- which is cheap, because
	// a level restarts deterministically from its seed.
	if (Coop::Game::Get().IsConnected())
		theLookForSavedGame = false;

	if (theLookForSavedGame && TryLoadGame())
		return;

	std::string aFileName = GetSavedGameName(mGameMode, mPlayerInfo->mId);
	EraseFile(aFileName);
	std::string aLegacyFileName = GetLegacySavedGameName(mGameMode, mPlayerInfo->mId);
	EraseFile(aLegacyFileName);
	NewGame();
}

void LawnApp::MakeNewBoard()
{
	KillBoard();
	mBoard = new Board(this);
	mBoard->Resize(0, 0, mWidth, mHeight);
	mWidgetManager->AddWidget(mBoard);
	mWidgetManager->BringToBack(mBoard);
	mWidgetManager->SetFocus(mBoard);
}

void LawnApp::StartPlaying()
{
	KillSeedChooserScreen();
	mBoard->StartLevel();
	mGameScene = GameScenes::SCENE_PLAYING;
}

bool LawnApp::SaveFileExists()
{
	std::string aFileName = GetSavedGameName(GameMode::GAMEMODE_ADVENTURE, mPlayerInfo->mId);
	if (this->FileExists(aFileName))
		return true;
	std::string aLegacyFileName = GetLegacySavedGameName(GameMode::GAMEMODE_ADVENTURE, mPlayerInfo->mId);
	return this->FileExists(aLegacyFileName);
}

bool LawnApp::TryLoadGame()
{
	std::string aSaveName = GetSavedGameName(mGameMode, mPlayerInfo->mId);
	std::string aLegacySaveName = GetLegacySavedGameName(mGameMode, mPlayerInfo->mId);
	mMusic->StopAllMusic();

	if (this->FileExists(aSaveName))
	{
		MakeNewBoard();
		if (mBoard->LoadGame(aSaveName))
		{
			mFirstTimeGameSelector = false;
			if (mBoard->mLevelAwardSpawned) // Ensure save cleanup after award collection
				mBoardResult = BoardResult::BOARDRESULT_WON;
			DoContinueDialog();
			return true;
		}

		KillBoard();
	}
	if (this->FileExists(aLegacySaveName))
	{
		MakeNewBoard();
		if (mBoard->LoadGame(aLegacySaveName))
		{
			if (LawnSaveGame(mBoard, aSaveName))
			{
				EraseFile(aLegacySaveName);
			}
			mFirstTimeGameSelector = false;
			if (mBoard->mLevelAwardSpawned) // Ensure save cleanup after award collection
				mBoardResult = BoardResult::BOARDRESULT_WON;
			DoContinueDialog();
			return true;
		}

		KillBoard();
	}

	return false;
}

void LawnApp::NewGame()
{
	mFirstTimeGameSelector = false;

	// Every route into a level funnels through here, which makes it the one
	// place co-op has to agree on: the host says which level this is, and the
	// client has already been told.
	CoopAnnounceLevel();

	Coop::Game& aCoop = Coop::Game::Get();
	const bool aCoopLevel = aCoop.IsConnected() && aCoop.HasLevelSetup();
	if (aCoopLevel)
	{
		// Both machines must build the board from the same random state. The
		// Board constructor and InitLevel below both draw from the global
		// generator, so the reseed has to come first -- after them is too
		// late and the two lawns are already different shapes.
		const uint32_t aSeed = aCoop.GetLevelSetup().mRngSeed;
		Sexy::SRand(aSeed);
		mAppRandSeed = static_cast<int>(aSeed);
	}

	MakeNewBoard();

	// The lockstep clock starts here rather than at StartLevel, because the
	// seed chooser is shared (SPEC.md section 4) and has to run on the same
	// scheduler as the board for both players' picks to land identically.
	if (aCoopLevel)
		aCoop.BeginLevel();

	// Build the level as player 0, the player the simulation always runs as
	// (see Board::Update). This all happens before the first tick, outside
	// it: the board would otherwise be resting on the local player, which is
	// a different player on each machine. The intro hides the seed bank right
	// here -- and it was hiding bank 0 on the host and bank 1 on the client,
	// so the client saw the HUD through the intro, and the two machines did
	// not agree where the bank was for clicks on it.
	const int aRestingPlayer = mBoard->GetActivePlayer();
	if (aCoopLevel)
		mBoard->SetActivePlayer(0);

	mBoard->InitLevel();
	mBoardResult = BoardResult::BOARDRESULT_NONE;
	mGameScene = GameScenes::SCENE_LEVEL_INTRO;

	ShowSeedChooserScreen();
	mBoard->mCutScene->StartLevelIntro();

	if (aCoopLevel)
	{
		mBoard->CoopSyncSeedBankLayout();
		mBoard->SetActivePlayer(aRestingPlayer);
	}
}

// ---------------------------------------------------------------------------
// Co-op campaign flow (docs/SPEC.md section 3)
// ---------------------------------------------------------------------------
//
// Outside a level the host drives and the client follows. Before this existed
// each player navigated their own menus and called into their own level, so
// the two machines were two unrelated games that happened to share a socket --
// which is exactly what it looked like on screen.

static_assert(sizeof(PlayerInfo::mPurchases) / sizeof(uint32_t) == Coop::PURCHASE_COUNT,
	"Coop::PURCHASE_COUNT must match PlayerInfo::mPurchases");

// Two windows side by side are otherwise indistinguishable, which matters a
// great deal when they behave differently on purpose. The title also says
// whether the other player is actually there.
std::string LawnApp::CoopWindowTitle() const
{
	const Coop::Session& aSession = Coop::Game::Get().GetSession();
	if (!aSession.IsActive())
		return "PvZ Co-op";

	const bool aConnected = aSession.IsConnected();
	if (aSession.IsHost())
		return aConnected ? "PvZ Co-op - Host - 1 player connected" : "PvZ Co-op - Host - 0 players connected";
	return aConnected ? "PvZ Co-op - Connected" : "PvZ Co-op - Not Connected";
}

void LawnApp::CoopRefreshWindowTitle()
{
	if (mWindow == nullptr)
		return;

	std::string aTitle = CoopWindowTitle();
	if (aTitle == mTitle)
		return;

	mTitle = std::move(aTitle);
	SDL_SetWindowTitle(static_cast<SDL_Window*>(mWindow), mTitle.c_str());
}

Coop::LevelSetup LawnApp::CoopMakeProfileSetup() const
{
	Coop::LevelSetup aSetup;
	if (mPlayerInfo == nullptr)
		return aSetup;

	aSetup.mProfileLevel = mPlayerInfo->mLevel;
	aSetup.mCoins = mPlayerInfo->mCoins;
	aSetup.mFinishedAdventure = mPlayerInfo->mFinishedAdventure;
	for (int i = 0; i < Coop::PURCHASE_COUNT; i++)
		aSetup.mPurchases[i] = mPlayerInfo->mPurchases[i];
	aSetup.mDifficulty = Coop::Game::Get().GetNextDifficulty();
	return aSetup;
}

void LawnApp::CoopAnnounceLevel()
{
	Coop::Game& aCoop = Coop::Game::Get();
	if (!aCoop.IsConnected() || !aCoop.IsHost() || mPlayerInfo == nullptr)
		return;

	Coop::LevelSetup aSetup = CoopMakeProfileSetup();
	aSetup.mLevel = static_cast<uint32_t>(mPlayerInfo->mLevel);
	aSetup.mGameMode = static_cast<int32_t>(mGameMode);
	// Not reset yet: InitLevel reads it as "how the last level went".
	aSetup.mPrevBoardResult = static_cast<int32_t>(mBoardResult);

	// Derived from the session seed and the level rather than drawn fresh, so
	// a level replayed after a loss or a dropped connection comes back the
	// same (SPEC.md section 10.4) while different levels still get different
	// waves. Mixing in the level matters: without it every level in the
	// campaign would run the same random sequence.
	aSetup.mRngSeed = aCoop.GetSession().GetParams().mRngSeed
		^ (static_cast<uint32_t>(mPlayerInfo->mLevel) * 2654435761u);

	aCoop.HostEnterLevel(aSetup);

	// A level is not a screen to mirror; forget the last one so that leaving
	// the level re-announces wherever the host lands, even if it is the same
	// kind of screen as before.
	mCoopLastScreen = Coop::HostScreen::None;
}

void LawnApp::CoopBorrowHostProfile(const Coop::LevelSetup& theSetup)
{
	if (mPlayerInfo == nullptr)
		return;

	// Keep the client's own campaign aside the first time, so it can be handed
	// back intact when the session ends. It is never saved while borrowed.
	if (!mCoopOwnProfile)
	{
		mCoopOwnProfile = std::make_unique<PlayerInfo>();
		*mCoopOwnProfile = *mPlayerInfo;
	}

	mPlayerInfo->mLevel = theSetup.mProfileLevel;
	mPlayerInfo->mCoins = theSetup.mCoins;
	mPlayerInfo->mFinishedAdventure = theSetup.mFinishedAdventure;
	for (int i = 0; i < Coop::PURCHASE_COUNT; i++)
		mPlayerInfo->mPurchases[i] = theSetup.mPurchases[i];

	mCoopShownDifficulty = theSetup.mDifficulty;
}

void LawnApp::CoopReturnOwnProfile()
{
	if (!mCoopOwnProfile || mPlayerInfo == nullptr)
		return;

	*mPlayerInfo = *mCoopOwnProfile;
	mCoopOwnProfile.reset();
}

// Takes down whatever the client is showing, so the next screen the host asks
// for starts from nothing. Dialogs go first: several of them sit over a board
// or a screen, and the host moving on is not something they can see coming.
void LawnApp::CoopTearDownForFollow()
{
	Coop::Game::Get().EndLevel();  // retire the previous level's clock
	// Dialogs stay: settings or the almanac can sit over whatever comes next
	// now that nothing pauses, and closing one from under its own nested
	// loop risks a crash. Game Over is the exception: it belongs to the level
	// being left, runs no loop of its own, and on this machine has no button
	// to close it -- the host's Try Again or Main Menu is what closes it.
	KillDialog(Dialogs::DIALOG_GAME_OVER);
	KillStoreScreen();
	KillCreditScreen();
	KillAwardScreen();
	KillSeedChooserScreen();
	KillBoard();
	KillGameSelector();
	mCoopWaitingForHost = false;
}

void LawnApp::CoopFollowIntoLevel(const Coop::LevelSetup& theSetup)
{
	// Being moved into a level while the old one was out of sync means the
	// host restarted it for that reason; say so here too.
	if (Coop::Game::Get().IsInGame() && Coop::Game::Get().GetLockstep().IsDesynced())
		mCoopResyncNoticeUntil = SDL_GetTicks() + 6000;

	// The host's progress decides the shape of the level -- how many seed
	// slots, which seeds exist, whether the roof has mowers -- so it has to be
	// in place before anything reads it.
	CoopBorrowHostProfile(theSetup);
	CoopTearDownForFollow();

	mGameMode = static_cast<GameMode>(theSetup.mGameMode);
	// The host's, not ours: a retry after a loss skips the intro on the host,
	// and this machine must skip it too.
	mBoardResult = static_cast<BoardResult>(theSetup.mPrevBoardResult);
	NewGame();
}

void LawnApp::CoopLeaveLevel()
{
	Coop::Game::Get().ClearLevelSetup();
	CoopTearDownForFollow();
	// Somewhere to stand until the host says where it has gone; normally the
	// next flow event replaces this at once.
	ShowGameSelector();
	mCoopWaitingForHost = true;
}

void LawnApp::CoopFollowScreen(Coop::HostScreen theScreen, int theArg, const Coop::LevelSetup& theProfile)
{
	// The profile first: a mirrored award shows the plant the host just won,
	// a mirrored store shows the host's coins and what is already bought.
	CoopBorrowHostProfile(theProfile);
	Coop::Game::Get().ClearLevelSetup();

	switch (theScreen)
	{
	case Coop::HostScreen::Menu:
		if (mGameSelector && !mBoard && !mAwardScreen && !mCreditScreen)
		{
			KillStoreScreen();
			mCoopWaitingForHost = true;
			break;  // already there
		}
		CoopTearDownForFollow();
		ShowGameSelector();
		mCoopWaitingForHost = true;
		break;

	case Coop::HostScreen::Award:
		if (mAwardScreen && static_cast<int>(mAwardScreen->mAwardType) == theArg && !mBoard)
		{
			KillStoreScreen();  // the host came back from the store to this award
			break;
		}
		CoopTearDownForFollow();
		// Achievements are the host's to collect, not this player's, so the
		// mirrored award never shows them.
		ShowAwardScreen(static_cast<AwardType>(theArg), false);
		break;

	case Coop::HostScreen::Store:
	{
		// The store sits over whatever the host opened it from. If we are
		// somewhere it cannot sit over -- still in a level, say -- start
		// from the menu.
		if (mBoard || mCreditScreen || (!mGameSelector && !mAwardScreen))
		{
			CoopTearDownForFollow();
			ShowGameSelector();
		}
		StoreScreen* aStore = static_cast<StoreScreen*>(GetDialog(Dialogs::DIALOG_STORE));
		if (aStore == nullptr)
			aStore = ShowStoreScreen();
		if (aStore != nullptr && theArg >= 0 && theArg < static_cast<int>(StorePages::NUM_STORE_PAGES)
			&& static_cast<int>(aStore->mPage) != theArg)
		{
			aStore->mPage = static_cast<StorePages>(theArg);
			aStore->mHatchTimer = 50;  // the same page-turn the host just saw
		}
		break;
	}

	case Coop::HostScreen::Credits:
		if (mCreditScreen)
			break;
		CoopTearDownForFollow();
		ShowCreditScreen();
		break;

	case Coop::HostScreen::None:
	default:
		break;
	}
}

// Host: watch where we are and tell the client when it changes. Watching beats
// instrumenting every transition -- the award screen alone can lead to the
// store, the almanac, the credits or the next level, through several nested
// loops -- and it cannot miss a path nobody thought of.
void LawnApp::CoopObserveHostScreen()
{
	Coop::HostScreen aScreen = Coop::HostScreen::None;
	int aArg = 0;

	if (StoreScreen* aStore = static_cast<StoreScreen*>(GetDialog(Dialogs::DIALOG_STORE)))
	{
		aScreen = Coop::HostScreen::Store;
		aArg = static_cast<int>(aStore->mPage);
	}
	else if (mCreditScreen)
		aScreen = Coop::HostScreen::Credits;
	else if (mAwardScreen)
	{
		aScreen = Coop::HostScreen::Award;
		aArg = static_cast<int>(mAwardScreen->mAwardType);
	}
	else if (mBoard)
		aScreen = Coop::HostScreen::None;  // a level: EnterLevel already covered it
	else if (mGameSelector)
		aScreen = Coop::HostScreen::Menu;

	if (aScreen == Coop::HostScreen::None)
		return;

	// Out of a level, by whatever route -- some, like Game Over's Main Menu,
	// go straight to the menu without passing through DoBackToMain -- so the
	// level's setup no longer applies.
	Coop::Game::Get().ClearLevelSetup();

	// What the client displays also depends on the profile -- coins in the
	// store, difficulty on the menu -- so a purchase counts as a change too.
	const Coop::LevelSetup aProfile = CoopMakeProfileSetup();
	uint32_t aHash = 2166136261u;
	auto aMix = [&aHash](uint32_t theValue) { aHash = (aHash ^ theValue) * 16777619u; };
	aMix(static_cast<uint32_t>(aProfile.mProfileLevel));
	aMix(static_cast<uint32_t>(aProfile.mCoins));
	aMix(aProfile.mFinishedAdventure);
	aMix(static_cast<uint32_t>(aProfile.mDifficulty));
	for (int i = 0; i < Coop::PURCHASE_COUNT; i++)
		aMix(aProfile.mPurchases[i]);

	if (aScreen == mCoopLastScreen && aArg == mCoopLastScreenArg && aHash == mCoopLastProfileHash)
		return;

	mCoopLastScreen = aScreen;
	mCoopLastScreenArg = aArg;
	mCoopLastProfileHash = aHash;
	Coop::Game::Get().HostShowScreen(aScreen, aArg, aProfile);
}

// The session has ended, for whatever reason: the other player left, the link
// dropped, or this player chose to leave. Both machines go back to the menu
// with the reason on screen (SPEC.md section 2), and the joining player gets
// their own campaign back before anything can save it.
void LawnApp::CoopHandleDisconnect()
{
	Coop::Session& aSession = Coop::Game::Get().GetSession();
	const bool aWasHost = aSession.IsHost();
	std::string aReason;
	switch (aSession.GetEndReason())
	{
	case Coop::Session::EndReason::PeerLeft:
		aReason = aWasHost ? "The joining player left the game." : "The host left the game.";
		break;
	case Coop::Session::EndReason::LocalLeft:
		aReason = "You left the co-op game.";
		break;
	case Coop::Session::EndReason::Lost:
		aReason = aWasHost ? "Lost the connection to the joining player." : "Lost the connection to the host.";
		break;
	default:
		aReason = aSession.GetStatusText();
		break;
	}

	Coop::Game::Get().EndLevel();
	Coop::Game::Get().ClearLevelSetup();

	// Dialogs are left alone: several run their own nested loop, and closing
	// one from under that loop is a crash waiting to happen. With nothing
	// paused any more, an open settings menu or almanac is harmless.
	if (Coop::Game::Get().GetSession().IsHost())
	{
		// The host's award, store and credits are its own and carry on as
		// single player. Only a level in progress was a co-op one.
		if (mBoard)
		{
			KillSeedChooserScreen();
			KillBoard();
			ShowGameSelector();
		}
	}
	else
	{
		// Everything the client shows was mirrored from the host. Its store
		// mirror never runs a nested loop (its buttons are dead), so it is
		// safe to close.
		KillStoreScreen();
		KillCreditScreen();
		KillAwardScreen();
		KillSeedChooserScreen();
		KillBoard();
		CoopReturnOwnProfile();
		// The menu was built with Adventure locked; rebuild it now that the
		// campaign is this player's own again.
		ShowGameSelector();
	}
	mCoopWaitingForHost = false;
	mCoopLastScreen = Coop::HostScreen::None;

	// A session is set up in the co-op launcher, not in the game, so a new
	// one starts there too. Offering host/join here would be a second,
	// different way to do the same thing; closing keeps it to one.
	const std::string aText = (aReason.empty() ? std::string("The co-op game has ended.") : aReason)
		+ "\n\nTo play again, start a new\nsession from the co-op launcher.";
	LawnMessageBox(Dialogs::DIALOG_MESSAGE, "Co-op Ended", aText.c_str(), "Close Game", "", Dialog::BUTTONS_FOOTER);
	CloseRequestAsync();
}

void LawnApp::CoopUpdateFlow()
{
	Coop::Game& aCoop = Coop::Game::Get();
	const bool aConnected = aCoop.IsConnected();

	// Record the new state before acting on it: the handler shows a message
	// box, which runs its own loop and comes back through here -- and seeing
	// "was connected" still set, it would handle the drop again, and again,
	// until the stack ran out.
	const bool aJustDisconnected = mCoopWasConnected && !aConnected;
	mCoopWasConnected = aConnected;
	if (aJustDisconnected)
		CoopHandleDisconnect();
	if (!aConnected)
		return;

	if (aCoop.IsHost())
	{
		CoopObserveHostScreen();
		return;
	}

	// Nothing to follow from, yet: still loading, or on the title screen.
	// Clicking through the title goes to the menu, which would throw away
	// anything built underneath it, so the host's steps wait in the queue
	// until we are past it. Once we are, say so -- the host will not start a
	// level until we have.
	if (mTitleScreen || !mLoadingThreadCompleted)
		return;
	aCoop.GetSession().SendPeerReady();

	// Strictly in the order the host took them.
	Coop::FlowEvent anEvent;
	while (aCoop.PopFlowEvent(anEvent))
	{
		switch (anEvent.mKind)
		{
		case Coop::FlowEvent::Kind::EnterLevel:
			CoopFollowIntoLevel(anEvent.mSetup);
			break;
		case Coop::FlowEvent::Kind::LeaveLevel:
			CoopLeaveLevel();
			break;
		case Coop::FlowEvent::Kind::Screen:
			CoopFollowScreen(anEvent.mScreen, anEvent.mArg, anEvent.mSetup);
			break;
		}
	}
}

void LawnApp::ShowGameSelector()
{
	KillBoard();
	//UpdateRegisterInfo();
	if (mGameSelector)
	{
		mWidgetManager->RemoveWidget(mGameSelector.get());
		SafeDeleteWidget(mGameSelector.release());
	}

	mGameScene = GameScenes::SCENE_MENU;
	mGameSelector = std::make_unique<GameSelector>(this);
	mGameSelector->Resize(0, 0, mWidth, mHeight);
	mWidgetManager->AddWidget(mGameSelector.get());
	mWidgetManager->BringToBack(mGameSelector.get());
	mWidgetManager->SetFocus(mGameSelector.get());

	//if (NeedRegister())
	//{
	//	DoNeedRegisterDialog();
	//}
}

void LawnApp::KillGameSelector()
{
	if (mGameSelector)
	{
		mWidgetManager->RemoveWidget(mGameSelector.get());
		SafeDeleteWidget(mGameSelector.release());
	}
}

void LawnApp::ShowAwardScreen(AwardType theAwardType, bool theShowAchievements)
{
	mGameScene = GameScenes::SCENE_AWARD;
	mAwardScreen = std::make_unique<AwardScreen>(this, theAwardType, theShowAchievements);
	mAwardScreen->Resize(0, 0, mWidth, mHeight);
	mWidgetManager->AddWidget(mAwardScreen.get());
	mWidgetManager->BringToBack(mAwardScreen.get());
	mWidgetManager->SetFocus(mAwardScreen.get());
}

void LawnApp::KillAwardScreen()
{
	if (mAwardScreen)
	{
		mWidgetManager->RemoveWidget(mAwardScreen.get());
		SafeDeleteWidget(mAwardScreen.release());
	}
}

void LawnApp::ShowCreditScreen()
{
	mCreditScreen = std::make_unique<CreditScreen>(this);
	mCreditScreen->Resize(0, 0, mWidth, mHeight);
	mWidgetManager->AddWidget(mCreditScreen.get());
	mWidgetManager->BringToBack(mCreditScreen.get());
	mWidgetManager->SetFocus(mCreditScreen.get());
}

void LawnApp::KillCreditScreen()
{
	if (mCreditScreen)
	{
		mWidgetManager->RemoveWidget(mCreditScreen.get());
		SafeDeleteWidget(mCreditScreen.release());
	}
}

void LawnApp::ShowChallengeScreen(ChallengePage thePage)
{
	mGameScene = GameScenes::SCENE_CHALLENGE;
	mChallengeScreen = std::make_unique<ChallengeScreen>(this, thePage);
	mChallengeScreen->Resize(0, 0, mWidth, mHeight);
	mWidgetManager->AddWidget(mChallengeScreen.get());
	mWidgetManager->BringToBack(mChallengeScreen.get());
	mWidgetManager->SetFocus(mChallengeScreen.get());
}

void LawnApp::KillChallengeScreen()
{
	if (mChallengeScreen)
	{
		mWidgetManager->RemoveWidget(mChallengeScreen.get());
		SafeDeleteWidget(mChallengeScreen.release());
	}
}

StoreScreen* LawnApp::ShowStoreScreen()
{
	//FinishModelessDialogs();
	PVZP_ASSERT(!GetDialog(static_cast<int>(Dialogs::DIALOG_STORE)));

	StoreScreen* aStoreScreen = new StoreScreen(this);
	AddDialog(aStoreScreen);
	mWidgetManager->SetFocus(aStoreScreen);

	return aStoreScreen;
}

void LawnApp::KillStoreScreen()
{
	if (GetDialog(Dialogs::DIALOG_STORE))
	{
		KillDialog(Dialogs::DIALOG_STORE);
		ClearUpdateBacklog(false);
	}
}

void LawnApp::ShowSeedChooserScreen()
{
	PVZP_ASSERT(mSeedChooserScreen == nullptr);

	mSeedChooserScreen = std::make_unique<SeedChooserScreen>();
	mSeedChooserScreen->Resize(0, 0, mWidth, mHeight);
	mWidgetManager->AddWidget(mSeedChooserScreen.get());
	mWidgetManager->BringToBack(mSeedChooserScreen.get());
}

void LawnApp::KillSeedChooserScreen()
{
	if (mSeedChooserScreen)
	{
		mWidgetManager->RemoveWidget(mSeedChooserScreen.get());
		SafeDeleteWidget(mSeedChooserScreen.release());
	}
}

void LawnApp::EndLevel()
{
	Coop::Game& aCoop = Coop::Game::Get();
	if (aCoop.IsFollower())
		return;  // the host retries; the client follows its announcement

	if (aCoop.IsConnected() && !mCoopRunningDeferredEndLevel)
	{
		// In co-op this is reached from a click replayed inside Board::Update
		// -- the "zombies ate your brains" screen -- and rebuilding the board
		// from inside its own update pulls the floor out from under it,
		// lockstep included. Do it once the frame is done instead.
		mCoopPendingEndLevel = true;
		return;
	}

	KillBoard();

	// In co-op every retry goes through NewGame, whatever the mode: it is
	// what announces the level, and a board built any other way is one the
	// joining player is never told about -- they stay on Game Over while the
	// host plays on alone.
	if (aCoop.IsConnected())
	{
		NewGame();
		mFirstTimeGameSelector = true;
		return;
	}

	if (IsAdventureMode())
	{
		NewGame();
	}

	mFirstTimeGameSelector = true;

	MakeNewBoard();
	mBoard->InitLevel();
	mBoardResult = BoardResult::BOARDRESULT_NONE;
	mGameScene = GameScenes::SCENE_LEVEL_INTRO;
	ShowSeedChooserScreen();
	mBoard->mCutScene->StartLevelIntro();
}

void LawnApp::DoBackToMain()
{
	// The host leaving a level takes the client with it, or the client sits
	// in a level that no longer exists on the other machine, stalling forever
	// on input that is never coming.
	Coop::Game& aCoop = Coop::Game::Get();
	if (aCoop.IsConnected() && aCoop.IsHost())
	{
		aCoop.HostLeaveLevel();
		aCoop.EndLevel();
	}
	else if (aCoop.IsFollower())
	{
		// The joining player has no "main menu" of their own to go to: the
		// host decides where both of them are. Asking for it means leaving
		// the session. CoopUpdateFlow sees the drop and tidies up.
		aCoop.GetSession().Disconnect("You left the co-op game.");
		return;
	}

	mMusic->StopAllMusic();
	mSoundSystem->CancelPausedFoley();
	WriteCurrentUserConfig();
	KillNewOptionsDialog();
	KillBoard();
	ShowGameSelector();
}

void LawnApp::DoConfirmBackToMain()
{
	LawnDialog* aDialog = (LawnDialog*)DoDialog(
		Dialogs::DIALOG_CONFIRM_BACK_TO_MAIN,
		true,
		GetString("LEAVE_GAME_HEADER", "Leave Game?"),
		GetString("LEAVE_GAME",
			"Do you want to return\nto the main menu?\n\nYour game will be saved."),
		"",
		Dialog::BUTTONS_YES_NO
	);

	aDialog->mLawnYesButton->mLabel = PvzpStringTranslate("[LEAVE_BUTTON]");
	aDialog->mLawnNoButton->mLabel = PvzpStringTranslate("[DIALOG_BUTTON_CANCEL]");
	//aDialog->CalcSize(0, 0);
}

void LawnApp::DoNewOptions(bool theFromGameSelector)
{
	//FinishModelessDialogs();

	NewOptionsDialog* aDialog = new NewOptionsDialog(this, theFromGameSelector);
	CenterDialog(aDialog, IMAGE_OPTIONS_MENUBACK->mWidth, IMAGE_OPTIONS_MENUBACK->mHeight);
	AddDialog(Dialogs::DIALOG_NEWOPTIONS, aDialog);
	mWidgetManager->SetFocus(aDialog);

	// While this player is in the menu their pointer is not on the lawn, so
	// the other screen hides it rather than leave it frozen where it was
	// (SPEC.md section 7). It comes back with their next move on the board.
	if (Coop::Game::Get().IsInGame())
		Coop::Game::Get().GetLockstep().QueueLocalCommand(Coop::CommandType::EnterMenu, 0, 0);
}

void LawnApp::ShowZombatarTOS()
{
	ZombatarTOS* aDialog = new ZombatarTOS(this);
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
	AddDialog(Dialogs::DIALOG_ZOMBATAR_TOS, aDialog);
	mWidgetManager->SetFocus(aDialog);
}

AlmanacDialog* LawnApp::DoAlmanacDialog(SeedType theSeedType, ZombieType theZombieType)
{
	PerfTimer mTimer;
	mTimer.Start();

	//FinishModelessDialogs();

	AlmanacDialog* aDialog = new AlmanacDialog(this);
	AddDialog(Dialogs::DIALOG_ALMANAC, aDialog);
	mWidgetManager->SetFocus(aDialog);

	if (theSeedType != SeedType::SEED_NONE)
	{
		aDialog->ShowPlant(theSeedType);
	}
	else if (theZombieType != ZombieType::ZOMBIE_INVALID)
	{
		aDialog->ShowZombie(theZombieType);
	}

	int aDuration = mTimer.GetDuration();
	PvzpLogLn("almanac load time: {} ms", aDuration);

	return aDialog;
}

void LawnApp::DoContinueDialog()
{
	ContinueDialog* aDialog = new ContinueDialog(this);
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
	AddDialog(Dialogs::DIALOG_CONTINUE, aDialog);
	mWidgetManager->SetFocus(aDialog);
}

void LawnApp::DoPauseDialog()
{
	mBoard->Pause(true);
	//FinishModelessDialogs();

	LawnDialog* aDialog = (LawnDialog*)DoDialog(
		Dialogs::DIALOG_PAUSED,
		true,
		GetString("GAME_PAUSED", "GAME PAUSED"),
		GetString("CLICK_TO_RESUME", "Click to resume game"),
		GetString("RESUME_GAME", "Resume Game"),
		Dialog::BUTTONS_FOOTER
	);

	aDialog->mReanimation->AddReanimation(72.0f, 42.0f, ReanimationType::REANIM_ZOMBIE_NEWSPAPER);
	aDialog->mSpaceAfterHeader = 155;
	aDialog->CalcSize(0, 10);
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
}

int LawnApp::LawnMessageBox(int theDialogId, const char* theHeaderName, const char* theLinesName, const char* theButton1Name, const char* theButton2Name, int theButtonMode)
{
	Widget* aOldFocus = mWidgetManager->mFocusWidget;

	LawnDialog* aDialog = (LawnDialog*)DoDialog(theDialogId, true, theHeaderName, theLinesName, theButton1Name, theButtonMode);
	if (aDialog->mLawnYesButton)
	{
		aDialog->mLawnYesButton->mLabel = PvzpStringTranslate(theButton1Name);
	}
	if (aDialog->mLawnNoButton)
	{
		aDialog->mLawnNoButton->mLabel = PvzpStringTranslate(theButton2Name);
	}
	//aDialog->CalcSize(0, 0);

	mWidgetManager->SetFocus(aDialog);
	int aResult = aDialog->WaitForResult(true);
	mWidgetManager->SetFocus(aOldFocus);

	return aResult;
}

Dialog* LawnApp::DoDialog(int theDialogId, bool isModal, const std::string& theDialogHeader, const std::string& theDialogLines, const std::string& theDialogFooter, int theButtonMode)
{
	std::string aHeader(PvzpStringTranslate(theDialogHeader));
	std::string aLines(PvzpStringTranslate(theDialogLines));
	std::string aFooter(PvzpStringTranslate(theDialogFooter));

	Dialog* aDialog = SexyAppBase::DoDialog(theDialogId, isModal, aHeader, aLines, aFooter, theButtonMode);
	if (mWidgetManager->mFocusWidget == nullptr)
	{
		mWidgetManager->mFocusWidget = aDialog;
	}

	return aDialog;
}

Dialog* LawnApp::DoDialogDelay(int theDialogId, bool isModal, const std::string& theDialogHeader, const std::string& theDialogLines, const std::string& theDialogFooter, int theButtonMode)
{
	LawnDialog* aDialog = (LawnDialog*)SexyAppBase::DoDialog(theDialogId, isModal, theDialogHeader, theDialogLines, theDialogFooter, theButtonMode);
	aDialog->SetButtonDelay(30);
	return aDialog;
}

void LawnApp::DoUserDialog()
{
	KillDialog(Dialogs::DIALOG_USERDIALOG);

	UserDialog* aDialog = new UserDialog(this);
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
	AddDialog(Dialogs::DIALOG_USERDIALOG, aDialog);
	mWidgetManager->SetFocus(aDialog);
}

void LawnApp::FinishUserDialog(bool isYes)
{
	UserDialog* aUserDialog = (UserDialog*)GetDialog(Dialogs::DIALOG_USERDIALOG);
	if (aUserDialog)
	{
		if (isYes)
		{
			PlayerInfo* aProfile = mProfileMgr->GetProfile(aUserDialog->GetSelName());
			if (aProfile)
			{
				mPlayerInfo = aProfile;
				mWidgetManager->MarkAllDirty();

				if (mGameSelector)
				{
					mGameSelector->SyncProfile(true);
				}
			}
		}

		KillDialog(Dialogs::DIALOG_USERDIALOG);
	}
}

void LawnApp::DoCreateUserDialog()
{
	KillDialog(Dialogs::DIALOG_CREATEUSER);

	NewUserDialog* aDialog = new NewUserDialog(this, false);
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
	AddDialog(Dialogs::DIALOG_CREATEUSER, aDialog);
}

void LawnApp::FinishCreateUserDialog(bool isYes)
{
	NewUserDialog* aNewUserDialog = (NewUserDialog*)GetDialog(Dialogs::DIALOG_CREATEUSER);
	if (aNewUserDialog == nullptr)
		return;

	std::string aName = aNewUserDialog->GetName();

	if (isYes && aName.empty())
	{
		DoDialog(
			Dialogs::DIALOG_CREATEUSERERROR,
			true,
			GetString("ENTER_YOUR_NAME", "Enter Your Name"),
			GetString("USER_ERROR_MESSAGE",
				"Please enter your name to create a new user profile for storing high score data and game progress."),
			"[DIALOG_BUTTON_OK]",
			Dialog::BUTTONS_FOOTER
		);
	}
	else if (mPlayerInfo == nullptr && (!isYes || aName.empty()))
	{
		DoDialog(
			Dialogs::DIALOG_CREATEUSERERROR,
			true,
			GetString("ENTER_YOUR_NAME", "Enter Your Name"),
			GetString("USER_ERROR_MESSAGE",
				"Please enter your name to create a new user profile for storing high score data and game progress."),
			"[DIALOG_BUTTON_OK]",
			Dialog::BUTTONS_FOOTER
		);
	}
	else if (!isYes)
	{
		KillDialog(Dialogs::DIALOG_CREATEUSER);
	}
	else
	{
		PlayerInfo* aProfile = mProfileMgr->AddProfile(aName);
		if (aProfile == nullptr)
		{
			DoDialog(
				Dialogs::DIALOG_CREATEUSERERROR,
				true,
				GetString("NAME_CONFLICT", "Name Conflict"),
				GetString("ENTER_UNIQUE_PLAYER_NAME",
					"The name you entered is already being used.  Please enter a unique player name."),
				"[DIALOG_BUTTON_OK]",
				Dialog::BUTTONS_FOOTER
			);
		}
		else
		{
			mProfileMgr->Save();
			mPlayerInfo = aProfile;

			KillDialog(Dialogs::DIALOG_USERDIALOG);
			KillDialog(Dialogs::DIALOG_CREATEUSER);
			mWidgetManager->MarkAllDirty();

			if (mGameSelector)
			{
				mGameSelector->SyncProfile(true);
			}
		}
	}
}

std::string LawnApp::GetFormattedString(std::string_view theComponentId, std::string_view theDefault, ...)
{
	std::string aFormat = GetString(theComponentId, theDefault);

	va_list args;
	va_start(args, theDefault);

	va_list argsCopy;
	va_copy(argsCopy, args);
#ifdef _WIN32
	int required = _vscprintf(aFormat.c_str(), argsCopy);
#else
	int required = vsnprintf(nullptr, 0, aFormat.c_str(), argsCopy);
#endif
	va_end(argsCopy);

	std::string aResult;
	if (required > 0)
	{
		aResult.resize((size_t)required + 1);
#ifdef _WIN32
		_vsnprintf(aResult.data(), (size_t)required + 1, aFormat.c_str(), args);
#else
		vsnprintf(aResult.data(), (size_t)required + 1, aFormat.c_str(), args);
#endif
		aResult.resize((size_t)required);
	}

	va_end(args);
	return aResult;
}

void LawnApp::DoConfirmDeleteUserDialog(const std::string& theName)
{
	KillDialog(Dialogs::DIALOG_CONFIRMDELETEUSER);
	DoDialog(
		Dialogs::DIALOG_CONFIRMDELETEUSER,
		true,
		GetString("ARE_YOU_SURE", "Are You Sure?"),
		GetFormattedString("DELETE_USER_WARNING", "This will permanently remove '%s' from the player roster!", theName.c_str()),
		"",
		Dialog::BUTTONS_YES_NO
	);
}

void LawnApp::FinishConfirmDeleteUserDialog(bool isYes)
{
	KillDialog(Dialogs::DIALOG_CONFIRMDELETEUSER);
	UserDialog* aUserDialog = (UserDialog*)GetDialog(Dialogs::DIALOG_USERDIALOG);
	if (aUserDialog == nullptr)
		return;

	mWidgetManager->SetFocus(aUserDialog);

	if (!isYes)
		return;

	std::string aCurName = mPlayerInfo ? mPlayerInfo->mName : "";
	std::string aName = aUserDialog->GetSelName();
	if (aName == aCurName)
	{
		mPlayerInfo = nullptr;
	}

	mProfileMgr->DeleteProfile(aName);
	aUserDialog->FinishDeleteUser();
	if (mPlayerInfo == nullptr)
	{
		mPlayerInfo = mProfileMgr->GetProfile(aUserDialog->GetSelName());
		if (mPlayerInfo == nullptr)
		{
			mPlayerInfo = mProfileMgr->GetAnyProfile();
		}
	}

	mProfileMgr->Save();
	if (mPlayerInfo == nullptr)
	{
		DoCreateUserDialog();
	}

	mWidgetManager->MarkAllDirty();
	if (mGameSelector != nullptr)
	{
		mGameSelector->SyncProfile(true);
	}
}

// Wipes every profile's progress -- the adventure, coins, upgrades,
// trophies, the Zen Garden, Luca, any level saved part-way -- and starts again
// as the same player, so the menu comes straight back instead of asking for a
// name. Settings (volume, full screen) are not saves and are kept.
void LawnApp::DeleteAllSaves()
{
	const std::string aName = mPlayerInfo ? mPlayerInfo->mName : std::string();
	mPlayerInfo = nullptr;

	std::vector<std::string> aNames;
	for (const auto& anEntry : mProfileMgr->GetProfileMap())
		aNames.push_back(anEntry.first);
	for (const std::string& aProfileName : aNames)
		mProfileMgr->DeleteProfile(aProfileName);  // its files go with it

	if (!aName.empty())
		mPlayerInfo = mProfileMgr->AddProfile(aName);
	mProfileMgr->Save();
	if (mPlayerInfo != nullptr)
		WriteCurrentUserConfig();
	else
		DoCreateUserDialog();

	mWidgetManager->MarkAllDirty();
	if (mGameSelector != nullptr)
		mGameSelector->SyncProfile(true);
}

void LawnApp::DoRenameUserDialog(const std::string& theName)
{
	KillDialog(Dialogs::DIALOG_RENAMEUSER);

	NewUserDialog* aDialog = new NewUserDialog(this, true);
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
	aDialog->SetName(theName);
	AddDialog(Dialogs::DIALOG_RENAMEUSER, aDialog);
}

void LawnApp::FinishRenameUserDialog(bool isYes)
{
	UserDialog* aUserDialog = (UserDialog*)GetDialog(Dialogs::DIALOG_USERDIALOG);
	if (!isYes)
	{
		KillDialog(Dialogs::DIALOG_RENAMEUSER);
		mWidgetManager->SetFocus(aUserDialog);
		return;
	}

	NewUserDialog* aNewUserDialog = (NewUserDialog*)GetDialog(Dialogs::DIALOG_RENAMEUSER);
	if (aUserDialog == nullptr || aNewUserDialog == nullptr)
		return;

	std::string anOldName = aUserDialog->GetSelName();
	std::string aNewName = aNewUserDialog->GetName();
	if (aNewName.empty())
		return;

	bool isCurrentUser = mProfileMgr->GetProfile(anOldName) == mPlayerInfo;
	if (!mProfileMgr->RenameProfile(anOldName, aNewName))
	{
		DoDialog(
			Dialogs::DIALOG_RENAMEUSERERROR,
			true,
			GetString("NAME_CONFLICT", "Name Conflict"),
			GetString("ENTER_UNIQUE_PLAYER_NAME",
				"The name you entered is already being used.  Please enter a unique player name."),
			"[DIALOG_BUTTON_OK]",
			Dialog::BUTTONS_FOOTER
		);
		return;
	}

	mProfileMgr->Save();
	if (isCurrentUser)
	{
		mPlayerInfo = mProfileMgr->GetProfile(aNewName);
	}

	aUserDialog->FinishRenameUser(aNewName);
	mWidgetManager->MarkAllDirty();
	KillDialog(Dialogs::DIALOG_RENAMEUSER);
	mWidgetManager->SetFocus(aUserDialog);
}

void LawnApp::FinishNameError(int theId)
{
	KillDialog(theId);

	NewUserDialog* aNewUserDialog = (NewUserDialog*)GetDialog(theId == Dialogs::DIALOG_CREATEUSERERROR ? Dialogs::DIALOG_CREATEUSER : Dialogs::DIALOG_RENAMEUSER);
	if (aNewUserDialog)
	{
		mWidgetManager->SetFocus(aNewUserDialog->mNameEditWidget.get());
	}
}

void LawnApp::FinishRestartConfirmDialog()
{
	mSawYeti = mBoard->mKilledYeti;

	KillDialog(Dialogs::DIALOG_CONTINUE);
	KillDialog(Dialogs::DIALOG_RESTARTCONFIRM);
	KillBoard();

	PreNewGame(mGameMode, false);
}

void LawnApp::DoCheatDialog()
{
	KillDialog(Dialogs::DIALOG_CHEAT);

	CheatDialog* aDialog = new CheatDialog(this);
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
	AddDialog(Dialogs::DIALOG_CHEAT, aDialog);
}

void LawnApp::FinishCheatDialog(bool isYes)
{
	CheatDialog* aCheatDialog = (CheatDialog*)GetDialog(Dialogs::DIALOG_CHEAT);
	if (aCheatDialog == nullptr)
		return;

	if (isYes && !aCheatDialog->ApplyCheat())
		return;

	KillDialog(Dialogs::DIALOG_CHEAT);
	if (isYes)
	{
		mMusic->StopAllMusic();
		mBoardResult = BoardResult::BOARDRESULT_CHEAT;
		PreNewGame(mGameMode, false);
	}
}

void LawnApp::FinishTimesUpDialog()
{
	KillDialog(Dialogs::DIALOG_TIMESUP);
}

void LawnApp::DoConfirmSellDialog(const std::string& theMessage)
{
	DoDialog(Dialogs::DIALOG_ZEN_SELL, true, "[ZEN_SELL_HEADER]", theMessage, "", Dialog::BUTTONS_YES_NO);
}

Dialog* LawnApp::NewDialog(int theDialogId, bool isModal, const std::string& theDialogHeader, const std::string& theDialogLines, const std::string& theDialogFooter, int theButtonMode)
{
	LawnDialog* aDialog = new LawnDialog(
		this,
		theDialogId,
		isModal,
		theDialogHeader,
		theDialogLines,
		theDialogFooter,
		theButtonMode
	);

	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
	return aDialog;
}

bool LawnApp::KillNewOptionsDialog()
{
	NewOptionsDialog* aNewOptionsDialog = (NewOptionsDialog*)GetDialog(Dialogs::DIALOG_NEWOPTIONS);
	if (aNewOptionsDialog == nullptr)
		return false;

	bool wantWindowed = !aNewOptionsDialog->mFullscreenCheckbox->IsChecked();
	bool want3D = aNewOptionsDialog->mHardwareAccelerationCheckbox->IsChecked();
	SwitchScreenMode(wantWindowed, want3D, false);

	KillDialog(Dialogs::DIALOG_NEWOPTIONS);
	ClearUpdateBacklog();
	return true;
}

bool LawnApp::KillAlmanacDialog()
{
	if (GetDialog(Dialogs::DIALOG_ALMANAC))
	{
		KillDialog(Dialogs::DIALOG_ALMANAC);
		ClearUpdateBacklog(false);
		return true;
	}

	return false;
}

bool LawnApp::NeedPauseGame()
{
	if (mDialogList.size() == 0)
		return false;

	if (mDialogList.size() == 1 && mDialogList.front()->mId != Dialogs::DIALOG_NEW_GAME)
	{
		int anId = mDialogList.front()->mId;
		if (anId == Dialogs::DIALOG_CHOOSER_WARNING || anId == Dialogs::DIALOG_PURCHASE_PACKET_SLOT || anId == Dialogs::DIALOG_IMITATER)
		{
			return false;
		}
	}

	return (mBoard == nullptr || mGameMode != GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN) && (mBoard == nullptr || mGameMode != GameMode::GAMEMODE_TREE_OF_WISDOM);
}

void LawnApp::ModalOpen()
{
	if (mBoard && NeedPauseGame())
	{
		mBoard->Pause(true);
	}
}

void LawnApp::ModalClose()
{
	if (mBoard && !NeedPauseGame())
	{
	mBoard->Pause(false);
	}
}

bool LawnApp::KillDialog(int theDialogId)
{
	if (SexyAppBase::KillDialog(theDialogId))
	{
		if (mDialogMap.size() == 0 && mWidgetManager->mFocusWidget == nullptr)
		{
			if (mBoard)
			{
				mWidgetManager->SetFocus(mBoard);
			}
			else if (mGameSelector)
			{
				mWidgetManager->SetFocus(mGameSelector.get());
			}
		}

		if (mBoard && !NeedPauseGame())
		{
			mBoard->Pause(false);
		}

		return true;
	}

	return false;
}

void LawnApp::ShowResourceError(bool doExit)
{
	SexyAppBase::ShowResourceError(doExit);
}

void LawnApp::Init()
{
	DoParseCmdLine();
	if (!mCheatKeys)
	{
		mOnlyAllowOneCopyToRun = true;
	}

	mSessionID = time(0);
	mPlayTimeActiveSession = 0;
	mPlayTimeInactiveSession = 0;
	mBoardResult = BoardResult::BOARDRESULT_NONE;
	mSawYeti = false;

	SexyApp::Init();

	if (mShutdown) // MakeWindow() failed
		return;

	if (mRecordingDemoBuffer || mPlayingDemoBuffer)
		mAppRandSeed = mRandSeed; // demo sessions derive the app-level seed from the recorded one

	Sexy::MkDir(Sexy::GetAppDataPath("userdata"));
#ifdef PVZ_DEBUG
	Sexy::RegisterLogFileSink(Sexy::GetAppDataPath("userdata/") + "log.txt");
	PvzpLogLn("Started {}", static_cast<uint64_t>(std::time(nullptr)));
#endif
	PvzpLogLn("session id: {}", static_cast<long long>(mSessionID));

	if (!mResourceManager->ParseResourcesFile("properties/resources.xml"))
	{
		ShowResourceError(true);
		return;
	}

	if (!PvzpLoadResources("Init"))
	{
		return;
	}

	PerfTimer mTimer;
	mTimer.Start();

	mProfileMgr->Load();

	std::string aCurUser;
	if (mPlayerInfo == nullptr && RegistryReadString("CurUser", &aCurUser))
	{
		mPlayerInfo = mProfileMgr->GetProfile(aCurUser);
	}
	if (mPlayerInfo == nullptr)
	{
		mPlayerInfo = mProfileMgr->GetAnyProfile();
	}

	mMaxExecutions = GetInteger("MaxExecutions", 0);
	mMaxPlays = GetInteger("MaxPlays", 0);
	mMaxTime = GetInteger("MaxTime", 60);

	mTitleScreen = std::make_unique<TitleScreen>(this);
	mTitleScreen->Resize(0, 0, mWidth, mHeight);
	mWidgetManager->AddWidget(mTitleScreen.get());
	mWidgetManager->SetFocus(mTitleScreen.get());

#ifdef PVZ_DEBUG
	int aDuration = mTimer.GetDuration();
	PvzpLogLn("loading: 'profiles' {} ms", aDuration);
#endif
	mTimer.Start();

	mMusic = std::make_unique<Music>();
	mSoundSystem = std::make_unique<PvzpFoley>();
	mEffectSystem = std::make_unique<EffectSystem>();
	mEffectSystem->EffectSystemInitialize();

	mKonamiCheck = std::make_unique<TypingCheck>();
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_UP);
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_UP);
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_DOWN);
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_DOWN);
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_LEFT);
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_RIGHT);
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_LEFT);
	mKonamiCheck->AddKeyCode(KeyCode::KEYCODE_RIGHT);
	mKonamiCheck->AddChar('b');
	mKonamiCheck->AddChar('a');
	mMustacheCheck = std::make_unique<TypingCheck>("mustache");
	mMoustacheCheck = std::make_unique<TypingCheck>("moustache");
	mSuperMowerCheck = std::make_unique<TypingCheck>("trickedout");
	mSuperMowerCheck2 = std::make_unique<TypingCheck>("tricked out");
	mFutureCheck = std::make_unique<TypingCheck>("future");
	mPinataCheck = std::make_unique<TypingCheck>("pinata");
	mDanceCheck = std::make_unique<TypingCheck>("dance");
	mDaisyCheck = std::make_unique<TypingCheck>("daisies");
	mSukhbirCheck = std::make_unique<TypingCheck>("sukhbir");

#ifdef PVZ_DEBUG
	aDuration = mTimer.GetDuration();
	PvzpLogLn("loading: 'system' {} ms", aDuration);
#endif
	mTimer.Start();

	ReanimatorLoadDefinitions(gLawnReanimationArray, ReanimationType::NUM_REANIMS);
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_LOADBAR_SPROUT, true);
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_LOADBAR_ZOMBIEHEAD, true);

#ifdef PVZ_DEBUG
	aDuration = mTimer.GetDuration();
	PvzpLogLn("loading: 'loaderbar' {} ms", aDuration);
#endif
	mTimer.Start();
}

bool LawnApp::ChangeDirHook(const char* /*theIntendedPath*/)
{
	return false;
}

void LawnApp::Start()
{
	if (mLoadingFailed)
		return;

	SexyAppBase::Start();
}

bool LawnApp::DebugKeyDown(int theKey)
{
	return SexyAppBase::DebugKeyDown(theKey);
}

void LawnApp::HandleCmdLineParam(std::string_view theParamName, std::string_view theParamValue)
{
	if (theParamName == "-cheat")
	{
#ifdef PVZ_DEBUG
		mCheatKeys = true;
		mDebugKeysEnabled = true;
#endif
	}
	else
	{
		SexyApp::HandleCmdLineParam(theParamName, theParamValue);
	}
}

bool LawnApp::UpdatePlayerProfileForFinishingLevel()
{
	bool aUnlockedNewChallenge = false;

	if (IsAdventureMode())
	{
		if (mBoard->mLevel == FINAL_LEVEL)
		{
			mPlayerInfo->SetLevel(1);
			mPlayerInfo->mFinishedAdventure++;
			if (mPlayerInfo->mFinishedAdventure == 1)
			{
				mPlayerInfo->mNeedsMessageOnGameSelector = 1;
			}
			ReportAchievement::GiveAchievement(this, HomeSecurity, false);
		}
		else
		{
			mPlayerInfo->SetLevel(mBoard->mLevel + 1);
		}

		if (!HasFinishedAdventure() && mBoard->mLevel == 34)
		{
			mPlayerInfo->mNeedsMagicTacoReward = 1;
		}
	}
	else if (IsSurvivalMode())
	{
		if (mBoard->IsFinalSurvivalStage())
		{
			aUnlockedNewChallenge = !HasBeatenChallenge(mGameMode);
			mBoard->SurvivalSaveScore();

			if (aUnlockedNewChallenge && HasFinishedAdventure())
			{
				int aNumTrophies = GetNumTrophies(ChallengePage::CHALLENGE_PAGE_SURVIVAL);
				if (aNumTrophies != 8 && aNumTrophies != 9)
				{
					mPlayerInfo->mHasNewSurvival = true;
				}
			}
		}
	}
	else if (IsPuzzleMode())
	{
		aUnlockedNewChallenge = !HasBeatenChallenge(mGameMode);
		mPlayerInfo->mChallengeRecords[GetCurrentChallengeIndex()]++;

		if (!HasFinishedAdventure() && (mGameMode == GameMode::GAMEMODE_SCARY_POTTER_3 || mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_3))
		{
			aUnlockedNewChallenge = false;
		}

		if (aUnlockedNewChallenge)
		{
			if (IsScaryPotterLevel())
			{
				mPlayerInfo->mHasNewScaryPotter = 1;
			}
			else
			{
				mPlayerInfo->mHasNewIZombie = 1;
			}
		}
	}
	else if (mGameMode == GameMode::GAMEMODE_COOP_ONSLAUGHT)
	{
		// What Gargantuar Onslaught is for. Kept in the purchases, so it saves
		// with the profile and reaches the joining player with the rest.
		aUnlockedNewChallenge = mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_UNLOCK_LUCA] == 0;
		mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_UNLOCK_LUCA] = 1;
	}
	else if (mGameMode == GameMode::GAMEMODE_COOP_SANDBOX)
	{
		// Nothing to earn in the sandbox.
	}
	else
	{
		aUnlockedNewChallenge = !HasBeatenChallenge(mGameMode);
		mPlayerInfo->mChallengeRecords[GetCurrentChallengeIndex()]++;

		if (aUnlockedNewChallenge && HasFinishedAdventure())
		{
			int aNumTrophies = GetNumTrophies(ChallengePage::CHALLENGE_PAGE_CHALLENGE);
			if (aNumTrophies <= 17)
			{
				mPlayerInfo->mHasNewMiniGame = 1;
			}
		}

		int aNumTrophies = GetNumTrophies(ChallengePage::CHALLENGE_PAGE_CHALLENGE);
		if (aNumTrophies == 20)
			ReportAchievement::GiveAchievement(this, BeyondTheGrave, false);
	}

	if ((IsAdventureMode() || IsSurvivalMode()) && !IsScaryPotterLevel() && !IsWhackAZombieLevel()) {
		if (mBoard->StageIsDayWithPool() && !mBoard->mPeaShooterUsed) {
			ReportAchievement::GiveAchievement(this, DontPea, false);
		} else if (mBoard->StageHasRoof() && !mBoard->HasConveyorBeltSeedBank() && !mBoard->mCatapultPlantsUsed) {
			ReportAchievement::GiveAchievement(this, Grounded, false);
		} else if (mBoard->StageIsDayWithoutPool() && mBoard->mMushroomAndCoffeeBeansOnly) {
			ReportAchievement::GiveAchievement(this, GoodMorning, false);
		}
		if (mBoard->StageIsNight() && !mBoard->mMushroomsUsed) {
			ReportAchievement::GiveAchievement(this, NoFungusAmongUs, false);
		}
	}

	WriteCurrentUserConfig();

	return aUnlockedNewChallenge;
}

void LawnApp::CheckForGameEnd()
{
	if (mBoard == nullptr || !mBoard->mLevelComplete)
		return;

	// The joining machine finishes the level on the same tick as the host,
	// but where to go next -- award, store, credits, next level -- is the
	// host's decision. Running this here too would send the client off on its
	// own route and advance a profile it is only borrowing.
	if (Coop::Game::Get().IsFollower())
		return;

	bool aUnlockedNewChallenge = UpdatePlayerProfileForFinishingLevel();

	// Co-op rides the host's profile (SPEC.md section 10.4), so the host's
	// progress is the campaign's progress: write it the moment a level is
	// won, not whenever the game next happens to save. A dropped session then
	// resumes from the next level instead of replaying this one.
	if (Coop::Game::Get().IsConnected() && Coop::Game::Get().IsHost())
		WriteCurrentUserConfig();

	if (IsAdventureMode())
	{
		int aLevel = mBoard->mLevel;

		// Take the client out of the level before the host tears it down.
		// Everything below -- the award screen, Crazy Dave handing over a new
		// seed packet -- is host-only and can take a while, during which the
		// host stops ticking the simulation entirely. A client left in the
		// level just stalls there waiting for input that is never coming,
		// which is what turned a level transition into a black screen.
		Coop::Game& aCoop = Coop::Game::Get();
		if (aCoop.IsConnected() && aCoop.IsHost())
		{
			aCoop.HostLeaveLevel();
			aCoop.EndLevel();
		}

		KillBoard();

		if (IsFirstTimeAdventureMode() && aLevel < 50)
		{
			ShowAwardScreen(AwardType::AWARD_FORLEVEL, true);
		}
		else if (aLevel == FINAL_LEVEL)
		{
			if (mPlayerInfo->mFinishedAdventure == 1)
			{
				ShowAwardScreen(AwardType::AWARD_FORLEVEL, true);
			}
			else
			{
				ShowAwardScreen(AwardType::AWARD_CREDITS_ZOMBIENOTE, true);
			}
		}
		else if (aLevel == 9 || aLevel == 19 || aLevel == 29 || aLevel == 39 || aLevel == 49)
		{
			ShowAwardScreen(AwardType::AWARD_FORLEVEL, true);
		}
		else if (HasUnshownAchievements(mPlayerInfo))
		{
			ShowAwardScreen(AwardType::AWARD_ACHIEVEMENTONLY, true);
		}
		else
		{
			PreNewGame(mGameMode, false);
		}
	}
	else if (IsSurvivalMode())
	{
		if (mBoard->IsFinalSurvivalStage())
		{
			KillBoard();

			if (aUnlockedNewChallenge && HasFinishedAdventure())
			{
				ShowAwardScreen(AwardType::AWARD_FORLEVEL, true);
			}
			else if (HasUnshownAchievements(mPlayerInfo))
			{
				ShowAwardScreen(AwardType::AWARD_ACHIEVEMENTONLY, true);
			}
			else
			{
				ShowChallengeScreen(ChallengePage::CHALLENGE_PAGE_SURVIVAL);
			}
		}
		else
		{
			mBoard->mChallenge->mSurvivalStage++;
			KillGameSelector();
			mBoard->InitSurvivalStage();
		}
	}
	else if (IsPuzzleMode())
	{
		KillBoard();

		if (aUnlockedNewChallenge)
		{
			ShowAwardScreen(AwardType::AWARD_FORLEVEL, true);
		}
		else if (HasUnshownAchievements(mPlayerInfo))
		{
			ShowAwardScreen(AwardType::AWARD_ACHIEVEMENTONLY, true);
		}
		else
		{
			ShowChallengeScreen(ChallengePage::CHALLENGE_PAGE_PUZZLE);
		}
	}
	else if (IsCoopBonusLevel())
	{
		// As for an adventure level: the client leaves with us, then follows
		// us to the award or the menu.
		Coop::Game& aCoop = Coop::Game::Get();
		if (aCoop.IsConnected() && aCoop.IsHost())
		{
			aCoop.HostLeaveLevel();
			aCoop.EndLevel();
		}

		KillBoard();

		// Luca is awarded the first time Gargantuar Onslaught is beaten,
		// the way a campaign level hands over its new plant.
		if (mGameMode == GameMode::GAMEMODE_COOP_ONSLAUGHT && aUnlockedNewChallenge)
		{
			ShowAwardScreen(AwardType::AWARD_COOP_LUCA, true);
		}
		else
		{
			ShowGameSelector();
		}
	}
	else
	{
		KillBoard();

		if (aUnlockedNewChallenge && HasFinishedAdventure())
		{
			ShowAwardScreen(AwardType::AWARD_FORLEVEL, true);
		}
		else if (HasUnshownAchievements(mPlayerInfo))
		{
			ShowAwardScreen(AwardType::AWARD_ACHIEVEMENTONLY, true);
		}
		else
		{
			ShowChallengeScreen(ChallengePage::CHALLENGE_PAGE_CHALLENGE);
		}
	}
}

void LawnApp::UpdatePlayTimeStats()
{
	static int aLastTime = -1;

	int aTickCount = SDL_GetTicks();
	int aSession = (aTickCount - aLastTime) / 1000;

	if (mPlayerInfo && !mPlayerInfo->mHasUsedCheatKeys && !mDebugKeysEnabled && mCheatKeys)
	{
		mPlayerInfo->mHasUsedCheatKeys = 1;
	}

	if (aLastTime == -1)
	{
		aLastTime = aTickCount;
		return;
	}

	if (aSession > 0)
	{
		aLastTime = aTickCount;

		if ((mBoard == nullptr || !mBoard->mPaused) && mHasFocus && mLastTimerTime - mLastUserInputTick <= 10000)
		{
			mPlayTimeActiveSession += aSession;

			if (mBoard)
			{
				mBoard->mPlayTimeActiveLevel += aSession;
			}

			if (mPlayerInfo)
			{
				mPlayerInfo->mPlayTimeActivePlayer += aSession;
			}
		}
		else
		{
			mPlayTimeInactiveSession += aSession;

			if (mBoard)
			{
				mBoard->mPlayTimeInactiveLevel += aSession;
			}

			if (mPlayerInfo)
			{
				mPlayerInfo->mPlayTimeInactivePlayer += aSession;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Debug test script (PVZ_TEST_SCRIPT)
// ---------------------------------------------------------------------------
//
// Testing co-op means driving two game windows at once, and the only mouse on
// the machine is the tester's. With PVZ_TEST_SCRIPT naming a file, the game
// feeds itself timed input instead, straight into its own widget manager, so a
// two-window test runs the same way every time and nobody's mouse is borrowed.
// Off unless the variable is set. One step per line, '#' for comments:
//
//     <seconds> click <x> <y>     game coordinates (800x600)
//     <seconds> move <x> <y>
//     <seconds> key escape|space|return
//     <seconds> snap <png path>   capture the next frame to that file
//     <seconds> log <text>        a marker in stdout
//     <seconds> win               win the current level (both machines)
//     <seconds> lose              lose it: Game Over (both machines)
//     <seconds> clickzombie       click the leftmost zombie on the lawn
//     <seconds> tryagain          press Game Over's Try Again, if it is up
//
// Seconds count from when the game started.

namespace Sexy { void RequestFrameDump(const std::string& thePath); }

namespace
{
	struct TestStep
	{
		uint32_t    mAtMs = 0;
		std::string mVerb;
		int         mX = 0;
		int         mY = 0;
		std::string mText;
	};

	std::vector<TestStep> gTestSteps;
	size_t                gNextTestStep = 0;
	bool                  gTestScriptLoaded = false;

	void LoadTestScript()
	{
		gTestScriptLoaded = true;
		const char* aPath = getenv("PVZ_TEST_SCRIPT");
		if (aPath == nullptr || *aPath == '\0')
			return;

		FILE* aFile = fopen(aPath, "rb");
		if (aFile == nullptr)
		{
			printf("test script: cannot open %s\n", aPath);
			return;
		}

		char aLine[512];
		while (fgets(aLine, sizeof(aLine), aFile))
		{
			std::string aText = aLine;
			while (!aText.empty() && (aText.back() == '\n' || aText.back() == '\r'))
				aText.pop_back();
			if (aText.empty() || aText[0] == '#')
				continue;

			TestStep aStep;
			double aSeconds = 0;
			char aVerb[32] = {};
			int aConsumed = 0;
			if (sscanf(aText.c_str(), "%lf %31s %n", &aSeconds, aVerb, &aConsumed) < 2)
				continue;
			aStep.mAtMs = static_cast<uint32_t>(aSeconds * 1000.0);
			aStep.mVerb = aVerb;
			aStep.mText = aText.substr(static_cast<size_t>(aConsumed));
			sscanf(aStep.mText.c_str(), "%d %d", &aStep.mX, &aStep.mY);
			gTestSteps.push_back(aStep);
		}
		fclose(aFile);
		std::stable_sort(gTestSteps.begin(), gTestSteps.end(),
			[](const TestStep& a, const TestStep& b) { return a.mAtMs < b.mAtMs; });
		printf("test script: %zu steps from %s\n", gTestSteps.size(), aPath);
		fflush(stdout);
	}
}

void LawnApp::RunTestScript()
{
	if (!gTestScriptLoaded)
		LoadTestScript();
	if (gNextTestStep >= gTestSteps.size() || !mWidgetManager)
		return;

	const uint32_t aNow = SDL_GetTicks();
	while (gNextTestStep < gTestSteps.size() && gTestSteps[gNextTestStep].mAtMs <= aNow)
	{
		const TestStep& aStep = gTestSteps[gNextTestStep++];

		// Buttons only fire for a focused window, and of two windows side by
		// side only one can be. Scripted input is input to this window, so
		// present it as such for the moment it is delivered.
		struct FocusScope
		{
			WidgetManager* mManager;
			bool           mHadFocus;
			explicit FocusScope(WidgetManager* theManager) : mManager(theManager), mHadFocus(theManager->mHasFocus) { mManager->mHasFocus = true; }
			~FocusScope() { mManager->mHasFocus = mHadFocus; }
		} aFocusScope(mWidgetManager.get());

		if (aStep.mVerb == "click")
		{
			mWidgetManager->mMouseIn = true;
			mWidgetManager->MouseMove(aStep.mX, aStep.mY);
			mWidgetManager->MouseDown(aStep.mX, aStep.mY, 1);
			mWidgetManager->MouseUp(aStep.mX, aStep.mY, 1);
		}
		else if (aStep.mVerb == "clickzombie")
		{
			// Zombies turn up wherever the level's random draw puts them, so a
			// fixed click position would mostly hit lawn.
			Zombie* aTarget = nullptr;
			if (mBoard)
			{
				for (Zombie* aZombie : mBoard->mZombies)
				{
					if (aZombie->mDead || aZombie->IsDeadOrDying() || aZombie->mPosX > 760.0f)
						continue;
					if (aTarget == nullptr || aZombie->mPosX < aTarget->mPosX)
						aTarget = aZombie;
				}
			}
			if (aTarget != nullptr)
			{
				const Rect aRect = aTarget->GetZombieRect();
				const int aX = aRect.mX + aRect.mWidth / 2 + mBoard->mX;
				const int aY = aRect.mY + aRect.mHeight / 2 + mBoard->mY;
				printf("test script: clickzombie type %d at %d,%d health %d\n",
					static_cast<int>(aTarget->mZombieType), aX, aY, aTarget->mBodyHealth);
				fflush(stdout);
				mWidgetManager->mMouseIn = true;
				mWidgetManager->MouseMove(aX, aY);
				mWidgetManager->MouseDown(aX, aY, 1);
				mWidgetManager->MouseUp(aX, aY, 1);
			}
		}
		else if (aStep.mVerb == "countzombies")
		{
			// How many zombies are alive, and how many are Gargantuars.
			int aTotal = 0;
			int aGargantuars = 0;
			if (mBoard)
			{
				for (Zombie* aZombie : mBoard->mZombies)
				{
					if (aZombie->mDead || aZombie->IsDeadOrDying())
						continue;
					aTotal++;
					if (aZombie->mZombieType == ZombieType::ZOMBIE_GARGANTUAR)
						aGargantuars++;
				}
			}
			printf("test script: zombies %d, gargantuars %d, wave %d of %d\n", aTotal, aGargantuars,
				mBoard ? mBoard->mCurrentWave : -1, mBoard ? mBoard->mNumWaves : -1);
			fflush(stdout);
		}
		else if (aStep.mVerb == "tryagain")
		{
			// Game Over's Try Again, as a click on it would press it.
			if (Dialog* aGameOver = GetDialog(Dialogs::DIALOG_GAME_OVER))
			{
				printf("test script: tryagain at %u ms\n", SDL_GetTicks());
				fflush(stdout);
				aGameOver->ButtonDepress(Dialog::ID_FOOTER);
			}
		}
		else if (aStep.mVerb == "gameovermenu")
		{
			// Game Over's Main Menu button (its id is 1).
			if (Dialog* aGameOver = GetDialog(Dialogs::DIALOG_GAME_OVER))
			{
				printf("test script: gameovermenu at %u ms\n", SDL_GetTicks());
				fflush(stdout);
				aGameOver->ButtonDepress(1);
			}
		}
		else if (aStep.mVerb == "move")
		{
			mWidgetManager->mMouseIn = true;
			mWidgetManager->MouseMove(aStep.mX, aStep.mY);
		}
		else if (aStep.mVerb == "key")
		{
			KeyCode aKey = KeyCode::KEYCODE_UNKNOWN;
			if (aStep.mText.rfind("escape", 0) == 0) aKey = KeyCode::KEYCODE_ESCAPE;
			else if (aStep.mText.rfind("space", 0) == 0) aKey = KeyCode::KEYCODE_SPACE;
			else if (aStep.mText.rfind("return", 0) == 0) aKey = KeyCode::KEYCODE_RETURN;
			if (aKey != KeyCode::KEYCODE_UNKNOWN)
			{
				mWidgetManager->KeyDown(aKey);
				mWidgetManager->KeyUp(aKey);
			}
		}
		else if (aStep.mVerb == "win")
		{
			// Through the lockstep in co-op, so both machines win together.
			if (Coop::Game::Get().IsInGame())
				Coop::Game::Get().GetLockstep().QueueLocalCommand(Coop::CommandType::DebugWin, 0, 0);
			else if (mBoard)
				mBoard->DebugWinLevel();
		}
		else if (aStep.mVerb == "lose")
		{
			// Game Over on demand, through the lockstep like "win".
			if (Coop::Game::Get().IsInGame())
				Coop::Game::Get().GetLockstep().QueueLocalCommand(Coop::CommandType::DebugLose, 0, 0);
			else if (mBoard && mGameScene == GameScenes::SCENE_PLAYING)
				mBoard->ZombiesWon(nullptr);
		}
		else if (aStep.mVerb == "snap")
		{
			Sexy::RequestFrameDump(aStep.mText);
		}
		else if (aStep.mVerb == "log")
		{
			printf("test script: %s\n", aStep.mText.c_str());
			fflush(stdout);
		}
	}
}

void LawnApp::UpdateFrames()
{
	RunTestScript();

	// Drives the connection even outside a level, so the handshake finishes
	// and a disconnect is noticed promptly.
	Coop::Game::Get().Update();
	CoopUpdateFlow();
	CoopRefreshWindowTitle();

	if ((!mActive || mMinimized) && mBoard)
	{
		mBoard->ResetFPSStats();
	}

#ifdef PVZ_DEBUG
	UpdatePlayTimeStats();
#endif

	int aUpdateCount = 1;
	if (gSlowMo)
	{
		++gSlowMoCounter;
		if (gSlowMoCounter < 4)
		{
			aUpdateCount = 0;
		}
		else
		{
			gSlowMoCounter = 0;
		}
	}
	else if (gFastMo)
	{
		aUpdateCount = 20;
	}

	for (int i = 0; i < aUpdateCount; i++)
	{
		mAppCounter++;

		if (mBoard)
		{
			mBoard->ProcessDeleteQueue();
		}
		if (mLoadingThreadCompleted && mEffectSystem)
		{
			mEffectSystem->ProcessDeleteQueue();
		}

		// A minimized window normally stops updating its widgets entirely.
		// In co-op that freezes the other player too -- they cannot advance
		// without our input -- so the game keeps running behind the taskbar.
		const bool aWasMinimized = mMinimized;
		if (Coop::Game::Get().IsConnected())
			mMinimized = false;
		SexyApp::UpdateFrames();
		mMinimized = aWasMinimized;

		// The games fell out of sync. That is a bug, but a dead session is
		// the worst way to find out: a level restarts deterministically from
		// its seed, so the host simply starts it again for both of them --
		// the same retry the "zombies ate your brains" screen uses.
		{
			Coop::Game& aCoop = Coop::Game::Get();
			if (aCoop.IsInGame() && aCoop.IsHost() && aCoop.GetLockstep().IsDesynced()
				&& mBoard && (IsAdventureMode() || IsCoopBonusLevel()) && !mCoopPendingEndLevel)
			{
				printf("co-op: %s Restarting the level.\n", aCoop.GetLockstep().GetDesyncText().c_str());
				fflush(stdout);
				mCoopResyncNoticeUntil = SDL_GetTicks() + 6000;
				mCoopPendingEndLevel = true;
			}
		}

		if (mCoopPendingEndLevel)
		{
			mCoopPendingEndLevel = false;
			mCoopRunningDeferredEndLevel = true;
			EndLevel();
			mCoopRunningDeferredEndLevel = false;
		}

		// Status text goes above everything, dialogs included. Created once
		// the fonts exist, and pulled back to the front every frame because
		// the board keeps lifting its dialogs over everything else.
		if (Coop::Game::Get().IsConnected() && mLoadingThreadCompleted && mWidgetManager)
		{
			if (!mCoopOverlay)
			{
				mCoopOverlay = std::make_unique<CoopOverlay>(this);
				mCoopOverlay->Resize(0, 0, mWidth, mHeight);
				mWidgetManager->AddWidget(mCoopOverlay.get());
			}
			mWidgetManager->BringToFront(mCoopOverlay.get());
		}

		mMusic->MusicUpdate();

		CheckForGameEnd();
	}
}

void LawnApp::ToggleSlowMo()
{
	gSlowMoCounter = 0;
	gSlowMo = !gSlowMo;
	gFastMo = false;
}

void LawnApp::ToggleFastMo()
{
	gSlowMo = false;
	gFastMo = !gFastMo;
}

void LawnApp::LoadGroup(const char* theGroupName, int theGroupAveMsToLoad)
{
	PerfTimer aTimer;
	aTimer.Start();

	mResourceManager->StartLoadResources(theGroupName);
	while (!mShutdown && !mCloseRequest && !mLoadingFailed && PvzpLoadNextResource())
	{
		mCompletedLoadingThreadTasks += theGroupAveMsToLoad;
	}

	if (mShutdown || mCloseRequest)
		return;

	if (mResourceManager->HadError() || !ExtractResourcesByName(mResourceManager.get(), theGroupName))
	{
		ShowResourceError();
		mLoadingFailed = true;
	}
}

void LawnApp::LoadingThreadProc()
{
	if (!PvzpLoadResources("LoaderBar"))
		return;

	PvzpStringListLoad("Properties/LawnStrings.txt");
	PvzpStringListReadFile("Properties/ZombatarTOS.txt");

	// Load localized properties AFTER LawnStrings so they can override string values
	LoadProperties("properties/default.xml", false, false);
	LoadProperties("properties/Layout.xml", false, false);
	LoadProperties("properties/pvz-portable.xml", false, false);

	// PvZ Co-op's own words. The game's strings come from the stock
	// main.pak, which knows nothing of them.
	mStringProperties.insert_or_assign("LUCA", "Luca");
	mStringProperties.insert_or_assign("LUCA_TOOLTIP", "Shoots cheese and makes sun");
	mStringProperties.insert_or_assign("LUCA_DESCRIPTION",
		"Luca throws cheese at zombies and makes sun while he's at it.{SHORTLINE}"
		"{KEYWORD}Damage:{STAT} heavy-ish{BR}{KEYWORD}Sun:{STAT} like a Sunflower{SHORTLINE}"
		"Nobody is quite sure where the cheese comes from.");

	if (mTitleScreen)
	{
		mTitleScreen->mLoaderScreenIsLoaded = true;
	}

	const char* groups[] = { "LoadingFonts", "LoadingImages", "LoadingSounds" };
	int group_ave_ms_to_load[] = { 54, 9, 54 };
	for (int i = 0; i < 3; i++)
	{
		mNumLoadingThreadTasks += mResourceManager->GetNumResources(groups[i]) * group_ave_ms_to_load[i];
	}
	mNumLoadingThreadTasks += 636;
	mNumLoadingThreadTasks += GetNumPreloadingTasks();
	mNumLoadingThreadTasks += Music::MUSIC_LOADING_TASKS;

	PerfTimer aTimer;
	aTimer.Start();

	PvzpHesitationBracket aHesitationResources("Resources");

	LoadGroup("LoadingImages", 9);
	LoadGroup("LoadingFonts", 54);
	if (mLoadingFailed || mShutdown || mCloseRequest)
		return;
	mDefaultFont = FONT_PICO129; // framework widgets fall back to this when no font is set

	aHesitationResources.EndBracket();
	PvzpLogLn("loading '{}' {} ms", "resources", static_cast<int>(aTimer.GetDuration()));

	mMusic->MusicInit();
	// aDuration goes unused
	//int aDuration = max(aTimer.GetDuration(), 0.0);
	aTimer.Start();

	mPoolEffect = std::make_unique<PoolEffect>();
	mPoolEffect->PoolEffectInitialize();
	mZenGarden = std::make_unique<ZenGarden>();
	mReanimatorCache = std::make_unique<ReanimatorCache>();
	mReanimatorCache->ReanimatorCacheInitialize();
	PvzpFoleyInitialize(gLawnFoleyParamArray, LENGTH(gLawnFoleyParamArray));

	PvzpLogLn("loading '{}' {} ms", "stuff", static_cast<int>(aTimer.GetDuration()));
	aTimer.Start();

	TrailLoadDefinitions(gLawnTrailArray, LENGTH(gLawnTrailArray));
	PvzpLogLn("loading '{}' {} ms", "trail", static_cast<int>(aTimer.GetDuration()));
	aTimer.Start();

	PvzpParticleLoadDefinitions(gLawnParticleArray, LENGTH(gLawnParticleArray));
	//aDuration = max(aTimer.GetDuration(), 0.0);
	aTimer.Start();

	PreloadForUser();
	if (mLoadingFailed || mShutdown || mCloseRequest)
		return;

	//aDuration = max(aTimer.GetDuration(), 0.0);
	aTimer.Start();

	GetNumPreloadingTasks();
	LoadGroup("LoadingSounds", 54);
}

void LawnApp::FastLoad(GameMode theGameMode)
{
	if (!mShutdown)
	{
		mWidgetManager->RemoveWidget(mTitleScreen.get());
		SafeDeleteWidget(mTitleScreen.release());

		PreNewGame(theGameMode, false);
	}
}

void LawnApp::LoadingThreadCompleted()
{
}

void LawnApp::LoadingCompleted()
{
	mWidgetManager->RemoveWidget(mTitleScreen.get());
	SafeDeleteWidget(mTitleScreen.release());

	mResourceManager->DeleteImage("IMAGE_TITLESCREEN");

	// A joining player who is already somewhere the host put them must not be
	// sent to the menu from under it. (The host waits for us to be ready, so
	// this should not arise; it is the backstop.)
	if (Coop::Game::Get().IsFollower() && (mBoard || mAwardScreen || mCreditScreen))
		return;

	ShowGameSelector();
}

void LawnApp::URLOpenFailed(const std::string& theURL)
{
	SexyAppBase::URLOpenFailed(theURL);
	KillDialog(Dialogs::DIALOG_OPENURL_WAIT);
	CopyToClipboard(theURL);

	std::string aString =
		GetFormattedString("OPEN_URL", "Please open the following URL in your browser\n\n%s\n\nFor your convenience, this URL has already been copied to your clipboard.", theURL.c_str());

	DoDialog(Dialogs::DIALOG_OPENURL_WAIT, true, GetString("OPEN_BROWSER", "Open Browser"), "[DIALOG_BUTTON_OK]", aString, Dialog::BUTTONS_FOOTER);
}

void LawnApp::URLOpenSucceeded(const std::string& theURL)
{
	SexyAppBase::URLOpenSucceeded(theURL);
	KillDialog(Dialogs::DIALOG_OPENURL_WAIT);
}

bool LawnApp::OpenURL(const std::string& theURL, bool shutdownOnOpen)
{
	DoDialog(
		Dialogs::DIALOG_OPENURL_WAIT,
		true,
		GetString("OPENING_BROWSER", "Opening Browser"),
		GetString("OPENING_BROWSER", "Opening Browser"),
		"",
		Dialog::BUTTONS_NONE
	);

	DrawDirtyStuff();

	return SexyAppBase::OpenURL(theURL, shutdownOnOpen);
}

void LawnApp::ConfirmQuit()
{
	std::string aBody(PvzpStringTranslate("[QUIT_MESSAGE]"));
	std::string aHeader(PvzpStringTranslate("[QUIT_HEADER]"));
	LawnDialog* aDialog = (LawnDialog*)DoDialog(Dialogs::DIALOG_QUIT, true, aHeader, aBody, "", Dialog::BUTTONS_OK_CANCEL);
	aDialog->mLawnYesButton->mLabel = PvzpStringTranslate("[QUIT_BUTTON]");
	CenterDialog(aDialog, aDialog->mWidth, aDialog->mHeight);
}

void LawnApp::PreDisplayHook()
{
	SexyApp::PreDisplayHook();
}


void LawnApp::ButtonPress(int) {}
void LawnApp::ButtonDownTick(int) {}
void LawnApp::ButtonMouseEnter(int) {}
void LawnApp::ButtonMouseLeave(int) {}
void LawnApp::ButtonMouseMove(int, int, int) {}

void LawnApp::ButtonDepress(int theId)
{
	if (theId % 10000 >= 2000 && theId % 10000 < 3000)  // ids in [2000, 3000): the "Yes" button of dialog (theId - 2000)
	{
		switch (theId - 2000)
		{
		case Dialogs::DIALOG_NEW_GAME:
			KillDialog(Dialogs::DIALOG_NEW_GAME);
			ShowGameSelector();
			return;

		case Dialogs::DIALOG_NEWOPTIONS:
			KillNewOptionsDialog();
			return;

		case Dialogs::DIALOG_PREGAME_NAG:
			DoRegister();
			return;

		case Dialogs::DIALOG_LOAD_GAME:
			return;

		case Dialogs::DIALOG_CONFIRM_UPDATE_CHECK:
			KillDialog(Dialogs::DIALOG_CONFIRM_UPDATE_CHECK);
			CheckForUpdates();
			return;

		case Dialogs::DIALOG_QUIT:
			KillDialog(Dialogs::DIALOG_QUIT);
#if defined(__ANDROID__) && !defined(__TERMUX__)
			// Android should move the task to the background instead of forcing a quit.
			SDL_MinimizeWindow(static_cast<SDL_Window*>(mWindow));
#elif !defined(__IPHONEOS__) // iOS apps must not quit or programmatically return to the Home screen.
			CloseRequestAsync();
#endif
			return;

		case Dialogs::DIALOG_NAG:
			KillDialog(Dialogs::DIALOG_NAG);
			DoRegister();
			return;

		case Dialogs::DIALOG_INFO:
			KillDialog(Dialogs::DIALOG_INFO);
			return;

		case Dialogs::DIALOG_PAUSED:
			KillDialog(Dialogs::DIALOG_PAUSED);
			return;

		case Dialogs::DIALOG_NO_MORE_MONEY:
			KillDialog(Dialogs::DIALOG_NO_MORE_MONEY);
			mBoard->AddSunMoney(100);
			return;

		case Dialogs::DIALOG_BONUS:
			KillDialog(Dialogs::DIALOG_BONUS);
			return;

		case Dialogs::DIALOG_CONFIRM_BACK_TO_MAIN:
			KillDialog(Dialogs::DIALOG_CONFIRM_BACK_TO_MAIN);
			mBoardResult = BoardResult::BOARDRESULT_QUIT;
			mBoard->TryToSaveGame();
			DoBackToMain();
			return;

		case Dialogs::DIALOG_USERDIALOG:
			FinishUserDialog(true);
			return;

		case Dialogs::DIALOG_CREATEUSER:
			FinishCreateUserDialog(true);
			return;

		case Dialogs::DIALOG_CONFIRMDELETEUSER:
			FinishConfirmDeleteUserDialog(true);
			return;

		case Dialogs::DIALOG_RENAMEUSER:
			FinishRenameUserDialog(true);
			return;

		case Dialogs::DIALOG_CREATEUSERERROR:
		case Dialogs::DIALOG_RENAMEUSERERROR:
			FinishNameError(theId - 2000);
			return;

		case Dialogs::DIALOG_CHEAT:
			FinishCheatDialog(true);
			return;

		case Dialogs::DIALOG_RESTARTCONFIRM:
			FinishRestartConfirmDialog();
			return;

		case Dialogs::DIALOG_TIMESUP:
			FinishTimesUpDialog();
			return;

		case 20008:
			KillDialog(20008);
			KillDialog(Dialogs::DIALOG_CHECKING_UPDATES);
			return;

		default:
			KillDialog(theId - 2000);
			return;
		}
	}

	if (theId % 10000 >= 3000 && theId < 4000)  // ids in [3000, 4000): the "No" button of dialog (theId - 3000)
	{
		switch (theId - 3000)
		{
		case Dialogs::DIALOG_PREGAME_NAG:
			KillDialog(Dialogs::DIALOG_PREGAME_NAG);
			Shutdown();
			return;

		case Dialogs::DIALOG_LOAD_GAME:
			KillDialog(Dialogs::DIALOG_LOAD_GAME);
			return;

		case Dialogs::DIALOG_USERDIALOG:
			FinishUserDialog(false);
			return;

		case Dialogs::DIALOG_CREATEUSER:
			FinishCreateUserDialog(false);
			return;

		case Dialogs::DIALOG_CONFIRMDELETEUSER:
			FinishConfirmDeleteUserDialog(false);
			return;

		case Dialogs::DIALOG_RENAMEUSER:
			FinishRenameUserDialog(false);
			return;

		case Dialogs::DIALOG_CHEAT:
			FinishCheatDialog(false);
			return;

		case Dialogs::DIALOG_TIMESUP:
			FinishTimesUpDialog();
			return;

		case 10008:
			KillDialog(10008);
			KillDialog(Dialogs::DIALOG_CHECKING_UPDATES);
			return;

		default:
			KillDialog(theId - 3000);
			return;
		}
	}
}

void LawnApp::CenterDialog(Dialog* theDialog, int theWidth, int theHeight)
{
	theDialog->Resize((BOARD_WIDTH - theWidth) / 2, (BOARD_HEIGHT - theHeight) / 2, theWidth, theHeight);
}

void LawnApp::PlayFoley(FoleyType theFoleyType)
{
	if (!mMuteSoundsForCutscene)
	{
		mSoundSystem->PlayFoley(theFoleyType);
	}
}

void LawnApp::PlayFoleyPitch(FoleyType theFoleyType, float thePitch)
{
	if (!mMuteSoundsForCutscene)
	{
		mSoundSystem->PlayFoleyPitch(theFoleyType, thePitch);
	}
}

std::string LawnApp::GetStageString(int theLevel)
{
	int aArea = std::clamp((theLevel - 1) / LEVELS_PER_AREA + 1, 1, ADVENTURE_AREAS + 1);
	int aSub = theLevel - (aArea - 1) * LEVELS_PER_AREA;
	return std::format("{}-{}", aArea, aSub);
}

bool LawnApp::IsAdventureMode()
{
	return mGameMode == GameMode::GAMEMODE_ADVENTURE;
}

bool LawnApp::IsSurvivalMode()
{
	return mGameMode >= GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1 && mGameMode <= GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_5;
}

bool LawnApp::IsPuzzleMode()
{
	return
		(mGameMode >= GameMode::GAMEMODE_SCARY_POTTER_1 && mGameMode <= GameMode::GAMEMODE_SCARY_POTTER_ENDLESS) ||
		(mGameMode >= GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_1 && mGameMode <= GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_ENDLESS);
}

bool LawnApp::IsCoopBonusLevel()
{
	return mGameMode == GameMode::GAMEMODE_COOP_ONSLAUGHT || mGameMode == GameMode::GAMEMODE_COOP_SANDBOX;
}

bool LawnApp::IsChallengeMode()
{
	return !IsAdventureMode() && !IsPuzzleMode() && !IsSurvivalMode();
}

bool LawnApp::IsSurvivalNormal(GameMode theGameMode)
{
	int aLevel = theGameMode - GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1;
	return aLevel >= 0 && aLevel <= 4;
}

bool LawnApp::IsSurvivalHard(GameMode theGameMode)
{
	int aLevel = theGameMode - GameMode::GAMEMODE_SURVIVAL_HARD_STAGE_1;
	return aLevel >= 0 && aLevel <= 4;
}

bool LawnApp::IsSurvivalEndless(GameMode theGameMode)
{
	int aLevel = theGameMode - GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_1;
	return aLevel >= 0 && aLevel <= 4;
}

bool LawnApp::IsEndlessScaryPotter(GameMode theGameMode)
{
	return theGameMode == GameMode::GAMEMODE_SCARY_POTTER_ENDLESS;
}

bool LawnApp::IsEndlessIZombie(GameMode theGameMode)
{
	return theGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_ENDLESS;
}

bool LawnApp::IsContinuousChallenge()
{
	return
		IsArtChallenge() ||
		IsSlotMachineLevel() ||
		IsFinalBossLevel() ||
		mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED ||
		mGameMode == GameMode::GAMEMODE_UPSELL ||
		mGameMode == GameMode::GAMEMODE_INTRO ||
		mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST;
}

bool LawnApp::IsArtChallenge()
{
	if (mBoard == nullptr)
		return false;

	return
		mGameMode == GameMode::GAMEMODE_CHALLENGE_ART_CHALLENGE_WALLNUT ||
		mGameMode == GameMode::GAMEMODE_CHALLENGE_ART_CHALLENGE_SUNFLOWER ||
		mGameMode == GameMode::GAMEMODE_CHALLENGE_SEEING_STARS;
}

bool LawnApp::IsSquirrelLevel()
{
	return mBoard && mGameMode == GameMode::GAMEMODE_CHALLENGE_SQUIRREL;
}

bool LawnApp::IsIZombieLevel()
{
	if (mBoard == nullptr)
		return false;

	return
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_1 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_2 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_3 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_4 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_5 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_6 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_7 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_8 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_9 ||
		mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_ENDLESS;
}

bool LawnApp::IsShovelLevel()
{
	return mBoard && mGameMode == GameMode::GAMEMODE_CHALLENGE_SHOVEL;
}

bool LawnApp::IsWallnutBowlingLevel()
{
	if (mBoard == nullptr)
		return false;

	if (mGameMode == GameMode::GAMEMODE_CHALLENGE_WALLNUT_BOWLING || mGameMode == GameMode::GAMEMODE_CHALLENGE_WALLNUT_BOWLING_2)
		return true;

	return IsAdventureMode() && mBoard->mLevel == 5;
}

bool LawnApp::IsSlotMachineLevel()
{
	return (mBoard && mGameMode == GameMode::GAMEMODE_CHALLENGE_SLOT_MACHINE);
}

bool LawnApp::IsWhackAZombieLevel()
{
	if (mBoard == nullptr)
		return false;

	if (mGameMode == GameMode::GAMEMODE_CHALLENGE_WHACK_A_ZOMBIE)
		return true;

	return IsAdventureMode() && mBoard->mLevel == 15;
}

bool LawnApp::IsLittleTroubleLevel()
{
	return (mBoard && (mGameMode == GameMode::GAMEMODE_CHALLENGE_LITTLE_TROUBLE || (mGameMode == GameMode::GAMEMODE_ADVENTURE && mBoard->mLevel == 25)));
}

bool LawnApp::IsScaryPotterLevel()
{
	if (mGameMode >= GameMode::GAMEMODE_SCARY_POTTER_1 && mGameMode <= GameMode::GAMEMODE_SCARY_POTTER_ENDLESS)
		return true;

	return IsAdventureMode() && mBoard && mBoard->mLevel == 35;
}

bool LawnApp::IsStormyNightLevel()
{
	if (mBoard == nullptr)
		return false;

	if (mGameMode == GameMode::GAMEMODE_CHALLENGE_STORMY_NIGHT)
		return true;

	return IsAdventureMode() && mBoard->mLevel == 40;
}

bool LawnApp::IsBungeeBlitzLevel()
{
	if (mBoard == nullptr)
		return false;

	if (mGameMode == GameMode::GAMEMODE_CHALLENGE_BUNGEE_BLITZ)
		return true;

	return IsAdventureMode() && mBoard->mLevel == 45;
}

bool LawnApp::IsMiniBossLevel()
{
	if (mBoard == nullptr)
		return false;

	return IsAdventureMode() && (mBoard->mLevel == 10 || mBoard->mLevel == 20 || mBoard->mLevel == 30);
}

bool LawnApp::IsFinalBossLevel()
{
	if (mBoard == nullptr)
		return false;

	if (mGameMode == GameMode::GAMEMODE_CHALLENGE_FINAL_BOSS)
		return true;

	return IsAdventureMode() && mBoard->mLevel == 50;
}

bool LawnApp::IsChallengeWithoutSeedBank()
{
	return
		mGameMode == GameMode::GAMEMODE_CHALLENGE_RAINING_SEEDS ||
		mGameMode == GameMode::GAMEMODE_UPSELL ||
		mGameMode == GameMode::GAMEMODE_INTRO ||
		IsWhackAZombieLevel() ||
		IsSquirrelLevel() ||
		IsScaryPotterLevel() ||
		mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN ||
		mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM;
}

bool LawnApp::IsNight()
{
	if (IsIceDemo() || mPlayerInfo == nullptr)
		return false;

	return (mPlayerInfo->mLevel >= 11 && mPlayerInfo->mLevel <= 20) || (mPlayerInfo->mLevel >= 31 && mPlayerInfo->mLevel <= 40) || mPlayerInfo->mLevel == 50;
}

int LawnApp::GetCurrentChallengeIndex()
{
	return static_cast<int>(mGameMode) - static_cast<int>(GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1);
}

const ChallengeDefinition& LawnApp::GetCurrentChallengeDef()
{
	return GetChallengeDefinition(GetCurrentChallengeIndex());
}

PottedPlant* LawnApp::GetPottedPlantByIndex(int thePottedPlantIndex)
{
	PVZP_ASSERT(thePottedPlantIndex >= 0 && thePottedPlantIndex < mPlayerInfo->mNumPottedPlants);
	return &mPlayerInfo->mPottedPlant[thePottedPlantIndex];
}

bool LawnApp::UpdateAppStep(bool* updated)
{
	if (mCloseRequest)
	{
		Shutdown();
		return false;
	}
	return SexyAppBase::UpdateAppStep(updated);
}

bool LawnApp::UpdateApp()
{
	if (mCloseRequest)
	{
		Shutdown();
		return false;
	}

	//if (mLoadingThreadCompleted)
	//{
	//	LoadingThreadCompleted();
	//}

	bool updated = SexyAppBase::UpdateApp();

	//if (mLoadingThreadCompleted && !mExitToTop)
	//{
	//	CheckForUpdates();
	//}

	return updated;
}

void LawnApp::CloseRequestAsync()
{
	mExitToTop = true;
	mCloseRequest = true;
}

SeedType LawnApp::GetAwardSeedForLevel(int theLevel)
{
	int aArea = (theLevel - 1) / LEVELS_PER_AREA + 1;
	int aSub = (theLevel - 1) % LEVELS_PER_AREA + 1;
	int aSeedsHasGot = (aArea - 1) * 8 + aSub;  // in general, each area awards 8 plants and each level awards 1
	if (aSub >= 10)
	{
		aSeedsHasGot -= 2;  // 2 levels in this area don't award a new plant
	}
	else if (aSub >= 5)
	{
		aSeedsHasGot -= 1;  // 1 level in this area doesn't award a new plant
	}
	if (aSeedsHasGot > 40)
	{
		aSeedsHasGot = 40;
	}

	return (SeedType)aSeedsHasGot;
}

int LawnApp::GetSeedsAvailable()
{
	int aLevel = mPlayerInfo->GetLevel();
	if (HasFinishedAdventure() || aLevel > 50)
	{
		return 49;
	}

	SeedType aSeedTypeMax = GetAwardSeedForLevel(aLevel);
	return std::min(NUM_SEEDS_IN_CHOOSER, aSeedTypeMax);
}

bool LawnApp::HasSeedType(SeedType theSeedType)
{
	// The Dev Sandbox is for trying things out: every plant, upgrades and
	// Luca included, whatever the profile has earned.
	if (mBoard && mGameMode == GameMode::GAMEMODE_COOP_SANDBOX &&
		theSeedType >= SeedType::SEED_PEASHOOTER && theSeedType < SeedType::NUM_SEEDS_IN_CHOOSER)
		return true;

	if (theSeedType == SeedType::SEED_LUCA)
		return mPlayerInfo != nullptr && mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_UNLOCK_LUCA] > 0;

	if (IsTrialStageLocked() && theSeedType >= SeedType::SEED_JALAPENO)
		return false;

	/*  optimization
	if (theSeedType >= SeedType::SEED_GATLINGPEA && theSeedType <= SeedType::SEED_IMITATER)
		return mPlayerInfo->mPurchases[theSeedType - SeedType::SEED_GATLINGPEA];
	*/

	switch (theSeedType)
	{
	case SeedType::SEED_GATLINGPEA:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_GATLINGPEA] > 0;
	case SeedType::SEED_TWINSUNFLOWER:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_TWINSUNFLOWER] > 0;
	case SeedType::SEED_GLOOMSHROOM:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_GLOOMSHROOM] > 0;
	case SeedType::SEED_CATTAIL:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_CATTAIL] > 0;
	case SeedType::SEED_WINTERMELON:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_WINTERMELON] > 0;
	case SeedType::SEED_GOLD_MAGNET:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_GOLD_MAGNET] > 0;
	case SeedType::SEED_SPIKEROCK:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_SPIKEROCK] > 0;
	case SeedType::SEED_COBCANNON:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_COBCANNON] > 0;
	case SeedType::SEED_IMITATER:
		return mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_PLANT_IMITATER] > 0;
	default:
		return theSeedType < GetSeedsAvailable();
	}
}

Reanimation* LawnApp::AddReanimation(float theX, float theY, int theRenderOrder, ReanimationType theReanimationType)
{
	return mEffectSystem->mReanimationHolder->AllocReanimation(theX, theY, theRenderOrder, theReanimationType);
}

PvzpParticleSystem* LawnApp::AddPvzpParticle(float theX, float theY, int theRenderOrder, ParticleEffect theEffect)
{
	return mEffectSystem->mParticleHolder->AllocParticleSystem(theX, theY, theRenderOrder, theEffect);
}

ParticleSystemID LawnApp::ParticleGetID(PvzpParticleSystem* theParticle)
{
	return (ParticleSystemID)mEffectSystem->mParticleHolder->mParticleSystems.DataArrayGetID(theParticle);
}

ReanimationID LawnApp::ReanimationGetID(Reanimation* theReanimation)
{
	return static_cast<ReanimationID>(mEffectSystem->mReanimationHolder->mReanimations.DataArrayGetID(theReanimation));
}

PvzpParticleSystem* LawnApp::ParticleGet(ParticleSystemID theParticleID)
{
	return mEffectSystem->mParticleHolder->mParticleSystems.DataArrayGet(static_cast<unsigned int>(theParticleID));
}

PvzpParticleSystem* LawnApp::ParticleTryToGet(ParticleSystemID theParticleID)
{
	return mEffectSystem->mParticleHolder->mParticleSystems.DataArrayTryToGet(static_cast<unsigned int>(theParticleID));
}

Reanimation* LawnApp::ReanimationGet(ReanimationID theReanimationID)
{
	return mEffectSystem->mReanimationHolder->mReanimations.DataArrayGet(static_cast<unsigned int>(theReanimationID));
}

Reanimation* LawnApp::ReanimationTryToGet(ReanimationID theReanimationID)
{
	return mEffectSystem->mReanimationHolder->mReanimations.DataArrayTryToGet(static_cast<unsigned int>(theReanimationID));
}

void LawnApp::RemoveReanimation(ReanimationID theReanimationID)
{
	Reanimation* aReanim = ReanimationTryToGet(theReanimationID);
	if (aReanim)
	{
		aReanim->ReanimationDie();
	}
}

void LawnApp::RemoveParticle(ParticleSystemID theParticleID)
{
	PvzpParticleSystem* aParticle = ParticleTryToGet(theParticleID);
	if (aParticle)
	{
		aParticle->ParticleSystemDie();
	}
}

bool LawnApp::AdvanceCrazyDaveText()
{
	std::string aMessageName = std::format("[CRAZY_DAVE_{}]", mCrazyDaveMessageIndex + 1);
	if (!PvzpStringListExists(aMessageName))
	{
		return false;
	}

	CrazyDaveTalkIndex(mCrazyDaveMessageIndex + 1);
	return true;
}

std::string LawnApp::GetCrazyDaveText(int theMessageIndex)
{
	std::string aMessage = std::format("[CRAZY_DAVE_{}]", theMessageIndex);
	aMessage = PvzpReplaceString(aMessage, "{PLAYER_NAME}", mPlayerInfo->mName);
	aMessage = PvzpReplaceString(aMessage, "{MONEY}", GetMoneyString(mPlayerInfo->mCoins));
	int aCost = StoreScreen::GetItemCost(StoreItem::STORE_ITEM_PACKET_UPGRADE);
	aMessage = PvzpReplaceString(aMessage, "{UPGRADE_COST}", GetMoneyString(aCost));
	return aMessage;
}

bool LawnApp::CanShowAlmanac()
{
	if (IsIceDemo())
		return false;

	if (mPlayerInfo == nullptr)
		return false;

	return HasFinishedAdventure() || mPlayerInfo->mLevel >= 15;
}

bool LawnApp::CanShowStore()
{
	if (IsIceDemo())
		return false;

	if (mPlayerInfo == nullptr)
		return false;

	return HasFinishedAdventure() || mPlayerInfo->mHasSeenUpsell || mPlayerInfo->mLevel >= 25;
}

bool LawnApp::CanShowZenGarden()
{
	if (mPlayerInfo == nullptr)
		return false;

	if (IsTrialStageLocked())
		return false;

	return HasFinishedAdventure() || mPlayerInfo->mLevel >= 45;
}

bool LawnApp::CanSpawnYetis()
{
	const ZombieDefinition& aZombieDef = GetZombieDefinition(ZombieType::ZOMBIE_YETI);
	return HasFinishedAdventure() && (mPlayerInfo->mFinishedAdventure >= 2 || mPlayerInfo->mLevel >= aZombieDef.mStartingLevel);
}

bool LawnApp::HasBeatenChallenge(GameMode theGameMode)
{
	if (mPlayerInfo == nullptr)
		return false;

	int aChallengeIndex = theGameMode - GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1;
	PVZP_ASSERT(aChallengeIndex >= 0 && aChallengeIndex < NUM_CHALLENGE_MODES);
	if (IsSurvivalNormal(theGameMode))
	{
		return mPlayerInfo->mChallengeRecords[aChallengeIndex] >= SURVIVAL_NORMAL_FLAGS;
	}
	if (IsSurvivalHard(theGameMode))
	{
		return mPlayerInfo->mChallengeRecords[aChallengeIndex] >= SURVIVAL_HARD_FLAGS;
	}
	if (IsSurvivalEndless(theGameMode) || IsEndlessScaryPotter(theGameMode) || IsEndlessIZombie(theGameMode))
	{
		return false;
	}
	return mPlayerInfo->mChallengeRecords[aChallengeIndex] > 0;
}

bool LawnApp::HasFinishedAdventure()
{
	return mPlayerInfo && mPlayerInfo->mFinishedAdventure > 0;
}

bool LawnApp::IsFirstTimeAdventureMode()
{
	return IsAdventureMode() && !HasFinishedAdventure();
}

void LawnApp::CrazyDaveEnter()
{
	PVZP_ASSERT(mCrazyDaveState == CRAZY_DAVE_OFF);
	PVZP_ASSERT(!ReanimationTryToGet(mCrazyDaveReanimID));

	Reanimation* aCrazyDaveReanim = AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_CRAZY_DAVE);
	aCrazyDaveReanim->mIsAttachment = true;
	aCrazyDaveReanim->SetBasePoseFromAnim("anim_idle_handing");
	mCrazyDaveReanimID = ReanimationGetID(aCrazyDaveReanim);
	aCrazyDaveReanim->PlayReanim("anim_enter", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 0, 24.0f);

	mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_ENTERING;
	mCrazyDaveMessageIndex = -1;
	mCrazyDaveMessageText.clear();
	mCrazyDaveBlinkCounter = RandRangeInt(400, 800);

	if (mGameScene == GameScenes::SCENE_LEVEL_INTRO && IsStormyNightLevel())
	{
		aCrazyDaveReanim->mColorOverride = Color(64, 64, 64);
	}
}

void LawnApp::CrazyDaveDie()
{
	Reanimation* aCrazyDaveReanim = ReanimationTryToGet(mCrazyDaveReanimID);
	if (aCrazyDaveReanim)
	{
		aCrazyDaveReanim->ReanimationDie();
	}

	mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_OFF;
	mCrazyDaveReanimID = ReanimationID::REANIMATIONID_NULL;
	mCrazyDaveBlinkReanimID = ReanimationID::REANIMATIONID_NULL;
	mCrazyDaveMessageIndex = -1;
	mCrazyDaveMessageText.clear();

	CrazyDaveStopSound();
}

void LawnApp::CrazyDaveLeave()
{
	Reanimation* aCrazyDaveReanim = ReanimationTryToGet(mCrazyDaveReanimID);
	if (aCrazyDaveReanim)
	{
		if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_TALKING || mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_IDLING)
		{
			CrazyDaveDoneHanding();
		}

		aCrazyDaveReanim->PlayReanim("anim_leave", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 20, 24.0f);
		aCrazyDaveReanim->SetImageOverride("Dave_mouths", nullptr);

		mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_LEAVING;
		mCrazyDaveMessageIndex = -1;
		mCrazyDaveMessageText.clear();

		CrazyDaveStopSound();
	}
}

void LawnApp::CrazyDaveTalkIndex(int theMessageIndex)
{
	static const bool sTrace = getenv("PVZ_COOP_TRACE") != nullptr;
	if (sTrace)
	{
		printf("trace: dave line %d (tick %u)\n", theMessageIndex, Coop::Game::Get().GetLockstep().GetCurrentTick());
		fflush(stdout);
	}
	mCrazyDaveMessageIndex = theMessageIndex;
	std::string aMessageText = GetCrazyDaveText(theMessageIndex);
	CrazyDaveTalkMessage(aMessageText);
}

void LawnApp::CrazyDaveDoneHanding()
{
	Reanimation* aCrazyDaveReanim = ReanimationGet(mCrazyDaveReanimID);
	ReanimatorTrackInstance* aHandTrackInstance = aCrazyDaveReanim->GetTrackInstanceByName("Dave_handinghand");
	AttachmentDie(aHandTrackInstance->mAttachmentID);

	PvzpLogLn("DoneHanding");
}

void LawnApp::CrazyDaveStopSound()
{
	mSoundSystem->StopFoley(FoleyType::FOLEY_CRAZY_DAVE_SHORT);
	mSoundSystem->StopFoley(FoleyType::FOLEY_CRAZY_DAVE_LONG);
	mSoundSystem->StopFoley(FoleyType::FOLEY_CRAZY_DAVE_EXTRA_LONG);
	mSoundSystem->StopFoley(FoleyType::FOLEY_CRAZY_DAVE_CRAZY);
}

void LawnApp::CrazyDaveTalkMessage(const std::string& theMessage)
{
	Reanimation* aCrazyDaveReanim = ReanimationGet(mCrazyDaveReanimID);

	bool doHanding = false;
	if (theMessage.find("{HANDING}") != std::string::npos)
	{
		doHanding = true;
	}
	if ((mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_TALKING || mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_IDLING) && !doHanding)
	{
		CrazyDaveDoneHanding();
	}

	bool doSound = true;
	if (theMessage.find("{NO_SOUND}") != std::string::npos)
	{
		doSound = false;
	}
	else
	{
		CrazyDaveStopSound();
	}

	int aWordsCount = 0;
	bool isControlWord = false;
	for (size_t i = 0; i < theMessage.size(); i++)  // byte count tracks CJK talk duration better than code points
	{
		if (theMessage[i] == '{')
		{
			isControlWord = true;
		}
		else if (theMessage[i] == '}')
		{
			isControlWord = false;
		}
		else if (!isControlWord)
		{
			aWordsCount++;
		}
	}

	aCrazyDaveReanim->SetImageOverride("Dave_mouths", nullptr);

	if (mCrazyDaveState != CrazyDaveState::CRAZY_DAVE_TALKING || doSound)
	{
		if (doHanding)
		{
			aCrazyDaveReanim->PlayReanim("anim_talk_handing", ReanimLoopType::REANIM_LOOP, 50, 12.0f);

			if (doSound)
			{
				if (theMessage.find("{SHORT_SOUND}") != std::string::npos)
				{
					PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_SHORT);
				}
				else if (theMessage.find("{SCREAM}") != std::string::npos)
				{
					PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_SCREAM);
				}
				else
				{
					PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_LONG);
				}
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_TALKING;
		}
		else if (theMessage.find("{SHAKE}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_crazy", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 50, 12.0f);

			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_CRAZY);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_TALKING;
		}
		else if (theMessage.find("{SCREAM}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_smalltalk", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 50, 12.0f);

			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_SCREAM);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_TALKING;
		}
		else if (theMessage.find("{SCREAM2}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_mediumtalk", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 50, 12.0f);

			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_SCREAM_2);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_TALKING;
		}
		else if (theMessage.find("{SHOW_WALLNUT}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_talk_handing", ReanimLoopType::REANIM_LOOP, 50, 12.0f);

			Reanimation* aWallnutReanim = AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_WALLNUT);
			aWallnutReanim->PlayReanim("anim_idle", ReanimLoopType::REANIM_LOOP, 0, 12.0f);
			PvzpLogLn("Handed");

			ReanimatorTrackInstance* aHandTrackInstance = aCrazyDaveReanim->GetTrackInstanceByName("Dave_handinghand");
			AttachEffect* aAttachEffect = AttachReanim(aHandTrackInstance->mAttachmentID, aWallnutReanim, 100.0f, 393.0f);
			aAttachEffect->mOffset.m00 = 1.2f;
			aAttachEffect->mOffset.m11 = 1.2f;

			aCrazyDaveReanim->Update();

			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_SCREAM_2);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_TALKING;
		}
		else if (theMessage.find("{SHOW_HAMMER}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_talk_handing", ReanimLoopType::REANIM_LOOP, 50, 12.0f);

			Reanimation* aHammerReanim = AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_HAMMER);
			aHammerReanim->PlayReanim("anim_whack_zombie", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 0, 24.0f);
			aHammerReanim->mAnimTime = 1.0f;

			ReanimatorTrackInstance* aHandTrackInstance = aCrazyDaveReanim->GetTrackInstanceByName("Dave_handinghand");
			AttachEffect* aAttachEffect = AttachReanim(aHandTrackInstance->mAttachmentID, aHammerReanim, 62.0f, 445.0f);
			aAttachEffect->mOffset.m00 = 1.5f;
			aAttachEffect->mOffset.m11 = 1.5f;

			aCrazyDaveReanim->Update();

			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_LONG);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_TALKING;
		}
		else if (theMessage.find("{SHOW_FERTILIZER}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_talk_handing", ReanimLoopType::REANIM_LOOP, 50, 12.0f);

			Reanimation* aFertilizerReanim = AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_ZENGARDEN_FERTILIZER);
			aFertilizerReanim->PlayReanim("bag", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 0, 24.0f);
			aFertilizerReanim->mAnimRate = 0.0f;

			ReanimatorTrackInstance* aHandTrackInstance = aCrazyDaveReanim->GetTrackInstanceByName("Dave_handinghand");
			AttachReanim(aHandTrackInstance->mAttachmentID, aFertilizerReanim, 102.0f, 412.0f);
			aCrazyDaveReanim->Update();

			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_LONG);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_TALKING;
		}
		else if (theMessage.find("{SHOW_TREE_FOOD}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_talk_handing", ReanimLoopType::REANIM_LOOP, 50, 12.0f);

			Reanimation* aTreeFoodReanim = AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_TREEOFWISDOM_TREEFOOD);
			aTreeFoodReanim->PlayReanim("bag", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 0, 24.0f);
			aTreeFoodReanim->mAnimRate = 0.0f;

			ReanimatorTrackInstance* aHandTrackInstance = aCrazyDaveReanim->GetTrackInstanceByName("Dave_handinghand");
			AttachReanim(aHandTrackInstance->mAttachmentID, aTreeFoodReanim, 102.0f, 412.0f);
			aCrazyDaveReanim->Update();

			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_LONG);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_TALKING;
		}
		else if (theMessage.find("{SHOW_MONEYBAG}") != std::string::npos)
		{
			aCrazyDaveReanim->PlayReanim("anim_talk_handing", ReanimLoopType::REANIM_LOOP, 50, 12.0f);

			Reanimation* aMoneyBagReanim = AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_ZENGARDEN_FERTILIZER);
			aMoneyBagReanim->PlayReanim("bag", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 0, 24.0f);
			aMoneyBagReanim->mAnimRate = 0.0f;
			aMoneyBagReanim->SetImageOverride("bag", IMAGE_MONEYBAG);

			ReanimatorTrackInstance* aHandTrackInstance = aCrazyDaveReanim->GetTrackInstanceByName("Dave_handinghand");
			AttachReanim(aHandTrackInstance->mAttachmentID, aMoneyBagReanim, 90.0f, 405.0f);
			aCrazyDaveReanim->Update();
			if (doSound)
			{
				PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_LONG);
			}

			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_TALKING;
		}
		else
		{
			if (aWordsCount < 23)
			{
				aCrazyDaveReanim->PlayReanim("anim_smalltalk", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 50, 12.0f);

				if (doSound)
				{
					PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_SHORT);
				}

				mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_TALKING;
			}
			else if (aWordsCount < 52)
			{
				aCrazyDaveReanim->PlayReanim("anim_mediumtalk", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 50, 12.0f);

				if (doSound)
				{
					PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_LONG);
				}

				mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_TALKING;
			}
			else
			{
				aCrazyDaveReanim->PlayReanim("anim_blahblah", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 50, 12.0f);

				if (doSound)
				{
					PlayFoley(FoleyType::FOLEY_CRAZY_DAVE_EXTRA_LONG);
				}

				mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_TALKING;
			}
		}
	}

	mCrazyDaveMessageText = theMessage;
}

void LawnApp::CrazyDaveStopTalking()
{
	bool aDoneHanding = true;
	if (mGameMode == GameMode::GAMEMODE_UPSELL)
	{
		aDoneHanding = false;
	}
	if (aDoneHanding && mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_TALKING)
	{
		CrazyDaveDoneHanding();
	}

	Reanimation* aCrazyDaveReanim = ReanimationGet(mCrazyDaveReanimID);
	aCrazyDaveReanim->SetImageOverride("Dave_mouths", nullptr);
	if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_TALKING && !aDoneHanding)
	{
		aCrazyDaveReanim->PlayReanim("anim_idle_handing", ReanimLoopType::REANIM_LOOP, 20, 12.0f);
		mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_IDLING;
	}
	else if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_TALKING || mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_TALKING)
	{
		aCrazyDaveReanim->PlayReanim("anim_idle", ReanimLoopType::REANIM_LOOP, 20, 12.0f);
		mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_IDLING;
	}

	mCrazyDaveMessageIndex = -1;
	mCrazyDaveMessageText.clear();
	CrazyDaveStopSound();
}

void LawnApp::UpdateCrazyDave()
{
	Reanimation* aCrazyDaveReanim = ReanimationTryToGet(mCrazyDaveReanimID);
	if (aCrazyDaveReanim == nullptr)
		return;

	if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_ENTERING || mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_TALKING)
	{
		if (aCrazyDaveReanim->mLoopCount > 0)
		{
			aCrazyDaveReanim->PlayReanim("anim_idle", ReanimLoopType::REANIM_LOOP, 20, 12.0f);
			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_IDLING;
		}
	}
	else if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_TALKING)
	{
		if (aCrazyDaveReanim->mLoopCount > 0)
		{
			aCrazyDaveReanim->PlayReanim("anim_idle_handing", ReanimLoopType::REANIM_LOOP, 20, 12.0f);
			mCrazyDaveState = CrazyDaveState::CRAZY_DAVE_HANDING_IDLING;
		}
	}
	else if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_LEAVING && aCrazyDaveReanim->mLoopCount > 0)
	{
		CrazyDaveDie();
	}

	if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_IDLING || mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_IDLING)
	{
		if (mCrazyDaveMessageText.find("{MOUTH_BIG_SMILE}") != std::string::npos)
		{
			aCrazyDaveReanim->SetImageOverride("Dave_mouths", IMAGE_REANIM_CRAZYDAVE_MOUTH1);
		}
		else if (mCrazyDaveMessageText.find("{MOUTH_SMALL_SMILE}") != std::string::npos)
		{
			aCrazyDaveReanim->SetImageOverride("Dave_mouths", IMAGE_REANIM_CRAZYDAVE_MOUTH5);
		}
		else if (mCrazyDaveMessageText.find("{MOUTH_BIG_OH}") != std::string::npos)
		{
			aCrazyDaveReanim->SetImageOverride("Dave_mouths", IMAGE_REANIM_CRAZYDAVE_MOUTH4);
		}
		else if (mCrazyDaveMessageText.find("{MOUTH_SMALL_OH}") != std::string::npos)
		{
			aCrazyDaveReanim->SetImageOverride("Dave_mouths", IMAGE_REANIM_CRAZYDAVE_MOUTH6);
		}
	}

	if (mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_IDLING || mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_TALKING ||
		mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_TALKING || mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_HANDING_IDLING)
	{
		mCrazyDaveBlinkCounter--;
		if (mCrazyDaveBlinkCounter <= 0)
		{
			mCrazyDaveBlinkCounter = RandRangeInt(400, 800);
			Reanimation* aBlinkReanim = AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_CRAZY_DAVE);
			aBlinkReanim->SetFramesForLayer("anim_blink");
			aBlinkReanim->mLoopType = ReanimLoopType::REANIM_PLAY_ONCE_FULL_LAST_FRAME_AND_HOLD;
			aBlinkReanim->mAnimRate = 15.0f;
			aBlinkReanim->AttachToAnotherReanimation(aCrazyDaveReanim, "Dave_head");
			aBlinkReanim->mColorOverride = aCrazyDaveReanim->mColorOverride;
			aCrazyDaveReanim->AssignRenderGroupToTrack("Dave_eye", RENDER_GROUP_HIDDEN);
			mCrazyDaveBlinkReanimID = ReanimationGetID(aBlinkReanim);
		}
	}

	Reanimation* aBlinkReanim = ReanimationTryToGet(mCrazyDaveBlinkReanimID);
	if (aBlinkReanim && aBlinkReanim->mLoopCount > 0)
	{
		aCrazyDaveReanim->AssignRenderGroupToTrack("Dave_eye", RENDER_GROUP_NORMAL);
		RemoveReanimation(mCrazyDaveBlinkReanimID);
		mCrazyDaveBlinkReanimID = ReanimationID::REANIMATIONID_NULL;
	}

	aCrazyDaveReanim->Update();
}

void LawnApp::DrawCrazyDave(Graphics* g)
{
	Reanimation* aCrazyDaveReanim = ReanimationTryToGet(mCrazyDaveReanimID);
	if (aCrazyDaveReanim == nullptr)
		return;

	if (mCrazyDaveMessageText.size())
	{
		Image* aBubbleImage = IMAGE_STORE_SPEECHBUBBLE2;
		int aPosX = 285;
		int aPosY = 20;
		if (GetDialog(Dialogs::DIALOG_STORE))
		{
			aBubbleImage = IMAGE_STORE_SPEECHBUBBLE;
			aPosX -= 180;
			aPosY -= 78;
		}
		else if (mGameMode == GameMode::GAMEMODE_UPSELL)
		{
			aPosX += 130;
			aPosY += 70;
		}
		g->DrawImage(aBubbleImage, aPosX, aPosY);

		std::string aBubbleText = mCrazyDaveMessageText;
		Rect aRect(aPosX + 25, aPosY + 6, 233, 144);
		if (aBubbleText.find("{SHAKE}") != std::string::npos)
		{
			aBubbleText = PvzpReplaceString(aBubbleText, "{SHAKE}", "");
			aRect.mX += rand() % 2;
			aRect.mY += rand() % 2;
		}

		bool clickToContinue = true;
		if (mGameMode == GameMode::GAMEMODE_UPSELL)
		{
			clickToContinue = false;
		}
		else if (aBubbleText.find("{NO_CLICK}") != std::string::npos)
		{
			aBubbleText = PvzpReplaceString(aBubbleText, "{NO_CLICK}", "");
			clickToContinue = false;
		}

		auto aWrapEnum = static_cast<DrawStringJustification>(GetInteger("CRAZY_DAVE_MESSAGE_TEXT_WRAP_ENUM", DS_ALIGN_CENTER_VERTICAL_MIDDLE));
		PvzpDrawStringWrapped(g, aBubbleText, aRect, FONT_BRIANNETOD16, Color::Black, aWrapEnum);
		if (clickToContinue)
		{
			PvzpDrawString(g, GetString("CLICK_TO_CONTINUE", "click to continue"), aPosX + 139, aPosY + 140, FONT_PICO129, Color::Black, DrawStringJustification::DS_ALIGN_CENTER);
		}
	}

	aCrazyDaveReanim->Draw(g);
}

int LawnApp::GetNumPreloadingTasks()
{
	int aTaskCount = 10;
	if (mPlayerInfo)
	{
		for (SeedType i = SeedType::SEED_PEASHOOTER; i < SeedType::NUM_SEED_TYPES; i = static_cast<SeedType>(static_cast<int>(i) + 1))
		{
			if (HasSeedType(i) || HasFinishedAdventure())
			{
				aTaskCount++;
			}
		}

		for (ZombieType i = ZombieType::ZOMBIE_NORMAL; i < ZombieType::NUM_ZOMBIE_TYPES; i = static_cast<ZombieType>(static_cast<int>(i) + 1))
		{
			if (HasFinishedAdventure() || mPlayerInfo->mLevel >= GetZombieDefinition(i).mStartingLevel)
			{
				if (i != ZombieType::ZOMBIE_BOSS &&
					i != ZombieType::ZOMBIE_CATAPULT &&
					i != ZombieType::ZOMBIE_GARGANTUAR &&
					i != ZombieType::ZOMBIE_DIGGER &&
					i != ZombieType::ZOMBIE_ZAMBONI)
				{
					aTaskCount++;
				}
			}
		}
	}
	return aTaskCount * 68;
}

void LawnApp::PreloadForUser()
{
	int aNumTasks = mCompletedLoadingThreadTasks + GetNumPreloadingTasks();
	if (mTitleScreen && mTitleScreen->mQuickLoadKey != KeyCode::KEYCODE_UNKNOWN)
	{
		PvzpLogLn("preload canceled");
		mNumLoadingThreadTasks = aNumTasks;
		return;
	}

	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_PUFF, true);
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_LAWN_MOWERED_ZOMBIE, true);
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_READYSETPLANT, true);
	mCompletedLoadingThreadTasks += 68;
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_FINAL_WAVE, true);
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_SUN, true);
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_TEXT_FADE_ON, true);
	mCompletedLoadingThreadTasks += 68;
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_ZOMBIE, true);
	mCompletedLoadingThreadTasks += 68;
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_ZOMBIE_NEWSPAPER, true);
	mCompletedLoadingThreadTasks += 68;
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_SELECTOR_SCREEN, true);
	mCompletedLoadingThreadTasks += 340;
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_ZOMBIE_HAND, true);
	mCompletedLoadingThreadTasks += 68;

	if (mPlayerInfo)
	{
		for (SeedType i = SeedType::SEED_PEASHOOTER; i < SeedType::NUM_SEED_TYPES; i = static_cast<SeedType>(static_cast<int>(i) + 1))
		{
			if (HasSeedType(i) || HasFinishedAdventure())
			{
				Plant::PreloadPlantResources(i);
				if (mCompletedLoadingThreadTasks < aNumTasks)
				{
					mCompletedLoadingThreadTasks += 68;
				}

				if (mTitleScreen && mTitleScreen->mQuickLoadKey != KeyCode::KEYCODE_UNKNOWN)
				{
					PvzpLogLn("preload canceled");
					mNumLoadingThreadTasks = aNumTasks;
					return;
				}

				if (mShutdown || mCloseRequest)
				{
					return;
				}
			}
		}

		for (ZombieType i = ZombieType::ZOMBIE_NORMAL; i < ZombieType::NUM_ZOMBIE_TYPES; i = static_cast<ZombieType>(static_cast<int>(i) + 1))
		{
			if (!HasFinishedAdventure() && mPlayerInfo->mLevel < GetZombieDefinition(i).mStartingLevel)
			{
				continue;
			}
			if (i == ZombieType::ZOMBIE_BOSS || i == ZombieType::ZOMBIE_CATAPULT || i == ZombieType::ZOMBIE_GARGANTUAR ||
				i == ZombieType::ZOMBIE_DIGGER || i == ZombieType::ZOMBIE_ZAMBONI)
			{
				continue;
			}

			Zombie::PreloadZombieResources(i);
			if (mCompletedLoadingThreadTasks < aNumTasks)
			{
				mCompletedLoadingThreadTasks += 68;
			}

			if (mTitleScreen && mTitleScreen->mQuickLoadKey != KeyCode::KEYCODE_UNKNOWN)
			{
				PvzpLogLn("preload canceled");
				mNumLoadingThreadTasks = aNumTasks;
				return;
			}

			if (mShutdown || mCloseRequest)
			{
				return;
			}
		}
	}

	if (mCompletedLoadingThreadTasks != aNumTasks)
	{
		PvzpLogLn("num preload tasks wasn't calculated correctly");
		mCompletedLoadingThreadTasks = aNumTasks;
	}
}

std::string LawnApp::Pluralize(int theCount, const char* theSingular, const char* thePlural)
{
	if (theCount == 1)
	{
		return PvzpReplaceNumberString(theSingular, "{COUNT}", theCount);
	}

	return PvzpReplaceNumberString(thePlural, "{COUNT}", theCount);
}

int LawnApp::GetNumTrophies(ChallengePage thePage)
{
	int aNumTrophies = 0;

	for (int i = 0; i < NUM_CHALLENGE_MODES; i++)
	{
		const ChallengeDefinition& aDef = GetChallengeDefinition(i);
		if (aDef.mPage == thePage && HasBeatenChallenge(aDef.mChallengeMode))
		{
			aNumTrophies++;
		}
	}

	return aNumTrophies;
}

int LawnApp::TrophiesNeedForGoldSunflower()
{
	return 48 - GetNumTrophies(CHALLENGE_PAGE_SURVIVAL) - GetNumTrophies(CHALLENGE_PAGE_CHALLENGE) - GetNumTrophies(CHALLENGE_PAGE_PUZZLE);
}

bool LawnApp::EarnedGoldTrophy()
{
	return HasFinishedAdventure() && TrophiesNeedForGoldSunflower() <= 0;
}

void LawnApp::FinishZenGardenToturial()
{
	mBoardResult = BoardResult::BOARDRESULT_WON;
	KillBoard();
	PreNewGame(GameMode::GAMEMODE_ADVENTURE, false);
}

bool LawnApp::IsTrialStageLocked()
{
	if (mDebugTrialLocked)
		return true;

	return mTrialType == TrialType::TRIALTYPE_STAGELOCKED;
}

void LawnApp::InitHook()
{
	mTrialType = TrialType::TRIALTYPE_NONE;
}

std::string LawnApp::GetMoneyString(int theAmount)
{
	int aValue = theAmount * 10;
	if (aValue > 999999)
	{
		return std::format("${},{:03d},{:03d}", aValue / 1000000, (aValue - aValue / 1000000 * 1000000) / 1000, aValue - aValue / 1000 * 1000);
	}
	else if (aValue > 9999)
	{
		return std::format("${},{:03d}", aValue / 1000, aValue - aValue / 1000 * 1000);
	}
	else
	{
		return std::format("${}", aValue);
	}
}

std::string LawnGetCurrentLevelName()
{
	if (gLawnApp == nullptr)
	{
		return "Before App";
	}
	if (gLawnApp->mGameScene == GameScenes::SCENE_LOADING)
	{
		return "Game Loading";
	}
	if (gLawnApp->mGameScene == GameScenes::SCENE_MENU)
	{
		return "Game Selector";
	}
	if (gLawnApp->mGameScene == GameScenes::SCENE_AWARD)
	{
		return "Award Screen";
	}
	if (gLawnApp->mGameScene == GameScenes::SCENE_CHALLENGE)
	{
		return "Challenge Screen";
	}
	if (gLawnApp->mGameScene == GameScenes::SCENE_CREDIT)
	{
		return "Credits";
	}
	if (gLawnApp->mBoard == nullptr)
	{
		return "Not Playing";
	}

	if (gLawnApp->IsFirstTimeAdventureMode())
	{
		return gLawnApp->GetStageString(gLawnApp->mBoard->mLevel);
	}
	if (gLawnApp->IsAdventureMode())
	{
		return std::format("F{}", gLawnApp->GetStageString(gLawnApp->mBoard->mLevel));
	}

	return gLawnApp->GetCurrentChallengeDef().mChallengeName;
}

bool LawnApp::CanDoPinataMode()
{
	if (mPlayerInfo == nullptr)
		return false;

	return mPlayerInfo->mChallengeRecords[static_cast<int>(GameMode::GAMEMODE_TREE_OF_WISDOM) - static_cast<int>(GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1)] >= 1000;
}

bool LawnApp::CanDoDanceMode()
{
	if (mPlayerInfo == nullptr)
		return false;

	return mPlayerInfo->mChallengeRecords[static_cast<int>(GameMode::GAMEMODE_TREE_OF_WISDOM) - static_cast<int>(GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1)] >= 500;
}

bool LawnApp::CanDoDaisyMode()
{
	if (mPlayerInfo == nullptr)
		return false;

	return mPlayerInfo->mChallengeRecords[static_cast<int>(GameMode::GAMEMODE_TREE_OF_WISDOM) - static_cast<int>(GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1)] >= 100;
}

void LawnApp::PlaySample(intptr_t theSoundNum)
{
	if (!mMuteSoundsForCutscene)
	{
		SexyAppBase::PlaySample(theSoundNum);
	}
}

void LawnApp::SwitchScreenMode(bool wantWindowed, bool is3d, bool force)
{
	SexyAppBase::SwitchScreenMode(wantWindowed, is3d, force);

	NewOptionsDialog* aNewOptionsDialog = (NewOptionsDialog*)GetDialog(Dialogs::DIALOG_NEWOPTIONS);
	if (aNewOptionsDialog)
	{
		aNewOptionsDialog->mFullscreenCheckbox->SetChecked(!mIsWindowed);
	}
}

void LawnApp::DoHighScoreDialog()
{

}

void LawnApp::DoRegister()
{

}

void LawnApp::DoRegisterError()
{

}

bool LawnApp::CanDoRegisterDialog()
{
	return false;
}

void LawnApp::DoNeedRegisterDialog()
{

}

void LawnApp::FinishModelessDialogs()
{
	// Kill dialogs bound to the board; a killed dialog counts as cancelled and deletion is deferred
	KillDialog(Dialogs::DIALOG_CONFIRM_RESTART);
	KillDialog(Dialogs::DIALOG_CONFIRM_BACK_TO_MAIN);
	KillDialog(Dialogs::DIALOG_PAUSED);
	KillDialog(Dialogs::DIALOG_ALMANAC);  // may be mid-WaitForResult with the options dialog parked under it
	KillNewOptionsDialog();
}

bool LawnApp::NeedRegister()
{
	return false;
}

void LawnApp::UpdateRegisterInfo()
{

}
