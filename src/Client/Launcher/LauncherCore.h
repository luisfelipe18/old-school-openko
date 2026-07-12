#ifndef CLIENT_LAUNCHER_LAUNCHERCORE_H
#define CLIENT_LAUNCHER_LAUNCHERCORE_H

#pragma once

// Platform-neutral logic behind the POSIX Launcher tool (docs/PORT_POSIX_PLAN.md,
// F10): packet framing/building/parsing and the Server.Ini reader, kept
// separate from SDL/Dear ImGui/Asio so it's unit-testable without a socket
// or a display.
//
// The original LauncherDlg.cpp (Windows/MFC) talks to VersionManager over
// the exact same connection WarFare's own login scene uses: the IPs listed
// in Server.Ini, port 15100 (CAPISocket's SOCKET_PORT_LOGIN /
// CONNECT_PORT), framed with the same 0xAA55-header/0x55AA-tail wrapping
// CAPISocket::Send implements (src/Client/WarFare/APISocket.cpp - verified
// byte-for-byte against it). LS_VERSION_REQ/LS_DOWNLOADINFO_REQ are answered
// in the clear (src/Server/VersionManager/User.cpp never calls into
// JvCryption for them), so no encryption handshake is needed here.
//
// LS_LOGIN_REQ/LS_SERVERLIST are deliberately NOT reimplemented: the
// original Launcher.PacketDef.h defines LOGIN_REQ=0x03/SERVER_LIST=0x05,
// which don't match the current protocol's LS_LOGIN_REQ=0xF3/
// LS_SERVERLIST=0xF5 - that path is already dead code against the real
// VersionManager on Windows, so there's nothing working to port. WarFare's
// own login scene already owns the real login/server-list flow.

#include <cstdint>
#include <string>
#include <vector>

namespace launcher_core
{

// --- Wire framing --------------------------------------------------------

// Wraps `payload` as CAPISocket::Send would: 0xAA 0x55, u16 LE payload
// size, payload, 0x55 0xAA.
std::vector<uint8_t> FrameMessage(const std::vector<uint8_t>& payload);

// Scans `buffer` for one complete frame at offset 0. On a match, erases the
// consumed bytes from `buffer` (so partial/multiple frames across several
// socket reads work the same way CAPISocket::ReceiveProcess's ring buffer
// does) and returns true with the payload in `outPayload`. Returns false
// (leaving `buffer` untouched) when no complete frame is present yet.
bool TryExtractFrame(std::vector<uint8_t>& buffer, std::vector<uint8_t>& outPayload);

// --- Packet building/parsing ----------------------------------------------

std::vector<uint8_t> BuildVersionRequest(int clientVersion);
std::vector<uint8_t> BuildDownloadInfoRequest(int clientVersion);

struct VersionResponse
{
	int serverVersion = 0;
};
bool ParseVersionResponse(const std::vector<uint8_t>& payload, VersionResponse& out);

struct DownloadInfoResponse
{
	std::string ftpUrl;
	std::string ftpPath;
	std::vector<std::string> fileNames;
};
bool ParseDownloadInfoResponse(const std::vector<uint8_t>& payload, DownloadInfoResponse& out);

// --- Server.Ini ------------------------------------------------------------

// [Server] Count / IP0.. - the same list WarFare's login scene connects to.
std::vector<std::string> ReadServerIpList(const std::string& serverIniPath);

} // namespace launcher_core

#endif // CLIENT_LAUNCHER_LAUNCHERCORE_H
