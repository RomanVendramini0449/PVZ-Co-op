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

#include "CoopSocket.h"

#if COOP_SOCKETS_AVAILABLE

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#define COOP_INVALID_SOCKET  static_cast<intptr_t>(INVALID_SOCKET)
#define COOP_LAST_ERROR      WSAGetLastError()
#define COOP_WOULD_BLOCK     WSAEWOULDBLOCK
#define COOP_IN_PROGRESS     WSAEWOULDBLOCK
#define COOP_CLOSE(s)        closesocket(static_cast<SOCKET>(s))
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ifaddrs.h>
#define COOP_INVALID_SOCKET  (-1)
#define COOP_LAST_ERROR      errno
#define COOP_WOULD_BLOCK     EWOULDBLOCK
#define COOP_IN_PROGRESS     EINPROGRESS
#define COOP_CLOSE(s)        ::close(static_cast<int>(s))
#endif

namespace Coop
{

namespace
{
	// A SOCKET on 64-bit Windows is a 64-bit UINT_PTR; casting it to int
	// would silently corrupt the handle. POSIX file descriptors really are
	// int. This keeps every call site at the platform's own width.
#ifdef _WIN32
	using NativeSocket = SOCKET;
#else
	using NativeSocket = int;
#endif

	inline NativeSocket Native(intptr_t theHandle)
	{
		return static_cast<NativeSocket>(theHandle);
	}

	int gStartupCount = 0;

	bool IsWouldBlock(int theError)
	{
		if (theError == COOP_WOULD_BLOCK)
			return true;
#ifndef _WIN32
		if (theError == EAGAIN || theError == EINTR)
			return true;
#endif
		return false;
	}

	bool SetNonBlocking(intptr_t theHandle)
	{
#ifdef _WIN32
		u_long aMode = 1;
		return ioctlsocket(Native(theHandle), FIONBIO, &aMode) == 0;
#else
		int aFlags = fcntl(Native(theHandle), F_GETFL, 0);
		if (aFlags < 0)
			return false;
		return fcntl(Native(theHandle), F_SETFL, aFlags | O_NONBLOCK) == 0;
#endif
	}
}

void Socket::Startup()
{
	if (gStartupCount++ > 0)
		return;
#ifdef _WIN32
	WSADATA aData;
	WSAStartup(MAKEWORD(2, 2), &aData);
#endif
}

void Socket::Cleanup()
{
	if (gStartupCount == 0 || --gStartupCount > 0)
		return;
#ifdef _WIN32
	WSACleanup();
#endif
}

Socket::Socket() : mHandle(COOP_INVALID_SOCKET), mStatus(Status::Closed) {}

Socket::~Socket() { Close(); }

void Socket::SetError(const std::string& theWhat)
{
	mError = theWhat + " (error " + std::to_string(COOP_LAST_ERROR) + ")";
	mStatus = Status::Failed;
}

// Latency matters more than packet efficiency at ~16 bytes per tick, so Nagle
// goes off: a held-back input frame stalls the peer's whole simulation.
bool Socket::ApplyCommonOptions()
{
	int aFlag = 1;
	setsockopt(Native(mHandle), IPPROTO_TCP, TCP_NODELAY,
		reinterpret_cast<const char*>(&aFlag), sizeof(aFlag));
	return SetNonBlocking(mHandle);
}

bool Socket::Listen(uint16_t thePort)
{
	Close();
	mHandle = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
	if (mHandle == COOP_INVALID_SOCKET)
	{
		SetError("could not create socket");
		return false;
	}

	// Without SO_REUSEADDR, re-hosting within TIME_WAIT fails with "address
	// already in use" for a minute or two, which looks like a broken game.
	int aReuse = 1;
	setsockopt(Native(mHandle), SOL_SOCKET, SO_REUSEADDR,
		reinterpret_cast<const char*>(&aReuse), sizeof(aReuse));

	sockaddr_in aAddr{};
	aAddr.sin_family = AF_INET;
	aAddr.sin_addr.s_addr = INADDR_ANY;
	aAddr.sin_port = htons(thePort);

	if (bind(Native(mHandle), reinterpret_cast<sockaddr*>(&aAddr), sizeof(aAddr)) != 0)
	{
		SetError("could not bind port " + std::to_string(thePort));
		return false;
	}
	if (::listen(Native(mHandle), 1) != 0)
	{
		SetError("could not listen on port " + std::to_string(thePort));
		return false;
	}
	if (!ApplyCommonOptions())
	{
		SetError("could not set socket options");
		return false;
	}

	mStatus = Status::Listening;
	return true;
}

bool Socket::Accept(Socket& theClient)
{
	if (mStatus != Status::Listening)
		return false;

	sockaddr_in aAddr{};
	socklen_t aLen = sizeof(aAddr);
	intptr_t aAccepted = static_cast<intptr_t>(
		accept(Native(mHandle), reinterpret_cast<sockaddr*>(&aAddr), &aLen));

	if (aAccepted == COOP_INVALID_SOCKET)
		return false;  // nobody waiting; not an error

	theClient.Close();
	theClient.mHandle = aAccepted;
	theClient.mStatus = Status::Connected;
	theClient.ApplyCommonOptions();
	return true;
}

bool Socket::Connect(const std::string& theHost, uint16_t thePort)
{
	Close();

	addrinfo aHints{};
	aHints.ai_family = AF_INET;
	aHints.ai_socktype = SOCK_STREAM;
	aHints.ai_protocol = IPPROTO_TCP;

	addrinfo* aResult = nullptr;
	const std::string aPortStr = std::to_string(thePort);
	if (getaddrinfo(theHost.c_str(), aPortStr.c_str(), &aHints, &aResult) != 0 || aResult == nullptr)
	{
		mError = "could not resolve " + theHost;
		mStatus = Status::Failed;
		return false;
	}

	mHandle = static_cast<intptr_t>(socket(aResult->ai_family, aResult->ai_socktype, aResult->ai_protocol));
	if (mHandle == COOP_INVALID_SOCKET)
	{
		freeaddrinfo(aResult);
		SetError("could not create socket");
		return false;
	}
	if (!ApplyCommonOptions())
	{
		freeaddrinfo(aResult);
		SetError("could not set socket options");
		return false;
	}

	const int aRet = ::connect(Native(mHandle), aResult->ai_addr,
		static_cast<socklen_t>(aResult->ai_addrlen));
	freeaddrinfo(aResult);

	if (aRet == 0)
	{
		mStatus = Status::Connected;
		return true;
	}
	if (IsWouldBlock(COOP_LAST_ERROR) || COOP_LAST_ERROR == COOP_IN_PROGRESS)
	{
		mStatus = Status::Connecting;  // finishes in PollConnect()
		return true;
	}

	SetError("could not connect to " + theHost + ":" + aPortStr);
	return false;
}

void Socket::PollConnect()
{
	if (mStatus != Status::Connecting)
		return;

	fd_set aWriteSet;
	FD_ZERO(&aWriteSet);
	FD_SET(Native(mHandle), &aWriteSet);

	// Zero timeout: this is called from the game loop and must not block.
	timeval aTimeout{};
#ifdef _WIN32
	const int aNfds = 0;  // ignored by Winsock
#else
	const int aNfds = static_cast<int>(mHandle) + 1;
#endif
	const int aReady = select(aNfds, nullptr, &aWriteSet, nullptr, &aTimeout);
	if (aReady <= 0)
		return;  // still pending

	// Writability alone does not mean success; ask for the real result.
	int aSockError = 0;
	socklen_t aLen = sizeof(aSockError);
	if (getsockopt(Native(mHandle), SOL_SOCKET, SO_ERROR,
			reinterpret_cast<char*>(&aSockError), &aLen) != 0 || aSockError != 0)
	{
		mError = "connection refused or unreachable";
		mStatus = Status::Failed;
		return;
	}

	mStatus = Status::Connected;
}

bool Socket::Send(const void* theData, size_t theLen, size_t& theSentOut)
{
	theSentOut = 0;
	if (mStatus != Status::Connected)
		return false;

#ifdef MSG_NOSIGNAL
	const int aFlags = MSG_NOSIGNAL;  // a dead peer must not SIGPIPE the game
#else
	const int aFlags = 0;
#endif

	const auto aSent = send(Native(mHandle),
		static_cast<const char*>(theData), static_cast<int>(theLen), aFlags);
	if (aSent < 0)
	{
		if (IsWouldBlock(COOP_LAST_ERROR))
			return true;  // buffer full; caller retries next tick
		SetError("send failed");
		return false;
	}

	theSentOut = static_cast<size_t>(aSent);
	return true;
}

bool Socket::Receive(void* theBuffer, size_t theLen, size_t& theReadOut)
{
	theReadOut = 0;
	if (mStatus != Status::Connected)
		return false;

	const auto aRead = recv(Native(mHandle),
		static_cast<char*>(theBuffer), static_cast<int>(theLen), 0);
	if (aRead == 0)
	{
		mError = "the other player disconnected";
		mStatus = Status::Closed;
		return false;
	}
	if (aRead < 0)
	{
		if (IsWouldBlock(COOP_LAST_ERROR))
			return true;  // nothing waiting
		SetError("receive failed");
		return false;
	}

	theReadOut = static_cast<size_t>(aRead);
	return true;
}

void Socket::Close()
{
	if (mHandle != COOP_INVALID_SOCKET)
	{
		COOP_CLOSE(mHandle);
		mHandle = COOP_INVALID_SOCKET;
	}
	if (mStatus != Status::Failed)
		mStatus = Status::Closed;
}

std::vector<std::string> Socket::GetLocalAddresses()
{
	std::vector<std::string> aResult;
#ifndef _WIN32
	ifaddrs* aList = nullptr;
	if (getifaddrs(&aList) == 0)
	{
		for (ifaddrs* aIt = aList; aIt != nullptr; aIt = aIt->ifa_next)
		{
			if (aIt->ifa_addr == nullptr || aIt->ifa_addr->sa_family != AF_INET)
				continue;
			char aBuf[INET_ADDRSTRLEN] = {};
			auto* aIn = reinterpret_cast<sockaddr_in*>(aIt->ifa_addr);
			if (inet_ntop(AF_INET, &aIn->sin_addr, aBuf, sizeof(aBuf)) == nullptr)
				continue;
			if (std::strcmp(aBuf, "127.0.0.1") == 0)
				continue;
			aResult.emplace_back(aBuf);
		}
		freeifaddrs(aList);
	}
#else
	char aHostName[256] = {};
	if (gethostname(aHostName, sizeof(aHostName) - 1) == 0)
	{
		addrinfo aHints{};
		aHints.ai_family = AF_INET;
		addrinfo* aInfo = nullptr;
		if (getaddrinfo(aHostName, nullptr, &aHints, &aInfo) == 0)
		{
			for (addrinfo* aIt = aInfo; aIt != nullptr; aIt = aIt->ai_next)
			{
				char aBuf[INET_ADDRSTRLEN] = {};
				auto* aIn = reinterpret_cast<sockaddr_in*>(aIt->ai_addr);
				if (inet_ntop(AF_INET, &aIn->sin_addr, aBuf, sizeof(aBuf)) == nullptr)
					continue;
				if (std::strcmp(aBuf, "127.0.0.1") == 0)
					continue;
				aResult.emplace_back(aBuf);
			}
			freeaddrinfo(aInfo);
		}
	}
#endif
	return aResult;
}

} // namespace Coop

#else // !COOP_SOCKETS_AVAILABLE

// Emscripten and Switch: co-op is compiled out, but the type still has to
// exist so the rest of the game builds. Everything fails cleanly.
namespace Coop
{

Socket::Socket() : mHandle(-1), mStatus(Status::Closed) {}
Socket::~Socket() {}
void Socket::Startup() {}
void Socket::Cleanup() {}
void Socket::SetError(const std::string& theWhat) { mError = theWhat; mStatus = Status::Failed; }
bool Socket::ApplyCommonOptions() { return false; }
bool Socket::Listen(uint16_t) { SetError("co-op is not available on this platform"); return false; }
bool Socket::Accept(Socket&) { return false; }
bool Socket::Connect(const std::string&, uint16_t) { SetError("co-op is not available on this platform"); return false; }
void Socket::PollConnect() {}
bool Socket::Send(const void*, size_t, size_t& theSentOut) { theSentOut = 0; return false; }
bool Socket::Receive(void*, size_t, size_t& theReadOut) { theReadOut = 0; return false; }
void Socket::Close() {}
std::vector<std::string> Socket::GetLocalAddresses() { return {}; }

} // namespace Coop

#endif // COOP_SOCKETS_AVAILABLE
