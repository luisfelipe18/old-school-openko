// Integration tests for CAPISocket's asio transport (POSIX port, phase 2).
//
// A miniature TCP server speaking the game's wire framing runs inside the
// test; CAPISocket connects to it and is pumped with Poll() exactly like
// CGameProcedure does per tick. Framing under test matches the live
// client<->Ebenezer protocol: AA 55 | size (LE int16) | payload | 55 AA,
// with the optional JvCryption layer on received packets.

#include <gtest/gtest.h>

#include <APISocket.h>

#include <shared/JvCryption.h>

#include <asio.hpp>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
constexpr std::chrono::seconds POLL_TIMEOUT {5};

std::vector<uint8_t> FramePacket(const std::vector<uint8_t>& payload)
{
	std::vector<uint8_t> frame;
	frame.reserve(payload.size() + 6);
	frame.push_back(0xAA);
	frame.push_back(0x55);

	const uint16_t size = static_cast<uint16_t>(payload.size());
	frame.push_back(static_cast<uint8_t>(size & 0xFF)); // little-endian size
	frame.push_back(static_cast<uint8_t>(size >> 8));

	frame.insert(frame.end(), payload.begin(), payload.end());

	frame.push_back(0x55);
	frame.push_back(0xAA);
	return frame;
}

// Pumps the client until predicate() is true or the timeout expires.
// Returns false when the connection dropped or the timeout was hit.
template <typename Predicate>
bool PollUntil(CAPISocket& socket, Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + POLL_TIMEOUT;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (!socket.Poll())
			return false;
		if (predicate())
			return true;

		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	return predicate();
}

class APISocketTest : public ::testing::Test
{
protected:
	asio::io_context _serverContext;
	std::unique_ptr<asio::ip::tcp::acceptor> _acceptor;
	asio::ip::tcp::socket _serverSide {_serverContext};
	std::thread _acceptThread;
	uint16_t _port = 0;

	CAPISocket _client;

	void SetUp() override
	{
		_acceptor = std::make_unique<asio::ip::tcp::acceptor>(
			_serverContext, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
		_port         = _acceptor->local_endpoint().port();

		_acceptThread = std::thread([this] {
			asio::error_code ec;
			_acceptor->accept(_serverSide, ec);
		});
	}

	void TearDown() override
	{
		asio::error_code ec;

		if (_acceptThread.joinable())
		{
			// A blocking accept() is not reliably interrupted by closing the
			// acceptor from another thread; connect a throwaway client to
			// wake it up when no test client ever did.
			asio::io_context dummyContext;
			asio::ip::tcp::socket dummy(dummyContext);
			dummy.connect(
				asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), _port), ec);
			_acceptThread.join();
			dummy.close(ec);
		}

		if (_acceptor != nullptr)
			_acceptor->close(ec);
		_serverSide.close(ec);

		CAPISocket::InitCrypt(0);
	}

	int ConnectClient()
	{
		const int result = _client.Connect(nullptr, "127.0.0.1", _port);
		_acceptThread.join();
		return result;
	}

	void ServerSend(const std::vector<uint8_t>& bytes)
	{
		asio::write(_serverSide, asio::buffer(bytes));
	}

	std::vector<uint8_t> ServerReadExactly(size_t count)
	{
		std::vector<uint8_t> bytes(count);
		asio::read(_serverSide, asio::buffer(bytes));
		return bytes;
	}
};
} // namespace

TEST_F(APISocketTest, ConnectToUnreachablePortFails)
{
	// Grab a port from a scratch acceptor and close it so nothing listens.
	asio::io_context scratchContext;
	asio::ip::tcp::acceptor scratch(
		scratchContext, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
	const uint16_t deadPort = scratch.local_endpoint().port();
	scratch.close();

	CAPISocket client;
	EXPECT_NE(client.Connect(nullptr, "127.0.0.1", deadPort), 0);
	EXPECT_FALSE(client.IsConnected());
}

TEST_F(APISocketTest, ReceivesPlainFramedPacket)
{
	ASSERT_EQ(ConnectClient(), 0);
	ASSERT_TRUE(_client.IsConnected());

	const std::vector<uint8_t> payload = {0x7E, 0x01, 0x02, 0x03, 0x04};
	ServerSend(FramePacket(payload));

	ASSERT_TRUE(PollUntil(_client, [this] { return !_client.m_qRecvPkt.empty(); }));

	Packet* pkt = _client.m_qRecvPkt.front();
	EXPECT_EQ(pkt->GetOpcode(), 0x7E);
	EXPECT_EQ(pkt->storage(), payload);

	delete pkt;
	_client.m_qRecvPkt.pop();
}

TEST_F(APISocketTest, SendProducesWellFormedFrame)
{
	ASSERT_EQ(ConnectClient(), 0);

	uint8_t buffer[16];
	int offset = 0;
	CAPISocket::MP_AddByte(buffer, offset, 0x2A);
	CAPISocket::MP_AddShort(buffer, offset, 1298);
	CAPISocket::MP_AddString(buffer, offset, "ko");
	_client.Send(buffer, offset);

	const std::vector<uint8_t> frame = ServerReadExactly(static_cast<size_t>(offset) + 6);

	EXPECT_EQ(frame[0], 0xAA);
	EXPECT_EQ(frame[1], 0x55);
	EXPECT_EQ(frame[2], static_cast<uint8_t>(offset)); // little-endian size
	EXPECT_EQ(frame[3], 0x00);
	EXPECT_EQ(std::memcmp(&frame[4], buffer, static_cast<size_t>(offset)), 0);
	EXPECT_EQ(frame[frame.size() - 2], 0x55);
	EXPECT_EQ(frame.back(), 0xAA);
}

TEST_F(APISocketTest, ReceivesJvCryptionEncryptedPacket)
{
	ASSERT_EQ(ConnectClient(), 0);

	// Both ends derive their stream from the same public key, exactly like
	// the live login handshake does.
	constexpr int64_t PUBLIC_KEY = 0x1234567890ABCDEFLL;
	CAPISocket::InitCrypt(PUBLIC_KEY);

	CJvCryption serverCryption;
	serverCryption.SetPublicKey(PUBLIC_KEY);
	serverCryption.Init();

	// Server-side plaintext layout: signature 0x1EFC, sequence, filler byte,
	// then the actual packet payload.
	const std::vector<uint8_t> payload = {0x50, 0xAB, 0xCD};
	std::vector<uint8_t> plain;
	plain.push_back(0xFC);
	plain.push_back(0x1E);
	plain.push_back(0x01);
	plain.push_back(0x00);
	plain.push_back(0x00);
	plain.insert(plain.end(), payload.begin(), payload.end());

	std::vector<uint8_t> encrypted(plain.size());
	serverCryption.JvEncryptionFast(
		static_cast<int>(plain.size()), plain.data(), encrypted.data());

	ServerSend(FramePacket(encrypted));

	ASSERT_TRUE(PollUntil(_client, [this] { return !_client.m_qRecvPkt.empty(); }));

	Packet* pkt = _client.m_qRecvPkt.front();
	EXPECT_EQ(pkt->GetOpcode(), 0x50);
	EXPECT_EQ(pkt->storage(), payload);

	delete pkt;
	_client.m_qRecvPkt.pop();
}

TEST_F(APISocketTest, PollReportsServerDisconnect)
{
	ASSERT_EQ(ConnectClient(), 0);
	ASSERT_TRUE(_client.Poll());

	asio::error_code ec;
	_serverSide.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	_serverSide.close(ec);

	const auto deadline = std::chrono::steady_clock::now() + POLL_TIMEOUT;
	bool sawDisconnect  = false;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (!_client.Poll())
		{
			sawDisconnect = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	EXPECT_TRUE(sawDisconnect);
	EXPECT_FALSE(_client.IsConnected());
}
