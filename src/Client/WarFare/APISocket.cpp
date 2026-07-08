// APISocket.cpp: implementation of the CAPISocket class.
//
// TCP transport over standalone Asio (the same stack the servers use):
// a blocking connect followed by non-blocking reads drained once per game
// tick via Poll(). This replaces the old Winsock WSAAsyncSelect model, which
// coupled socket notifications to the Win32 window message pump
// (WM_SOCKETMSG) and therefore only existed on Windows.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "APISocket.h"

#include <asio.hpp>

#ifdef _N3GAME
#include <N3Base/LogWriter.h>
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

int CAPISocket::s_nInstanceCount = 0;

#ifdef _CRYPTION
BOOL CAPISocket::s_bCryptionFlag = FALSE; //0 : 비암호화 , 1 : 암호화
CJvCryption CAPISocket::s_JvCrypt;
uint32_t CAPISocket::s_wSendVal = 0;
uint32_t CAPISocket::s_wRcvVal  = 0;
#endif

const uint16_t PACKET_HEADER = 0XAA55;
const uint16_t PACKET_TAIL   = 0X55AA;

// Byte-order helpers matching the legacy htons/ntohs usage without pulling
// the platform socket headers in.
static uint16_t SwapBytes16(uint16_t value)
{
	return static_cast<uint16_t>((value << 8) | (value >> 8));
}

class CAPISocketTransport
{
public:
	asio::io_context IoContext;
	asio::ip::tcp::socket Socket {IoContext};
};

CAPISocket::CAPISocket() : m_SendBuf(SEND_BUF_SIZE), m_RecvBuf(RECV_BUF_SIZE), m_CB(RECV_BUF_SIZE)
{
	m_pTransport      = std::make_unique<CAPISocketTransport>();
	m_hWndTarget      = nullptr;
	m_dwPort          = 0;

	++s_nInstanceCount;

	m_iSendByteCount  = 0;
	m_bConnected      = FALSE;
	m_bConnectionLost = FALSE;
	m_bEnableSend     = TRUE; // 보내기 가능..?

	memset(m_SendBuf.data(), 0, m_SendBuf.size());
	memset(m_RecvBuf.data(), 0, m_RecvBuf.size());
}

CAPISocket::~CAPISocket()
{
	Release();

	--s_nInstanceCount;
}

void CAPISocket::Release()
{
	Disconnect();

	while (!m_qRecvPkt.empty())
	{
		delete m_qRecvPkt.front();
		m_qRecvPkt.pop();
	}

	m_iSendByteCount = 0;
}

void CAPISocket::Disconnect()
{
	asio::error_code ec;
	if (m_pTransport->Socket.is_open())
	{
		m_pTransport->Socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		m_pTransport->Socket.close(ec);
	}

	m_hWndTarget = nullptr;
	m_szIP.clear();
	m_dwPort          = 0;

	m_bConnected      = FALSE;
	m_bConnectionLost = FALSE;
	m_bEnableSend     = TRUE; // 보내기 가능..?

#ifdef _CRYPTION
	InitCrypt(0);             // 암호화 해제..
#endif                        // #ifdef _CRYPTION
}

int CAPISocket::Connect(HWND hWnd, const std::string& szIP, uint32_t dwPort)
{
	if (szIP.empty() || dwPort == 0)
		return -1;

	if (m_pTransport->Socket.is_open())
		Disconnect();

	asio::error_code ec;

	// The resolver covers both dotted IPs and hostnames (formerly the
	// inet_addr/gethostbyname split).
	asio::ip::tcp::resolver resolver(m_pTransport->IoContext);
	const auto endpoints = resolver.resolve(szIP, std::to_string(dwPort), ec);
	if (ec)
	{
#ifdef _N3GAME
		CLogWriter::Write("socket resolve error! ({}): {}", szIP, ec.message());
#endif
		return ec.value() != 0 ? ec.value() : -1;
	}

	asio::connect(m_pTransport->Socket, endpoints, ec);
	if (ec)
	{
		asio::error_code ignored;
		m_pTransport->Socket.close(ignored);
		return ec.value() != 0 ? ec.value() : -1;
	}

	asio::error_code optionError;
	m_pTransport->Socket.set_option(asio::socket_base::receive_buffer_size(RECV_BUF_SIZE), optionError);

	// Reads are drained from the game loop (Poll), so the socket must never
	// block there.
	m_pTransport->Socket.non_blocking(true, ec);
	if (ec)
	{
		asio::error_code ignored;
		m_pTransport->Socket.close(ignored);
		return ec.value() != 0 ? ec.value() : -1;
	}

	m_hWndTarget      = hWnd;
	m_szIP            = szIP;
	m_dwPort          = dwPort;
	m_bConnected      = TRUE;
	m_bConnectionLost = FALSE;

	return 0;
}

int CAPISocket::ReConnect()
{
	return Connect(m_hWndTarget, m_szIP, m_dwPort);
}

BOOL CAPISocket::Poll()
{
	if (m_bConnected)
		Receive();

	if (m_bConnectionLost)
	{
		m_bConnectionLost = FALSE;
		return FALSE;
	}

	return TRUE;
}

void CAPISocket::Receive()
{
	if (FALSE == m_bConnected || !m_pTransport->Socket.is_open())
		return;

	// Drain everything currently readable.
	for (;;)
	{
		asio::error_code ec;
		const size_t count =
			m_pTransport->Socket.read_some(asio::buffer(m_RecvBuf.data(), m_RecvBuf.size()), ec);

		if (count > 0)
			m_CB.PutData(m_RecvBuf.data(), static_cast<int>(count));

		if (ec)
		{
			if (ec == asio::error::would_block || ec == asio::error::try_again)
				break; // nothing more to read right now

			// Graceful close or hard error: flag it for Poll()'s caller.
			if (ec != asio::error::eof)
			{
#ifdef _N3GAME
				CLogWriter::Write("socket receive error! : {}", ec.message());
#endif
			}

			m_bConnected      = FALSE;
			m_bConnectionLost = TRUE;
			break;
		}
	}

	// packet analysis.
	while (ReceiveProcess())
		;
}

BOOL CAPISocket::ReceiveProcess()
{
	int iCount      = m_CB.GetValidCount();
	BOOL bFoundTail = FALSE;
	if (iCount >= 7)
	{
		std::vector<uint8_t> data(iCount);
		m_CB.GetData(reinterpret_cast<char*>(data.data()), iCount);

		if (PACKET_HEADER == SwapBytes16(*reinterpret_cast<uint16_t*>(&data[0])))
		{
			int16_t siCore = *reinterpret_cast<int16_t*>(&data[2]);
			if (siCore <= iCount)
			{
				// 패킷 꼬리 부분 검사..
				if (PACKET_TAIL == SwapBytes16(*reinterpret_cast<uint16_t*>(&data[iCount - 2])))
				{
					Packet* pkt = new Packet();
					if (s_bCryptionFlag)
					{
						// NOTE: Decrypts in-place
						s_JvCrypt.JvDecryptionFast(siCore, &data[4], &data[4]);

						uint16_t sig = *reinterpret_cast<uint16_t*>(&data[4]);

						if (sig != 0x1EFC)
						{
							delete pkt;
							__ASSERT(0, "Crypt Error");
							return FALSE;
						}

						// uint16_t sequence = *(uint16_t*) &&data[6];
						// uint8_t empty     = &data[8];
						pkt->append(&data[9], siCore - 5);
					}
					else
					{
						pkt->append(&data[4], siCore);
					}

					m_qRecvPkt.push(pkt);
					m_CB.HeadIncrease(siCore + 6); // 환형 버퍼 인덱스 증가 시키기..
					bFoundTail = TRUE;
				}
			}
		}
		else
		{
			// 패킷이 깨졌다??
			__ASSERT(0, "broken packet header.. skip!");
			m_CB.HeadIncrease(iCount); // 환형 버퍼 인덱스 증가 시키기..
		}
	}

	return bFoundTail;
}

void CAPISocket::Send(uint8_t* pData, int nSize)
{
	if (!m_bEnableSend)
		return; // 보내기 가능..?
	if (FALSE == m_bConnected || !m_pTransport->Socket.is_open())
		return;

#ifdef _CRYPTION
	DataPack DP;

	if (s_bCryptionFlag)
	{
		static uint8_t pTBuf[SEND_BUF_SIZE];

		++s_wSendVal;

		memcpy(pTBuf, &s_wSendVal, sizeof(uint32_t));
		memcpy((pTBuf + 4), pData, nSize);

		*((uint32_t*) (pTBuf + (nSize + 4))) = crc32(pTBuf, (nSize + 4), -1);

		s_JvCrypt.JvEncryptionFast((nSize + 4 + 4), pTBuf, pTBuf);

		DP.m_Size  = (nSize + 4 + 4);
		DP.m_pData = new uint8_t[DP.m_Size];
		memcpy(DP.m_pData, pTBuf, DP.m_Size);

		nSize = DP.m_Size;
		pData = DP.m_pData;
	}
#endif

	const size_t nTotalSize   = static_cast<size_t>(nSize) + 6;
	char* pSendData           = m_SendBuf.data();
	*((uint16_t*) pSendData)  = SwapBytes16(PACKET_HEADER);
	pSendData                += 2;
	*((uint16_t*) pSendData)  = static_cast<uint16_t>(nSize);
	pSendData                += 2;
	memcpy(pSendData, pData, nSize);
	pSendData                += nSize;
	*((uint16_t*) pSendData)  = SwapBytes16(PACKET_TAIL);
	// pSendData             += 2;

	size_t nSent              = 0;
	while (nSent < nTotalSize)
	{
		asio::error_code ec;
		nSent += m_pTransport->Socket.write_some(
			asio::buffer(m_SendBuf.data() + nSent, nTotalSize - nSent), ec);

		if (ec == asio::error::would_block || ec == asio::error::try_again)
		{
			// Outgoing packets are small; wait for writability and continue.
			asio::error_code waitError;
			m_pTransport->Socket.wait(asio::ip::tcp::socket::wait_write, waitError);
			continue;
		}

		if (ec)
		{
			__ASSERT(0, "socket send error!");
#ifdef _N3GAME
			CLogWriter::Write("socket send error! : {}", ec.message());
#endif
			// The disconnect is reported through the next Poll(), which runs
			// the caller's connection-closed handling (formerly this posted
			// WM_QUIT directly).
			m_bConnected      = FALSE;
			m_bConnectionLost = TRUE;
			break;
		}
	}

	m_iSendByteCount += static_cast<int>(nTotalSize);
}
