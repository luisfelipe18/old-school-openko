#include "LauncherPatch.h"

#include <miniz.h>

#include <asio.hpp>
#if defined(LAUNCHER_HAVE_TLS)
#include <asio/ssl.hpp>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace launcher_patch
{
namespace
{
std::string ToLower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool StartsWith(const std::string& s, const std::string& prefix)
{
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Strips one leading and one trailing '/' so pieces join with exactly one.
std::string TrimSlashes(const std::string& s)
{
	size_t begin = 0, end = s.size();
	while (begin < end && s[begin] == '/')
		++begin;
	while (end > begin && s[end - 1] == '/')
		--end;
	return s.substr(begin, end - begin);
}
} // namespace

ParsedUrl ParseUrl(const std::string& url)
{
	ParsedUrl out;

	std::string rest;
	if (StartsWith(ToLower(url), "http://"))
	{
		out.scheme = "http";
		out.port   = "80";
		rest       = url.substr(7);
	}
	else if (StartsWith(ToLower(url), "https://"))
	{
		out.scheme = "https";
		out.port   = "443";
		rest       = url.substr(8);
	}
	else
	{
		return out; // not an absolute http(s) URL
	}

	// rest = host[:port][/target]
	const size_t slash = rest.find('/');
	std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
	out.target = (slash == std::string::npos) ? "/" : rest.substr(slash);

	const size_t colon = authority.find(':');
	if (colon == std::string::npos)
	{
		out.host = authority;
	}
	else
	{
		out.host = authority.substr(0, colon);
		out.port = authority.substr(colon + 1);
	}

	out.valid = !out.host.empty();
	return out;
}

std::string BuildPatchUrl(
	const std::string& base, const std::string& path, const std::string& fileName)
{
	// The base carries the scheme + host (and possibly a leading path). If the
	// admin gave a bare host, default to http:// - the protocol field predates
	// HTTPS. A trailing path on the base is kept.
	std::string prefix;
	const std::string lowerBase = ToLower(base);
	if (StartsWith(lowerBase, "http://") || StartsWith(lowerBase, "https://"))
		prefix = base;
	else
		prefix = "http://" + base;

	// Strip only trailing slashes from the prefix (keeping the "scheme://"),
	// then join path and file with single slashes.
	while (!prefix.empty() && prefix.back() == '/')
		prefix.pop_back();

	std::string url = prefix;
	const std::string trimmedPath = TrimSlashes(path);
	if (!trimmedPath.empty())
		url += "/" + trimmedPath;

	const std::string trimmedFile = TrimSlashes(fileName);
	if (!trimmedFile.empty())
		url += "/" + trimmedFile;

	return url;
}

bool ExtractZip(const std::string& zipPath, const std::string& destDir, std::string& error)
{
	mz_zip_archive zip;
	std::memset(&zip, 0, sizeof(zip));

	if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0))
	{
		error = "could not open zip archive '" + zipPath + "'";
		return false;
	}

	std::error_code ec;
	const fs::path destRoot = fs::weakly_canonical(fs::path(destDir), ec);
	if (ec)
	{
		mz_zip_reader_end(&zip);
		error = "invalid destination '" + destDir + "'";
		return false;
	}
	fs::create_directories(destRoot, ec);

	const mz_uint count = mz_zip_reader_get_num_files(&zip);
	bool ok             = true;

	for (mz_uint i = 0; i < count && ok; ++i)
	{
		mz_zip_archive_file_stat stat;
		if (!mz_zip_reader_file_stat(&zip, i, &stat))
		{
			error = "corrupt zip entry";
			ok    = false;
			break;
		}

		// Reject absolute paths and any entry that would escape destRoot
		// (zip-slip). weakly_canonical resolves ".." before the check.
		const fs::path entryPath =
			fs::weakly_canonical(destRoot / fs::path(stat.m_filename), ec);
		if (ec
			|| std::mismatch(destRoot.begin(), destRoot.end(), entryPath.begin()).first
				   != destRoot.end())
		{
			error = std::string("unsafe zip entry path '") + stat.m_filename + "'";
			ok    = false;
			break;
		}

		if (mz_zip_reader_is_file_a_directory(&zip, i))
		{
			fs::create_directories(entryPath, ec);
			continue;
		}

		fs::create_directories(entryPath.parent_path(), ec);
		if (!mz_zip_reader_extract_to_file(&zip, i, entryPath.string().c_str(), 0))
		{
			error = std::string("failed to extract '") + stat.m_filename + "'";
			ok    = false;
			break;
		}
	}

	mz_zip_reader_end(&zip);
	return ok;
}

namespace
{
// Parses "HTTP/1.1 200 OK" -> 200 and finds Content-Length / Location.
struct HttpResponseHead
{
	int statusCode = 0;
	uint64_t contentLength = 0;
	bool hasContentLength  = false;
	std::string location; // for redirects
};

HttpResponseHead ParseResponseHead(const std::string& head)
{
	HttpResponseHead out;
	std::istringstream stream(head);
	std::string line;

	if (std::getline(stream, line))
	{
		const size_t sp = line.find(' ');
		if (sp != std::string::npos)
			out.statusCode = std::atoi(line.c_str() + sp + 1);
	}

	while (std::getline(stream, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string key = ToLower(line.substr(0, colon));
		std::string val = line.substr(colon + 1);
		while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
			val.erase(val.begin());

		if (key == "content-length")
		{
			out.contentLength    = std::strtoull(val.c_str(), nullptr, 10);
			out.hasContentLength = true;
		}
		else if (key == "location")
		{
			out.location = val;
		}
	}
	return out;
}

// Streams the body of an already-sent GET from `stream` into `out`, invoking
// progress. SyncStream is either a plain tcp::socket or an ssl::stream.
template <typename SyncStream>
bool ReceiveBody(SyncStream& stream, const std::string& destPath,
	const ProgressCallback& progress, HttpResponseHead& head, std::string& error)
{
	asio::streambuf buf;
	asio::error_code ec;

	// Read up to and including the header terminator.
	asio::read_until(stream, buf, "\r\n\r\n", ec);
	if (ec)
	{
		error = "reading response header: " + ec.message();
		return false;
	}

	const size_t size = buf.size();
	std::string all(asio::buffers_begin(buf.data()), asio::buffers_end(buf.data()));
	const size_t headerEnd = all.find("\r\n\r\n");
	const std::string headStr = all.substr(0, headerEnd);
	head = ParseResponseHead(headStr);

	if (head.statusCode >= 300 && head.statusCode < 400 && !head.location.empty())
		return true; // caller handles the redirect; nothing written yet

	if (head.statusCode != 200)
	{
		error = "server returned HTTP " + std::to_string(head.statusCode);
		return false;
	}

	std::ofstream file(destPath, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		error = "cannot open '" + destPath + "' for writing";
		return false;
	}

	// Body bytes already buffered past the header.
	uint64_t received = 0;
	const size_t bodyStart = headerEnd + 4;
	if (size > bodyStart)
	{
		file.write(all.data() + bodyStart, static_cast<std::streamsize>(size - bodyStart));
		received += size - bodyStart;
	}

	if (progress && !progress(received, head.contentLength))
	{
		error = "download cancelled";
		return false;
	}

	std::array<char, 64 * 1024> chunk;
	while (true)
	{
		const size_t n = stream.read_some(asio::buffer(chunk), ec);
		if (n > 0)
		{
			file.write(chunk.data(), static_cast<std::streamsize>(n));
			received += n;
			if (progress && !progress(received, head.contentLength))
			{
				error = "download cancelled";
				return false;
			}
		}
		if (ec == asio::error::eof)
			break;
		if (ec)
		{
			// A clean TLS/socket close mid-stream after all bytes is fine
			// when the length was satisfied.
			if (head.hasContentLength && received >= head.contentLength)
				break;
			error = "reading body: " + ec.message();
			return false;
		}
	}

	if (head.hasContentLength && received < head.contentLength)
	{
		error = "truncated download (" + std::to_string(received) + "/"
				+ std::to_string(head.contentLength) + " bytes)";
		return false;
	}
	return true;
}

std::string BuildRequest(const ParsedUrl& url)
{
	return "GET " + url.target + " HTTP/1.1\r\n" + "Host: " + url.host + "\r\n"
		   + "User-Agent: OpenKO-Launcher\r\n" + "Accept: */*\r\n" + "Connection: close\r\n\r\n";
}

bool DownloadOnce(const ParsedUrl& url, const std::string& destPath,
	const ProgressCallback& progress, HttpResponseHead& head, std::string& error)
{
	try
	{
		asio::io_context io;
		asio::ip::tcp::resolver resolver(io);
		const auto endpoints = resolver.resolve(url.host, url.port);

		if (url.scheme == "https")
		{
#if defined(LAUNCHER_HAVE_TLS)
			asio::ssl::context ctx(asio::ssl::context::tls_client);
			ctx.set_default_verify_paths();
			asio::ssl::stream<asio::ip::tcp::socket> stream(io, ctx);
			// SNI - many hosts require it.
			SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str());
			stream.set_verify_mode(asio::ssl::verify_peer);
			stream.set_verify_callback(asio::ssl::host_name_verification(url.host));
			asio::connect(stream.next_layer(), endpoints);
			stream.handshake(asio::ssl::stream_base::client);

			const std::string req = BuildRequest(url);
			asio::write(stream, asio::buffer(req));
			return ReceiveBody(stream, destPath, progress, head, error);
#else
			error = "HTTPS patch URLs require a build with OpenSSL (LAUNCHER_HAVE_TLS)";
			return false;
#endif
		}

		asio::ip::tcp::socket socket(io);
		asio::connect(socket, endpoints);
		const std::string req = BuildRequest(url);
		asio::write(socket, asio::buffer(req));
		return ReceiveBody(socket, destPath, progress, head, error);
	}
	catch (const std::exception& e)
	{
		error = e.what();
		return false;
	}
}
} // namespace

bool DownloadFile(const std::string& url, const std::string& destPath,
	const ProgressCallback& progress, std::string& error)
{
	ParsedUrl parsed = ParseUrl(url);
	if (!parsed.valid)
	{
		error = "invalid URL '" + url + "'";
		return false;
	}

	// Follow at most one redirect (http->https CDN hops are common).
	for (int hop = 0; hop < 2; ++hop)
	{
		HttpResponseHead head;
		if (!DownloadOnce(parsed, destPath, progress, head, error))
			return false;

		if (head.statusCode >= 300 && head.statusCode < 400 && !head.location.empty())
		{
			ParsedUrl next = ParseUrl(head.location);
			if (!next.valid)
			{
				error = "redirect to unsupported URL '" + head.location + "'";
				return false;
			}
			parsed = next;
			continue;
		}
		return true;
	}

	error = "too many redirects";
	return false;
}

} // namespace launcher_patch
