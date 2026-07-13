#include "LauncherCore.h"

#include <Platform/PlatformIni.h>

#include <shared/Packet.h>
#include <shared/packets.h>

#include <array>
#include <cstring>

namespace launcher_core
{

namespace
{
constexpr uint8_t HEADER_0 = 0xAA;
constexpr uint8_t HEADER_1 = 0x55;
constexpr uint8_t TAIL_0   = 0x55;
constexpr uint8_t TAIL_1   = 0xAA;
} // namespace

std::vector<uint8_t> FrameMessage(const std::vector<uint8_t>& payload)
{
	std::vector<uint8_t> frame;
	frame.reserve(payload.size() + 6);

	frame.push_back(HEADER_0);
	frame.push_back(HEADER_1);

	const uint16_t size = static_cast<uint16_t>(payload.size());
	frame.push_back(static_cast<uint8_t>(size & 0xff));
	frame.push_back(static_cast<uint8_t>((size >> 8) & 0xff));

	frame.insert(frame.end(), payload.begin(), payload.end());

	frame.push_back(TAIL_0);
	frame.push_back(TAIL_1);

	return frame;
}

bool TryExtractFrame(std::vector<uint8_t>& buffer, std::vector<uint8_t>& outPayload)
{
	if (buffer.size() < 6)
		return false;
	if (buffer[0] != HEADER_0 || buffer[1] != HEADER_1)
		return false;

	const uint16_t size = static_cast<uint16_t>(buffer[2]) | (static_cast<uint16_t>(buffer[3]) << 8);
	const size_t frameLen = static_cast<size_t>(size) + 6;
	if (buffer.size() < frameLen)
		return false;
	if (buffer[frameLen - 2] != TAIL_0 || buffer[frameLen - 1] != TAIL_1)
		return false;

	outPayload.assign(buffer.begin() + 4, buffer.begin() + 4 + size);
	buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameLen));
	return true;
}

std::vector<uint8_t> BuildVersionRequest(int clientVersion)
{
	Packet pkt(LS_VERSION_REQ);
	pkt.append<int16_t>(static_cast<int16_t>(clientVersion));
	return std::vector<uint8_t>(pkt.storage().begin(), pkt.storage().end());
}

std::vector<uint8_t> BuildDownloadInfoRequest(int clientVersion)
{
	Packet pkt(LS_DOWNLOADINFO_REQ);
	pkt.append<int16_t>(static_cast<int16_t>(clientVersion));
	return std::vector<uint8_t>(pkt.storage().begin(), pkt.storage().end());
}

bool ParseVersionResponse(const std::vector<uint8_t>& payload, VersionResponse& out)
{
	if (payload.empty())
		return false;

	Packet pkt;
	pkt.append(payload.data(), payload.size());

	const uint8_t opcode = pkt.read<uint8_t>();
	if (opcode != LS_VERSION_REQ)
		return false;
	if (pkt.rpos() + sizeof(int16_t) > pkt.size())
		return false;

	out.serverVersion = pkt.read<int16_t>();
	return true;
}

bool ParseDownloadInfoResponse(const std::vector<uint8_t>& payload, DownloadInfoResponse& out)
{
	if (payload.empty())
		return false;

	Packet pkt;
	pkt.append(payload.data(), payload.size());

	const uint8_t opcode = pkt.read<uint8_t>();
	if (opcode != LS_DOWNLOADINFO_REQ)
		return false;

	auto readString2 = [&pkt](std::string& dest) -> bool {
		if (pkt.rpos() + sizeof(int16_t) > pkt.size())
			return false;
		const int16_t len = pkt.read<int16_t>();
		if (len < 0)
			return false;
		return pkt.readString(dest, static_cast<size_t>(len));
	};

	if (!readString2(out.ftpUrl) || !readString2(out.ftpPath))
		return false;

	if (pkt.rpos() + sizeof(int16_t) > pkt.size())
		return false;
	const int16_t fileCount = pkt.read<int16_t>();
	if (fileCount < 0 || fileCount > 64)
		return false;

	out.fileNames.clear();
	out.fileNames.reserve(static_cast<size_t>(fileCount));
	for (int i = 0; i < fileCount; ++i)
	{
		std::string name;
		if (!readString2(name))
			return false;
		out.fileNames.push_back(std::move(name));
	}

	return true;
}

std::vector<std::string> ReadServerIpList(const std::string& serverIniPath)
{
	std::vector<std::string> ips;

	const int count = GetPrivateProfileInt("Server", "Count", 0, serverIniPath.c_str());
	ips.reserve(static_cast<size_t>(count > 0 ? count : 0));

	for (int i = 0; i < count; ++i)
	{
		const std::string key = "IP" + std::to_string(i);
		std::array<char, 128> ip {};
		GetPrivateProfileString("Server", key.c_str(), "", ip.data(), static_cast<DWORD>(ip.size()),
			serverIniPath.c_str());
		if (ip[0] != '\0')
			ips.emplace_back(ip.data());
	}

	return ips;
}

} // namespace launcher_core
