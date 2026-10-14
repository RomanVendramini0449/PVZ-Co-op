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

#ifndef __COOPSOCKET_H__
#define __COOPSOCKET_H__

// Raw TCP is unavailable in the browser, and the Switch build would need
// libnx socket init we have not done. Co-op compiles out entirely there.
#if defined(__EMSCRIPTEN__) || defined(__SWITCH__)
#define COOP_SOCKETS_AVAILABLE 0
#else
#define COOP_SOCKETS_AVAILABLE 1
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace Coop
{

// Non-blocking TCP, thin enough to read in one sitting. The game loop drives
// it: nothing here ever blocks the 100 Hz tick.
class Socket
{
public:
	enum class Status
	{
		Closed,
		Listening,
		Connecting,
		Connected,
		Failed,
	};

private:
	intptr_t    mHandle;       // SOCKET on Windows, int fd elsewhere
	Status      mStatus;
	std::string mError;

	void        SetError(const std::string& theWhat);
	bool        ApplyCommonOptions();

public:
	Socket();
	Socket(const Socket&) = delete;
	Socket& operator=(const Socket&) = delete;
	~Socket();

	// Process-wide init/teardown. No-ops outside Windows; safe to nest.
	static void Startup();
	static void Cleanup();

	bool        Listen(uint16_t thePort);
	// Non-blocking accept. Returns true and fills theClient when someone
	// connects; false means nobody yet (not an error).
	bool        Accept(Socket& theClient);
	// Begins a non-blocking connect; poll with PollConnect().
	bool        Connect(const std::string& theHost, uint16_t thePort);
	// Drives an in-progress connect. Status becomes Connected or Failed.
	void        PollConnect();

	// Queues bytes; returns false only on a dead connection. Partial writes
	// are handled by the caller's outbound buffer.
	bool        Send(const void* theData, size_t theLen, size_t& theSentOut);
	// Reads whatever is available. Returns false if the peer closed or errored.
	// theReadOut == 0 with a true return simply means nothing was waiting.
	bool        Receive(void* theBuffer, size_t theLen, size_t& theReadOut);

	void        Close();
	// Close after saying everything: stop sending, then read and discard
	// whatever the peer still has in flight for a moment before closing.
	// Closing with unread data in the buffer makes the OS reset the
	// connection, and a reset can wipe out the last thing we sent -- here,
	// the goodbye that tells the other player why the game ended.
	void        CloseGracefully();

	Status      GetStatus() const { return mStatus; }
	bool        IsConnected() const { return mStatus == Status::Connected; }
	const std::string& GetError() const { return mError; }

	// Best-effort local IPv4 addresses, so the host can tell their friend what
	// to type without leaving the game.
	static std::vector<std::string> GetLocalAddresses();
};

} // namespace Coop

#endif
