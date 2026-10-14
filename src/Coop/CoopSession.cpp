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

#include "CoopSession.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace Coop
{

// ---------------------------------------------------------------------------
// Content fingerprints
// ---------------------------------------------------------------------------

namespace
{
	constexpr uint64_t FNV_OFFSET = 1469598103934665603ull;
	constexpr uint64_t FNV_PRIME = 1099511628211ull;

	uint64_t HashFile(const std::string& thePath)
	{
		FILE* aFile = fopen(thePath.c_str(), "rb");
		if (aFile == nullptr)
			return 0;  // absent on both sides hashes equal, which is what we want

		uint64_t aHash = FNV_OFFSET;
		char aBuffer[64 * 1024];
		size_t aRead = 0;
		while ((aRead = fread(aBuffer, 1, sizeof(aBuffer), aFile)) > 0)
		{
			for (size_t i = 0; i < aRead; ++i)
			{
				aHash ^= static_cast<uint8_t>(aBuffer[i]);
				aHash *= FNV_PRIME;
			}
		}
		fclose(aFile);
		return aHash;
	}

	std::string GetOwnExecutablePath()
	{
#ifdef _WIN32
		char aPath[MAX_PATH] = {};
		if (GetModuleFileNameA(nullptr, aPath, MAX_PATH) == 0)
			return {};
		return aPath;
#elif defined(__APPLE__)
		char aPath[4096] = {};
		uint32_t aSize = sizeof(aPath);
		if (_NSGetExecutablePath(aPath, &aSize) != 0)
			return {};
		return aPath;
#else
		char aPath[4096] = {};
		const ssize_t aLen = readlink("/proc/self/exe", aPath, sizeof(aPath) - 1);
		if (aLen <= 0)
			return {};
		aPath[aLen] = '\0';
		return aPath;
#endif
	}

	// Little-endian length prefix, written by hand so the framing does not
	// depend on host byte order even though both peers run the same build.
	void WriteLengthPrefix(std::vector<char>& theOut, uint32_t theLen)
	{
		theOut.push_back(static_cast<char>(theLen & 0xFF));
		theOut.push_back(static_cast<char>((theLen >> 8) & 0xFF));
		theOut.push_back(static_cast<char>((theLen >> 16) & 0xFF));
		theOut.push_back(static_cast<char>((theLen >> 24) & 0xFF));
	}

	uint32_t ReadLengthPrefix(const char* theData)
	{
		return static_cast<uint32_t>(static_cast<uint8_t>(theData[0]))
			| (static_cast<uint32_t>(static_cast<uint8_t>(theData[1])) << 8)
			| (static_cast<uint32_t>(static_cast<uint8_t>(theData[2])) << 16)
			| (static_cast<uint32_t>(static_cast<uint8_t>(theData[3])) << 24);
	}

	// Minimal LE cursor over a frame payload. Deliberately not DataReader:
	// this must never throw into the game loop, and bounds failures here mean
	// "drop the peer", not "unwind".
	struct Cursor
	{
		const char* mData;
		uint32_t    mLen;
		uint32_t    mPos = 0;
		bool        mOk = true;

		Cursor(const char* theData, uint32_t theLen) : mData(theData), mLen(theLen) {}

		uint8_t U8()
		{
			if (mPos + 1 > mLen) { mOk = false; return 0; }
			return static_cast<uint8_t>(mData[mPos++]);
		}
		uint32_t U32()
		{
			if (mPos + 4 > mLen) { mOk = false; return 0; }
			const uint32_t aValue = ReadLengthPrefix(mData + mPos);
			mPos += 4;
			return aValue;
		}
		uint64_t U64()
		{
			const uint64_t aLow = U32();
			const uint64_t aHigh = U32();
			return aLow | (aHigh << 32);
		}
		int16_t I16()
		{
			if (mPos + 2 > mLen) { mOk = false; return 0; }
			const uint16_t aValue = static_cast<uint16_t>(
				static_cast<uint8_t>(mData[mPos])
				| (static_cast<uint16_t>(static_cast<uint8_t>(mData[mPos + 1])) << 8));
			mPos += 2;
			return static_cast<int16_t>(aValue);
		}
	};

	struct Packer
	{
		std::vector<char> mData;

		void U8(uint8_t v) { mData.push_back(static_cast<char>(v)); }
		void U32(uint32_t v) { WriteLengthPrefix(mData, v); }
		void U64(uint64_t v) { U32(static_cast<uint32_t>(v & 0xFFFFFFFFu)); U32(static_cast<uint32_t>(v >> 32)); }
		void I16(int16_t v)
		{
			const uint16_t aU = static_cast<uint16_t>(v);
			mData.push_back(static_cast<char>(aU & 0xFF));
			mData.push_back(static_cast<char>((aU >> 8) & 0xFF));
		}
	};
}

uint64_t HashOwnExecutable()
{
	const std::string aPath = GetOwnExecutablePath();
	if (aPath.empty())
		return 0;
	return HashFile(aPath);
}

uint64_t HashGameAssets()
{
	// main.pak carries the art, sounds and level data. Different paks mean
	// different gameplay constants, which means a desync.
	return HashFile("main.pak");
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

Session::Session()
{
	Socket::Startup();
}

Session::~Session()
{
	Socket::Cleanup();
}

void Session::SetFailed(const std::string& theReason)
{
	mState = State::Failed;
	mStatusText = theReason;
	mPeer.Close();
	mListener.Close();
}

bool Session::StartHost(uint16_t thePort, Difficulty theDifficulty, uint32_t theStartLevel)
{
	mIsHost = true;
	mSentHello = false;
	mInbound.clear();
	mOutbound.clear();
	mPendingCommands.clear();

	mLocalBuildHash = HashOwnExecutable();
	mLocalAssetHash = HashGameAssets();

	mParams = SessionParams{};
	mParams.mDifficulty = theDifficulty;
	mParams.mStartLevel = theStartLevel;
	// The host picks the seed that both simulations run from.
	mParams.mRngSeed = static_cast<uint32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);

	if (!mListener.Listen(thePort))
	{
		SetFailed("Could not host on port " + std::to_string(thePort) + ": " + mListener.GetError());
		return false;
	}

	mState = State::Hosting;
	mStatusText = "Waiting for a player to join on port " + std::to_string(thePort) + "...";
	return true;
}

bool Session::StartJoin(const std::string& theHost, uint16_t thePort)
{
	mIsHost = false;
	mSentHello = false;
	mInbound.clear();
	mOutbound.clear();
	mPendingCommands.clear();

	mLocalBuildHash = HashOwnExecutable();
	mLocalAssetHash = HashGameAssets();

	if (!mPeer.Connect(theHost, thePort))
	{
		SetFailed("Could not connect to " + theHost + ": " + mPeer.GetError());
		return false;
	}

	mState = State::Connecting;
	mStatusText = "Connecting to " + theHost + "...";
	return true;
}

void Session::Disconnect(const std::string& theReason)
{
	if (mPeer.IsConnected())
	{
		Packer aPacker;
		aPacker.U8(static_cast<uint8_t>(MsgType::Bye));
		SendFrame(aPacker.mData.data(), static_cast<uint32_t>(aPacker.mData.size()));
		FlushOutbound();
	}
	mPeer.Close();
	mListener.Close();
	mState = State::Disconnected;
	mStatusText = theReason;
}

bool Session::SendFrame(const void* theData, uint32_t theLen)
{
	if (theLen > MAX_FRAME_BYTES)
		return false;
	WriteLengthPrefix(mOutbound, theLen);
	mOutbound.insert(mOutbound.end(), static_cast<const char*>(theData),
		static_cast<const char*>(theData) + theLen);
	return FlushOutbound();
}

bool Session::FlushOutbound()
{
	while (!mOutbound.empty())
	{
		size_t aSent = 0;
		if (!mPeer.Send(mOutbound.data(), mOutbound.size(), aSent))
			return false;
		if (aSent == 0)
			break;  // socket buffer full; the rest goes next tick
		mOutbound.erase(mOutbound.begin(), mOutbound.begin() + static_cast<long>(aSent));
	}
	return true;
}

bool Session::PumpInbound()
{
	char aBuffer[8192];
	for (;;)
	{
		size_t aRead = 0;
		if (!mPeer.Receive(aBuffer, sizeof(aBuffer), aRead))
			return false;
		if (aRead == 0)
			return true;  // drained
		mInbound.insert(mInbound.end(), aBuffer, aBuffer + aRead);
		if (mInbound.size() > MAX_FRAME_BYTES * 4)
			return false;  // peer is flooding us
	}
}

bool Session::ProcessFrames()
{
	for (;;)
	{
		if (mInbound.size() < 4)
			return true;

		const uint32_t aLen = ReadLengthPrefix(mInbound.data());
		if (aLen > MAX_FRAME_BYTES)
			return false;  // garbage or hostile
		if (mInbound.size() < 4 + static_cast<size_t>(aLen))
			return true;   // frame still arriving

		const bool aOk = HandleFrame(mInbound.data() + 4, aLen);
		mInbound.erase(mInbound.begin(), mInbound.begin() + 4 + static_cast<long>(aLen));
		if (!aOk)
			return false;
	}
}

bool Session::HandleFrame(const char* theData, uint32_t theLen)
{
	Cursor aCursor(theData, theLen);
	const MsgType aType = static_cast<MsgType>(aCursor.U8());
	if (!aCursor.mOk)
		return false;

	switch (aType)
	{
	case MsgType::Hello:
	{
		if (!mIsHost)
			return false;  // only the host receives Hello

		const uint32_t aVersion = aCursor.U32();
		const uint64_t aBuildHash = aCursor.U64();
		const uint64_t aAssetHash = aCursor.U64();
		if (!aCursor.mOk)
			return false;

		if (aVersion != PROTOCOL_VERSION)
		{
			SendReject(RejectReason::ProtocolMismatch);
			SetFailed(GetRejectReasonText(RejectReason::ProtocolMismatch));
			return false;
		}
		if (aBuildHash != mLocalBuildHash)
		{
			SendReject(RejectReason::BuildMismatch);
			SetFailed(GetRejectReasonText(RejectReason::BuildMismatch));
			return false;
		}
		if (aAssetHash != mLocalAssetHash)
		{
			SendReject(RejectReason::AssetMismatch);
			SetFailed(GetRejectReasonText(RejectReason::AssetMismatch));
			return false;
		}
		return SendWelcome();
	}

	case MsgType::Welcome:
	{
		if (mIsHost)
			return false;

		mParams.mDifficulty = static_cast<Difficulty>(aCursor.U8());
		mParams.mStartLevel = aCursor.U32();
		mParams.mRngSeed = aCursor.U32();
		mParams.mInputDelay = aCursor.U32();
		mParams.mUnlockFlags = aCursor.U32();
		if (!aCursor.mOk)
			return false;

		if (!SendReady())
			return false;
		mState = State::Connected;
		mStatusText = "Connected to host.";
		return true;
	}

	case MsgType::Reject:
	{
		const RejectReason aReason = static_cast<RejectReason>(aCursor.U8());
		SetFailed(GetRejectReasonText(aReason));
		return false;
	}

	case MsgType::Ready:
	{
		if (!mIsHost)
			return false;
		mState = State::Connected;
		mStatusText = "Player connected.";
		// Two players only: stop advertising.
		mListener.Close();
		return true;
	}

	case MsgType::Commands:
	{
		const uint32_t aCount = aCursor.U32();
		if (!aCursor.mOk || aCount > 256)
			return false;
		for (uint32_t i = 0; i < aCount; ++i)
		{
			Command aCommand;
			aCommand.mTick = aCursor.U32();
			aCommand.mPlayer = static_cast<PlayerId>(aCursor.U8());
			aCommand.mType = static_cast<CommandType>(aCursor.U8());
			aCommand.mX = aCursor.I16();
			aCommand.mY = aCursor.I16();
			aCommand.mArg = aCursor.I16();
			if (!aCursor.mOk)
				return false;
			mPendingCommands.push_back(aCommand);
		}
		return true;
	}

	case MsgType::Checksum:
		// Phase 2 compares these to catch a desync. Accepted and ignored for now.
		return true;

	case MsgType::Bye:
		mPeer.Close();
		mState = State::Disconnected;
		mStatusText = "The other player left the game.";
		return true;

	default:
		return false;
	}
}

bool Session::SendHello()
{
	Packer aPacker;
	aPacker.U8(static_cast<uint8_t>(MsgType::Hello));
	aPacker.U32(PROTOCOL_VERSION);
	aPacker.U64(mLocalBuildHash);
	aPacker.U64(mLocalAssetHash);
	return SendFrame(aPacker.mData.data(), static_cast<uint32_t>(aPacker.mData.size()));
}

bool Session::SendWelcome()
{
	Packer aPacker;
	aPacker.U8(static_cast<uint8_t>(MsgType::Welcome));
	aPacker.U8(static_cast<uint8_t>(mParams.mDifficulty));
	aPacker.U32(mParams.mStartLevel);
	aPacker.U32(mParams.mRngSeed);
	aPacker.U32(mParams.mInputDelay);
	aPacker.U32(mParams.mUnlockFlags);
	return SendFrame(aPacker.mData.data(), static_cast<uint32_t>(aPacker.mData.size()));
}

bool Session::SendReject(RejectReason theReason)
{
	Packer aPacker;
	aPacker.U8(static_cast<uint8_t>(MsgType::Reject));
	aPacker.U8(static_cast<uint8_t>(theReason));
	return SendFrame(aPacker.mData.data(), static_cast<uint32_t>(aPacker.mData.size()));
}

bool Session::SendReady()
{
	Packer aPacker;
	aPacker.U8(static_cast<uint8_t>(MsgType::Ready));
	return SendFrame(aPacker.mData.data(), static_cast<uint32_t>(aPacker.mData.size()));
}

bool Session::SendCommands(const std::vector<Command>& theCommands)
{
	if (!IsConnected())
		return false;

	Packer aPacker;
	aPacker.U8(static_cast<uint8_t>(MsgType::Commands));
	aPacker.U32(static_cast<uint32_t>(theCommands.size()));
	for (const Command& aCommand : theCommands)
	{
		aPacker.U32(aCommand.mTick);
		aPacker.U8(static_cast<uint8_t>(aCommand.mPlayer));
		aPacker.U8(static_cast<uint8_t>(aCommand.mType));
		aPacker.I16(aCommand.mX);
		aPacker.I16(aCommand.mY);
		aPacker.I16(aCommand.mArg);
	}
	return SendFrame(aPacker.mData.data(), static_cast<uint32_t>(aPacker.mData.size()));
}

bool Session::PopCommand(Command& theCommandOut)
{
	if (mPendingCommands.empty())
		return false;
	theCommandOut = mPendingCommands.front();
	mPendingCommands.pop_front();
	return true;
}

void Session::Update()
{
	switch (mState)
	{
	case State::Hosting:
		if (mListener.Accept(mPeer))
		{
			mState = State::Handshaking;
			mStatusText = "Player found, checking versions...";
		}
		return;

	case State::Connecting:
		mPeer.PollConnect();
		if (mPeer.GetStatus() == Socket::Status::Failed)
		{
			SetFailed("Could not reach the host: " + mPeer.GetError());
			return;
		}
		if (!mPeer.IsConnected())
			return;
		mState = State::Handshaking;
		mStatusText = "Connected, checking versions...";
		if (!mSentHello)
		{
			mSentHello = true;
			if (!SendHello())
				SetFailed("Lost the connection during the handshake.");
		}
		return;

	case State::Handshaking:
	case State::Connected:
		if (!FlushOutbound() || !PumpInbound())
		{
			if (mState != State::Disconnected && mState != State::Failed)
				SetFailed(mPeer.GetError().empty() ? "Connection lost." : mPeer.GetError());
			return;
		}
		if (!ProcessFrames())
		{
			if (mState != State::Disconnected && mState != State::Failed)
				SetFailed("The other player sent something unexpected; disconnected.");
		}
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// Launch options
// ---------------------------------------------------------------------------

LaunchOptions& GetLaunchOptions()
{
	static LaunchOptions sOptions;
	return sOptions;
}

namespace
{
	// Takes the value after a flag. A value that looks like another flag means
	// the user forgot the value, so "--join --host" is an error rather than a
	// connection to a host literally named "--host".
	bool TakeValue(int& i, int argc, char** argv, const char* theFlag,
		std::string& theValueOut, std::string& theErrorOut)
	{
		if (i + 1 >= argc || strncmp(argv[i + 1], "--", 2) == 0)
		{
			theErrorOut = std::string(theFlag) + " needs a value";
			return false;
		}
		theValueOut = argv[++i];
		return true;
	}

	// Accepts "1.2.3.4:1337" or a bare "1.2.3.4".
	bool SplitHostPort(const std::string& theText, std::string& theHostOut, uint16_t& thePortOut)
	{
		const size_t aColon = theText.rfind(':');
		if (aColon == std::string::npos)
		{
			theHostOut = theText;
			return !theHostOut.empty();
		}
		theHostOut = theText.substr(0, aColon);
		const std::string aPortText = theText.substr(aColon + 1);
		if (theHostOut.empty() || aPortText.empty())
			return false;
		const long aPort = strtol(aPortText.c_str(), nullptr, 10);
		if (aPort <= 0 || aPort > 65535)
			return false;
		thePortOut = static_cast<uint16_t>(aPort);
		return true;
	}
}

bool ParseLaunchOptions(int argc, char** argv, std::string& theErrorOut)
{
	LaunchOptions& aOptions = GetLaunchOptions();

	for (int i = 1; i < argc; ++i)
	{
		const std::string aArg = argv[i];
		std::string aValue;

		if (aArg == "--coop-selftest")
		{
			aOptions.mSelfTest = true;
		}
		else if (aArg == "--host")
		{
			aOptions.mWantHost = true;
		}
		else if (aArg == "--join")
		{
			if (!TakeValue(i, argc, argv, "--join", aValue, theErrorOut))
			{
				theErrorOut = "--join needs an address, e.g. --join 192.168.1.42:1337";
				return false;
			}
			if (!SplitHostPort(aValue, aOptions.mJoinHost, aOptions.mPort))
			{
				theErrorOut = "could not understand the address given to --join";
				return false;
			}
			aOptions.mWantJoin = true;
		}
		else if (aArg == "--port")
		{
			if (!TakeValue(i, argc, argv, "--port", aValue, theErrorOut))
				return false;
			const long aPort = strtol(aValue.c_str(), nullptr, 10);
			if (aPort <= 0 || aPort > 65535) { theErrorOut = "--port must be between 1 and 65535"; return false; }
			aOptions.mPort = static_cast<uint16_t>(aPort);
		}
		else if (aArg == "--difficulty")
		{
			if (!TakeValue(i, argc, argv, "--difficulty", aValue, theErrorOut))
				return false;
			if (aValue == "normal")    aOptions.mDifficulty = Difficulty::Normal;
			else if (aValue == "hard") aOptions.mDifficulty = Difficulty::Hard;
			else { theErrorOut = "--difficulty must be 'normal' or 'hard'"; return false; }
		}
		// Unknown arguments belong to the base game; leave them alone.
	}

	if (aOptions.mWantHost && aOptions.mWantJoin)
	{
		theErrorOut = "--host and --join cannot both be given";
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

// Runs a host and a client in one process over loopback and checks that the
// handshake completes and the session parameters agree. Needs no game assets,
// so it runs in CI and proves the transport independently of the game.
int RunSelfTest()
{
	printf("coop selftest: starting\n");

#if !COOP_SOCKETS_AVAILABLE
	printf("coop selftest: SKIP (sockets unavailable on this platform)\n");
	return 0;
#else
	Socket::Startup();

	Session aHost;
	Session aClient;

	const uint16_t aPort = 34567;
	if (!aHost.StartHost(aPort, Difficulty::Hard, 1))
	{
		printf("coop selftest: FAIL host: %s\n", aHost.GetStatusText().c_str());
		return 1;
	}
	printf("coop selftest: hosting on port %u\n", aPort);

	if (!aClient.StartJoin("127.0.0.1", aPort))
	{
		printf("coop selftest: FAIL join: %s\n", aClient.GetStatusText().c_str());
		return 1;
	}

	// Pump both ends as the game loop would, with a generous bound so a
	// failure reports instead of hanging.
	for (int i = 0; i < 2000; ++i)
	{
		aHost.Update();
		aClient.Update();
		if (aHost.IsConnected() && aClient.IsConnected())
			break;
		if (aHost.GetState() == Session::State::Failed || aClient.GetState() == Session::State::Failed)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	if (!aHost.IsConnected() || !aClient.IsConnected())
	{
		printf("coop selftest: FAIL handshake did not complete\n");
		printf("  host:   %s\n", aHost.GetStatusText().c_str());
		printf("  client: %s\n", aClient.GetStatusText().c_str());
		return 1;
	}
	printf("coop selftest: handshake OK\n");

	const SessionParams& aHostParams = aHost.GetParams();
	const SessionParams& aClientParams = aClient.GetParams();
	if (aHostParams.mRngSeed != aClientParams.mRngSeed)
	{
		printf("coop selftest: FAIL rng seed differs (%u vs %u)\n",
			aHostParams.mRngSeed, aClientParams.mRngSeed);
		return 1;
	}
	if (aHostParams.mDifficulty != aClientParams.mDifficulty)
	{
		printf("coop selftest: FAIL difficulty differs\n");
		return 1;
	}
	if (aHostParams.mInputDelay != aClientParams.mInputDelay)
	{
		printf("coop selftest: FAIL input delay differs\n");
		return 1;
	}
	printf("coop selftest: params agree (seed=%u difficulty=%s delay=%u)\n",
		aHostParams.mRngSeed,
		aHostParams.mDifficulty == Difficulty::Hard ? "hard" : "normal",
		aHostParams.mInputDelay);

	// Round-trip a command the way Phase 2 will.
	std::vector<Command> aOutgoing;
	Command aPlant;
	aPlant.mTick = 42;
	aPlant.mPlayer = PlayerId::Client;
	aPlant.mType = CommandType::PlantAt;
	aPlant.mX = 5;
	aPlant.mY = 3;
	aPlant.mArg = 2;
	aOutgoing.push_back(aPlant);

	if (!aClient.SendCommands(aOutgoing))
	{
		printf("coop selftest: FAIL could not send commands\n");
		return 1;
	}

	Command aReceived;
	bool aGotIt = false;
	for (int i = 0; i < 2000 && !aGotIt; ++i)
	{
		aHost.Update();
		aClient.Update();
		aGotIt = aHost.PopCommand(aReceived);
		if (!aGotIt)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	if (!aGotIt)
	{
		printf("coop selftest: FAIL command did not arrive\n");
		return 1;
	}
	if (aReceived.mTick != aPlant.mTick || aReceived.mPlayer != aPlant.mPlayer
		|| aReceived.mType != aPlant.mType || aReceived.mX != aPlant.mX
		|| aReceived.mY != aPlant.mY || aReceived.mArg != aPlant.mArg)
	{
		printf("coop selftest: FAIL command round-tripped incorrectly\n");
		return 1;
	}
	printf("coop selftest: command round-trip OK\n");

	aClient.Disconnect("selftest done");
	aHost.Disconnect("selftest done");
	Socket::Cleanup();

	printf("coop selftest: PASS\n");
	return 0;
#endif
}

} // namespace Coop
