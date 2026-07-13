// NetworkEncoding.h - CP949 <-> UTF-8 conversion at the network boundary
// (docs/PORT_POSIX_PLAN.md, T7.3).
//
// The wire protocol carries strings as CP949 bytes (the codepage the original
// Korean client used). On Windows the client's internal representation is the
// same CP949, so these wrappers are no-ops. On POSIX the internal
// representation is UTF-8 (SDL text input, modern locale, FreeType), so bytes
// are translated when they cross the socket.
//
//   Receiving: pkt.readString(name, len);
//              name = NetToLocal(name);          // CP949 -> UTF-8 on POSIX
//
//   Sending:   const std::string wire = LocalToNet(szChat); // UTF-8 -> CP949
//              CAPISocket::MP_AddShort(buf, off, wire.size());
//              CAPISocket::MP_AddString(buf, off, wire);

#ifndef NETWORK_ENCODING_H
#define NETWORK_ENCODING_H

#include <string>

#ifdef _WIN32

// Windows: internal encoding == wire encoding (both CP949/ANSI). Pass-through.
inline const std::string& NetToLocal(const std::string& s)
{
	return s;
}
inline const std::string& LocalToNet(const std::string& s)
{
	return s;
}

#else

#include <Platform/PlatformEncoding.h>

// POSIX: internal encoding is UTF-8; wire encoding is CP949.
inline std::string NetToLocal(const std::string& s)
{
	return Cp949ToUtf8(s);
}
inline std::string LocalToNet(const std::string& s)
{
	return Utf8ToCp949(s);
}

#endif // _WIN32

#endif // NETWORK_ENCODING_H
