// LauncherCore (docs/PORT_POSIX_PLAN.md, F10): pins the wire framing and
// packet layout against what VersionManager (src/Server/VersionManager/
// User.cpp) actually sends/expects for LS_VERSION_REQ/LS_DOWNLOADINFO_REQ,
// and the CAPISocket-compatible 0xAA55/0x55AA frame wrapper
// (src/Client/WarFare/APISocket.cpp) - independent of any socket or display.

#include <gtest/gtest.h>

#include <Client/Launcher/LauncherCore.h>
#include <shared/packets.h>

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(LauncherCoreTest, FrameMessageMatchesCAPISocketWireFormat)
{
	const std::vector<uint8_t> payload = { 0x01, 0x0A, 0x00 };
	const std::vector<uint8_t> frame   = launcher_core::FrameMessage(payload);

	// 0xAA 0x55 header, u16 LE size (3), payload, 0x55 0xAA tail.
	const std::vector<uint8_t> expected = { 0xAA, 0x55, 0x03, 0x00, 0x01, 0x0A, 0x00, 0x55, 0xAA };
	EXPECT_EQ(frame, expected);
}

TEST(LauncherCoreTest, TryExtractFrameRoundTrips)
{
	const std::vector<uint8_t> payload = { 0x05, 0x06, 0x07, 0x08 };
	std::vector<uint8_t> buffer        = launcher_core::FrameMessage(payload);

	std::vector<uint8_t> extracted;
	ASSERT_TRUE(launcher_core::TryExtractFrame(buffer, extracted));
	EXPECT_EQ(extracted, payload);
	EXPECT_TRUE(buffer.empty()); // fully consumed
}

TEST(LauncherCoreTest, TryExtractFrameHandlesPartialData)
{
	std::vector<uint8_t> frame = launcher_core::FrameMessage({ 0x01, 0x02, 0x03 });
	std::vector<uint8_t> buffer(frame.begin(), frame.begin() + 4); // truncated mid-payload

	std::vector<uint8_t> extracted;
	EXPECT_FALSE(launcher_core::TryExtractFrame(buffer, extracted));
	EXPECT_EQ(buffer.size(), 4u); // untouched until the rest arrives
}

TEST(LauncherCoreTest, TryExtractFrameHandlesTwoFramesInOneBuffer)
{
	std::vector<uint8_t> buffer = launcher_core::FrameMessage({ 0xAA });
	const std::vector<uint8_t> second = launcher_core::FrameMessage({ 0xBB, 0xCC });
	buffer.insert(buffer.end(), second.begin(), second.end());

	std::vector<uint8_t> first;
	ASSERT_TRUE(launcher_core::TryExtractFrame(buffer, first));
	EXPECT_EQ(first, (std::vector<uint8_t> { 0xAA }));

	std::vector<uint8_t> secondOut;
	ASSERT_TRUE(launcher_core::TryExtractFrame(buffer, secondOut));
	EXPECT_EQ(secondOut, (std::vector<uint8_t> { 0xBB, 0xCC }));
	EXPECT_TRUE(buffer.empty());
}

TEST(LauncherCoreTest, TryExtractFrameRejectsBadHeader)
{
	std::vector<uint8_t> buffer = { 0x00, 0x00, 0x01, 0x00, 0xFF, 0x55, 0xAA };
	std::vector<uint8_t> out;
	EXPECT_FALSE(launcher_core::TryExtractFrame(buffer, out));
}

TEST(LauncherCoreTest, VersionRequestRoundTripsThroughVersionManagerLayout)
{
	// BuildVersionRequest mirrors what CLauncherDlg::PacketSend_VersionReq
	// sends; the byte layout must be opcode + i16 LE version, matching what
	// CUser::Parsing's LS_VERSION_REQ case (client -> server direction is
	// symmetric with the response) expects on the server side.
	const std::vector<uint8_t> req = launcher_core::BuildVersionRequest(1298);
	ASSERT_EQ(req.size(), 3u);
	EXPECT_EQ(req[0], LS_VERSION_REQ);
	EXPECT_EQ(req[1], 0x12); // 1298 = 0x0512, LE low byte
	EXPECT_EQ(req[2], 0x05); // LE high byte
}

TEST(LauncherCoreTest, ParsesVersionResponseFromVersionManager)
{
	// Byte-for-byte what CUser::Parsing's LS_VERSION_REQ case sends:
	// SetByte(LS_VERSION_REQ) + SetShort(LastVersion).
	const std::vector<uint8_t> payload = { LS_VERSION_REQ, 0x12, 0x05 }; // version 1298

	launcher_core::VersionResponse resp;
	ASSERT_TRUE(launcher_core::ParseVersionResponse(payload, resp));
	EXPECT_EQ(resp.serverVersion, 1298);
}

TEST(LauncherCoreTest, RejectsVersionResponseWithWrongOpcode)
{
	const std::vector<uint8_t> payload = { LS_DOWNLOADINFO_REQ, 0x00, 0x00 };
	launcher_core::VersionResponse resp;
	EXPECT_FALSE(launcher_core::ParseVersionResponse(payload, resp));
}

TEST(LauncherCoreTest, ParsesDownloadInfoResponseFromVersionManager)
{
	// Byte-for-byte CUser::SendDownloadInfo's layout: opcode, String2 url,
	// String2 path, i16 count, String2 filename * count.
	std::vector<uint8_t> payload = { LS_DOWNLOADINFO_REQ };

	auto appendString2 = [&payload](const std::string& s) {
		const int16_t len = static_cast<int16_t>(s.size());
		payload.push_back(static_cast<uint8_t>(len & 0xff));
		payload.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
		payload.insert(payload.end(), s.begin(), s.end());
	};

	appendString2("ftp.example.com");
	appendString2("/patches");
	payload.push_back(2); // count = 2 (LE)
	payload.push_back(0);
	appendString2("patch1299.zip");
	appendString2("patch1300.zip");

	launcher_core::DownloadInfoResponse resp;
	ASSERT_TRUE(launcher_core::ParseDownloadInfoResponse(payload, resp));
	EXPECT_EQ(resp.ftpUrl, "ftp.example.com");
	EXPECT_EQ(resp.ftpPath, "/patches");
	ASSERT_EQ(resp.fileNames.size(), 2u);
	EXPECT_EQ(resp.fileNames[0], "patch1299.zip");
	EXPECT_EQ(resp.fileNames[1], "patch1300.zip");
}

namespace
{
fs::path MakeTempIniPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);
	return fs::temp_directory_path()
		   / ("LauncherCoreTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++) + ".ini");
}
} // namespace

TEST(LauncherCoreTest, ReadServerIpListMatchesServerIniFormat)
{
	const fs::path path = MakeTempIniPath();
	{
		std::ofstream out(path);
		out << "[Server]\n";
		out << "Count=2\n";
		out << "IP0=127.0.0.1\n";
		out << "IP1=192.168.1.10\n";
	}

	const std::vector<std::string> ips = launcher_core::ReadServerIpList(path.string());
	ASSERT_EQ(ips.size(), 2u);
	EXPECT_EQ(ips[0], "127.0.0.1");
	EXPECT_EQ(ips[1], "192.168.1.10");

	std::error_code ec;
	fs::remove(path, ec);
}

TEST(LauncherCoreTest, ReadServerIpListEmptyWhenFileMissing)
{
	const fs::path path = MakeTempIniPath();
	std::error_code ec;
	fs::remove(path, ec);

	EXPECT_TRUE(launcher_core::ReadServerIpList(path.string()).empty());
}
