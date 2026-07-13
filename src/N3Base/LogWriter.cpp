// LogWriter.cpp: implementation of the CLogWriter class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfxBase.h"
#include "N3Base.h"
#include "LogWriter.h"
#include <ctime>

namespace
{
// Portable replacement for GetLocalTime(): month/day/wall-clock parts.
std::tm LocalTimeNow()
{
	const std::time_t now = std::time(nullptr);
	std::tm parts {};
#ifdef _WIN32
	localtime_s(&parts, &now);
#else
	localtime_r(&now, &parts);
#endif
	return parts;
}
} // namespace

#include <FileIO/FileWriter.h>

std::string CLogWriter::s_szFileName;

CLogWriter::CLogWriter()
{
}

CLogWriter::~CLogWriter()
{
}

void CLogWriter::Open(const std::string& szFN)
{
	if (szFN.empty())
		return;

	s_szFileName = szFN;

	FileWriter file;
	if (!file.OpenExisting(s_szFileName))
	{
		if (!file.Create(s_szFileName))
			return;
	}

	auto fileSize = file.Size();

	// 파일 사이즈가 너무 크면 지운다..
	if (fileSize > 256'000)
	{
		file.Close();

		std::error_code ec;
		std::filesystem::remove(s_szFileName, ec);

		if (!file.Create(s_szFileName))
			return;
	}

	file.Seek(0, SEEK_END); // 추가 하기 위해서 파일의 끝으로 옮기고..

	std::string buff;
	const std::tm time = LocalTimeNow();

	buff = "---------------------------------------------------------------------------\r\n";
	file.Write(buff.data(), buff.length());

	buff = fmt::format("// Begin writing log... [{:02}/{:02} {:02}:{:02}]\r\n", time.tm_mon + 1,
		time.tm_mday, time.tm_hour, time.tm_min);
	file.Write(buff.data(), buff.length());
}

void CLogWriter::Close()
{
	FileWriter file;
	if (!file.OpenExisting(s_szFileName))
	{
		if (!file.Create(s_szFileName))
			return;
	}

	file.Seek(0, SEEK_END); // 추가 하기 위해서 파일의 끝으로 옮기고..

	std::string buff;
	const std::tm time = LocalTimeNow();

	buff = fmt::format("// End writing log... [{:02}/{:02} {:02}:{:02}]\r\n", time.tm_mon + 1,
		time.tm_mday, time.tm_hour, time.tm_min);
	file.Write(buff.data(), buff.length());

	buff = "---------------------------------------------------------------------------\r\n";
	file.Write(buff.data(), buff.length());
}

void CLogWriter::Write(const std::string_view message)
{
	if (s_szFileName.empty() || message.empty())
		return;

	FileWriter file;
	if (!file.OpenExisting(s_szFileName))
	{
		if (!file.Create(s_szFileName))
			return;
	}

	const std::tm time        = LocalTimeNow();

	std::string outputMessage = fmt::format(
		"    [{:02}:{:02}:{:02}] {}\r\n", time.tm_hour, time.tm_min, time.tm_sec, message);

	file.Seek(0, SEEK_END); // 추가 하기 위해서 파일의 끝으로 옮기고..
	file.Write(outputMessage.data(), outputMessage.length());
}
