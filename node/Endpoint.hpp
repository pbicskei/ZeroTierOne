/*
 * Copyright (c)2013-2020 ZeroTier, Inc.
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file in the project's root directory.
 *
 * Change Date: 2024-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2.0 of the Apache License.
 */
/****/

#ifndef ZT_ENDPOINT_HPP
#define ZT_ENDPOINT_HPP

#include "Constants.hpp"
#include "InetAddress.hpp"
#include "Address.hpp"
#include "Utils.hpp"
#include "TriviallyCopyable.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

// max name size + type byte + port (for DNS name/port) + 3x 16-bit coordinate for location
#define ZT_ENDPOINT_MARSHAL_SIZE_MAX (ZT_ENDPOINT_MAX_NAME_SIZE+1+2+2+2+2)

namespace ZeroTier {

/**
 * Endpoint variant specifying some form of network endpoint
 *
 * This data structure supports a number of types that are not yet actually used:
 * DNSNAME, URL, and ETHERNET. These are present to reserve them for future use.
 */
class Endpoint : public TriviallyCopyable
{
public:
	/**
	 * Endpoint type
	 */
	enum Type
	{
		TYPE_NIL =          0,          // NIL value
		TYPE_ZEROTIER =     1,          // ZeroTier Address (for relaying and meshy behavior)
		TYPE_DNSNAME =      2,          // DNS name and port that resolves to InetAddress
		TYPE_URL =          3,          // URL for HTTP or Web Sockets transport
		TYPE_INETADDR_V4 =  4,          // IPv4
		TYPE_ETHERNET =     5,          // 48-bit LAN-local Ethernet address
		TYPE_INETADDR_V6 =  6           // IPv6
	};

	ZT_ALWAYS_INLINE Endpoint() noexcept { memoryZero(this); }

	ZT_ALWAYS_INLINE Endpoint(const InetAddress &sa)
	{
		switch (sa.ss_family) {
			case AF_INET:
				_t = TYPE_INETADDR_V4;
				break;
			case AF_INET6:
				_t = TYPE_INETADDR_V6;
			default:
				_t = TYPE_NIL;
				return;
		}
		asInetAddress(_v.sa) = sa;
	}

	ZT_ALWAYS_INLINE Endpoint(const Address &zt,const uint8_t identityHash[ZT_IDENTITY_HASH_SIZE]) :
		_t(TYPE_ZEROTIER)
	{
		_v.zt.a = zt.toInt();
		memcpy(_v.zt.idh,identityHash,ZT_IDENTITY_HASH_SIZE);
	}

	ZT_ALWAYS_INLINE Endpoint(const char *name,const int port) :
		_t(TYPE_DNSNAME)
	{
		_v.dns.port = port;
		Utils::scopy(_v.dns.name,sizeof(_v.dns.name),name);
	}

	explicit ZT_ALWAYS_INLINE Endpoint(const char *url) :
		_t(TYPE_URL)
	{ Utils::scopy(_v.url,sizeof(_v.url),url); }

	/**
	 * @return InetAddress or NIL if not of this type
	 */
	ZT_ALWAYS_INLINE const InetAddress &inetAddr() const noexcept { return ((_t == TYPE_INETADDR_V4)||(_t == TYPE_INETADDR_V6)) ? asInetAddress(_v.sa) : InetAddress::NIL; }

	/**
	 * @return DNS name or empty string if not of this type
	 */
	ZT_ALWAYS_INLINE const char *dnsName() const noexcept { return (_t == TYPE_DNSNAME) ? _v.dns.name : ""; }

	/**
	 * @return Port associated with DNS name or -1 if not of this type
	 */
	ZT_ALWAYS_INLINE int dnsPort() const noexcept { return (_t == TYPE_DNSNAME) ? _v.dns.port : -1; }

	/**
	 * @return ZeroTier address or NIL if not of this type
	 */
	ZT_ALWAYS_INLINE Address ztAddress() const noexcept { return Address((_t == TYPE_ZEROTIER) ? _v.zt.a : (uint64_t)0); }

	/**
	 * @return 384-bit hash of identity keys or NULL if not of this type
	 */
	ZT_ALWAYS_INLINE const uint8_t *ztIdentityHash() const noexcept { return (_t == TYPE_ZEROTIER) ? _v.zt.idh : nullptr; }

	/**
	 * @return URL or empty string if not of this type
	 */
	ZT_ALWAYS_INLINE const char *url() const noexcept { return (_t == TYPE_URL) ? _v.url : ""; }

	/**
	 * @return Ethernet address or NIL if not of this type
	 */
	ZT_ALWAYS_INLINE MAC ethernet() const noexcept { return (_t == TYPE_ETHERNET) ? MAC(_v.eth) : MAC(); }

	/**
	 * @return Endpoint type or NIL if unset/empty
	 */
	ZT_ALWAYS_INLINE Type type() const noexcept { return _t; }

	explicit ZT_ALWAYS_INLINE operator bool() const noexcept { return _t != TYPE_NIL; }

	bool operator==(const Endpoint &ep) const;
	ZT_ALWAYS_INLINE bool operator!=(const Endpoint &ep) const { return (!(*this == ep)); }
	bool operator<(const Endpoint &ep) const;
	ZT_ALWAYS_INLINE bool operator>(const Endpoint &ep) const { return (ep < *this); }
	ZT_ALWAYS_INLINE bool operator<=(const Endpoint &ep) const { return !(ep < *this); }
	ZT_ALWAYS_INLINE bool operator>=(const Endpoint &ep) const { return !(*this < ep); }

	static constexpr int marshalSizeMax() noexcept { return ZT_ENDPOINT_MARSHAL_SIZE_MAX; }
	int marshal(uint8_t data[ZT_ENDPOINT_MARSHAL_SIZE_MAX]) const noexcept;
	int unmarshal(const uint8_t *restrict data,int len) noexcept;

private:
	Type _t;
	int _l[3]; // X,Y,Z location in kilometers from the nearest gravitational center of mass
	union {
		struct sockaddr_storage sa;
		struct {
			uint16_t port;
			char name[ZT_ENDPOINT_MAX_NAME_SIZE];
		} dns;
		struct {
			uint64_t a;
			uint8_t idh[ZT_IDENTITY_HASH_SIZE];
		} zt;
		char url[ZT_ENDPOINT_MAX_NAME_SIZE];
		uint64_t eth;
	} _v;
};

} // namespace ZeroTier

#endif
