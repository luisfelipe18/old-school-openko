#include <gtest/gtest.h>

#include <LauncherPatch.h>

#include <miniz.h>

#include <asio.hpp>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// --- URL parsing ------------------------------------------------------------

TEST(LauncherPatchUrl, ParsesHttpAndHttps)
{
	launcher_patch::ParsedUrl http = launcher_patch::ParseUrl("http://patch.example.com/a/b.zip");
	ASSERT_TRUE(http.valid);
	EXPECT_EQ(http.scheme, "http");
	EXPECT_EQ(http.host, "patch.example.com");
	EXPECT_EQ(http.port, "80");
	EXPECT_EQ(http.target, "/a/b.zip");

	launcher_patch::ParsedUrl https = launcher_patch::ParseUrl("https://cdn.example.com:8443/x");
	ASSERT_TRUE(https.valid);
	EXPECT_EQ(https.scheme, "https");
	EXPECT_EQ(https.host, "cdn.example.com");
	EXPECT_EQ(https.port, "8443");
	EXPECT_EQ(https.target, "/x");

	// No path -> "/".
	EXPECT_EQ(launcher_patch::ParseUrl("http://host").target, "/");
	// Not an http(s) URL.
	EXPECT_FALSE(launcher_patch::ParseUrl("ftp://legacy/host").valid);
	EXPECT_FALSE(launcher_patch::ParseUrl("host/path").valid);
}

// --- URL building -----------------------------------------------------------

TEST(LauncherPatchUrl, BuildsFromBareHostAndPath)
{
	// The version manager sends a bare host + dir; default to http://.
	EXPECT_EQ(launcher_patch::BuildPatchUrl("patch.example.com", "/patches", "p1300.zip"),
		"http://patch.example.com/patches/p1300.zip");

	// Doubled/missing slashes get normalized.
	EXPECT_EQ(launcher_patch::BuildPatchUrl("patch.example.com/", "patches/", "/p1300.zip"),
		"http://patch.example.com/patches/p1300.zip");

	// Empty path.
	EXPECT_EQ(launcher_patch::BuildPatchUrl("patch.example.com", "", "p.zip"),
		"http://patch.example.com/p.zip");
}

TEST(LauncherPatchUrl, PreservesExplicitHttpsBase)
{
	EXPECT_EQ(launcher_patch::BuildPatchUrl("https://cdn.example.com", "/dir", "p.zip"),
		"https://cdn.example.com/dir/p.zip");
	EXPECT_EQ(launcher_patch::BuildPatchUrl("https://cdn.example.com/base/", "sub", "p.zip"),
		"https://cdn.example.com/base/sub/p.zip");
}

// --- ZIP extraction ---------------------------------------------------------

namespace
{
// Builds a zip in memory containing the given (name -> content) entries.
std::vector<uint8_t> MakeZip(const std::vector<std::pair<std::string, std::string>>& entries)
{
	mz_zip_archive zip;
	std::memset(&zip, 0, sizeof(zip));
	mz_zip_writer_init_heap(&zip, 0, 0);
	for (const auto& [name, content] : entries)
	{
		mz_zip_writer_add_mem(
			&zip, name.c_str(), content.data(), content.size(), MZ_DEFAULT_COMPRESSION);
	}
	void* buf     = nullptr;
	size_t bufLen = 0;
	mz_zip_writer_finalize_heap_archive(&zip, &buf, &bufLen);
	std::vector<uint8_t> out(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + bufLen);
	mz_zip_writer_end(&zip);
	mz_free(buf);
	return out;
}

class LauncherPatchZipTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		dir = fs::temp_directory_path() / "openko-launcherpatch-test";
		std::error_code ec;
		fs::remove_all(dir, ec);
		fs::create_directories(dir, ec);
	}
	void TearDown() override
	{
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
	fs::path dir;
};
} // namespace

TEST_F(LauncherPatchZipTest, ExtractsNestedEntries)
{
	const std::vector<uint8_t> zip = MakeZip({
		{ "readme.txt", "hello patch" },
		{ "Data/sub/file.bin", "binary\0data" },
	});
	const fs::path zipPath = dir / "patch.zip";
	std::ofstream(zipPath, std::ios::binary)
		.write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));

	const fs::path dest = dir / "out";
	std::string error;
	ASSERT_TRUE(launcher_patch::ExtractZip(zipPath.string(), dest.string(), error)) << error;

	std::ifstream f1(dest / "readme.txt");
	std::string c1((std::istreambuf_iterator<char>(f1)), std::istreambuf_iterator<char>());
	EXPECT_EQ(c1, "hello patch");

	EXPECT_TRUE(fs::exists(dest / "Data" / "sub" / "file.bin"));
}

TEST_F(LauncherPatchZipTest, RejectsZipSlip)
{
	// An entry that tries to escape the destination with '..'.
	const std::vector<uint8_t> zip = MakeZip({ { "../evil.txt", "pwned" } });
	const fs::path zipPath = dir / "evil.zip";
	std::ofstream(zipPath, std::ios::binary)
		.write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));

	const fs::path dest = dir / "out";
	std::string error;
	EXPECT_FALSE(launcher_patch::ExtractZip(zipPath.string(), dest.string(), error));
	EXPECT_FALSE(fs::exists(dir / "evil.txt"));
}

// --- HTTP download (against a throwaway local server) -----------------------

namespace
{
// A one-shot HTTP/1.1 server that serves a fixed body to a single client on
// an ephemeral port, then returns. Runs on its own thread.
class OneShotHttpServer
{
public:
	explicit OneShotHttpServer(std::string body) : m_body(std::move(body))
	{
		m_acceptor = std::make_unique<asio::ip::tcp::acceptor>(
			m_io, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
		m_port     = m_acceptor->local_endpoint().port();
		m_thread   = std::thread([this] { Run(); });
	}

	~OneShotHttpServer()
	{
		if (m_thread.joinable())
			m_thread.join();
	}

	uint16_t Port() const
	{
		return m_port;
	}

private:
	void Run()
	{
		asio::error_code ec;
		asio::ip::tcp::socket socket(m_io);
		m_acceptor->accept(socket, ec);
		if (ec)
			return;

		// Drain the request (we don't parse it - single fixed response).
		asio::streambuf req;
		asio::read_until(socket, req, "\r\n\r\n", ec);

		std::string response = "HTTP/1.1 200 OK\r\nContent-Length: "
							   + std::to_string(m_body.size()) + "\r\nConnection: close\r\n\r\n"
							   + m_body;
		asio::write(socket, asio::buffer(response), ec);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	}

	asio::io_context m_io;
	std::unique_ptr<asio::ip::tcp::acceptor> m_acceptor;
	std::thread m_thread;
	std::string m_body;
	uint16_t m_port = 0;
};
} // namespace

TEST_F(LauncherPatchZipTest, DownloadsOverHttp)
{
	const std::string body(200000, 'Z'); // a couple hundred KB, multi-chunk
	OneShotHttpServer server(body);

	const std::string url =
		"http://127.0.0.1:" + std::to_string(server.Port()) + "/patch.zip";
	const fs::path dest = dir / "downloaded.bin";

	std::atomic<uint64_t> lastReceived{ 0 };
	std::string error;
	const bool ok = launcher_patch::DownloadFile(url, dest.string(),
		[&](uint64_t received, uint64_t total) {
			lastReceived = received;
			EXPECT_LE(received, total == 0 ? received : total);
			return true;
		},
		error);

	ASSERT_TRUE(ok) << error;
	EXPECT_EQ(lastReceived.load(), body.size());

	std::ifstream f(dest, std::ios::binary);
	std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	EXPECT_EQ(got.size(), body.size());
	EXPECT_EQ(got, body);
}

TEST_F(LauncherPatchZipTest, DownloadThenExtractRoundTrips)
{
	// End-to-end: a real zip served over HTTP, downloaded, then extracted.
	const std::vector<uint8_t> zip = MakeZip({ { "patched.txt", "new content 1300" } });
	OneShotHttpServer server(std::string(zip.begin(), zip.end()));

	const std::string url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/p.zip";
	const fs::path zipDst = dir / "p.zip";
	std::string error;
	ASSERT_TRUE(launcher_patch::DownloadFile(url, zipDst.string(), nullptr, error)) << error;

	const fs::path outDir = dir / "installed";
	ASSERT_TRUE(launcher_patch::ExtractZip(zipDst.string(), outDir.string(), error)) << error;

	std::ifstream f(outDir / "patched.txt");
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	EXPECT_EQ(content, "new content 1300");
}

TEST(LauncherPatchDownload, RejectsBadUrl)
{
	std::string error;
	EXPECT_FALSE(launcher_patch::DownloadFile("not-a-url", "/tmp/x", nullptr, error));
	EXPECT_FALSE(error.empty());
}
