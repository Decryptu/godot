/**************************************************************************/
/*  ip_nx.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#if defined(UNIX_ENABLED) || defined(NX_ENABLED)

#include "ip_nx.h"

#include <netdb.h>

#ifdef ANDROID_ENABLED
// We could drop this file once we up our API level to 24,
// where the NDK's ifaddrs.h supports to needed getifaddrs.
#include <thirdparty/misc/ifaddrs-android.h>
#elif defined(NX_ENABLED)
#include <unistd.h>
#else
#ifdef __FreeBSD__
#include <sys/types.h>
#endif
#include <ifaddrs.h>
#endif

#include <arpa/inet.h>
#include <sys/socket.h>

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

#include <net/if.h> // Order is important on OpenBSD, leave as last.

static IPAddress _sockaddr2ip(struct sockaddr *p_addr) {
	IPAddress ip;

	if (p_addr->sa_family == AF_INET) {
		struct sockaddr_in *addr = (struct sockaddr_in *)p_addr;
		ip.set_ipv4((uint8_t *)&(addr->sin_addr));
	} else if (p_addr->sa_family == AF_INET6) {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)p_addr;
		ip.set_ipv6(addr6->sin6_addr.s6_addr);
	}

	return ip;
}

void IPNX::_resolve_hostname(List<IPAddress> &r_addresses, const String &p_hostname, Type p_type) const {
	struct addrinfo hints;
	struct addrinfo *result = nullptr;

	memset(&hints, 0, sizeof(struct addrinfo));
	if (p_type == TYPE_IPV4) {
		hints.ai_family = AF_INET;
	} else if (p_type == TYPE_IPV6) {
		hints.ai_family = AF_INET6;
		hints.ai_flags = 0;
	} else {
		hints.ai_family = AF_UNSPEC;
		hints.ai_flags = AI_ADDRCONFIG;
	}
	hints.ai_flags &= ~AI_NUMERICHOST;

	int s = getaddrinfo(p_hostname.utf8().get_data(), nullptr, &hints, &result);
	if (s != 0) {
		print_verbose("getaddrinfo failed! Cannot resolve hostname.");
		return;
	}

	if (result == nullptr || result->ai_addr == nullptr) {
		print_verbose("Invalid response from getaddrinfo.");
		if (result) {
			freeaddrinfo(result);
		}
		return;
	}

	struct addrinfo *next = result;

	do {
		if (next->ai_addr == nullptr) {
			next = next->ai_next;
			continue;
		}
		IPAddress ip = _sockaddr2ip(next->ai_addr);
		if (ip.is_valid() && !r_addresses.find(ip)) {
			r_addresses.push_back(ip);
		}
		next = next->ai_next;
	} while (next);

	freeaddrinfo(result);
}

#ifdef NX_ENABLED

// Horizon exposes no interface list. The console has one address and
// gethostid() is where libnx puts it.
void IPNX::get_local_interfaces(HashMap<String, Interface_Info> *r_interfaces) const {
	const uint32_t host_id = (uint32_t)gethostid();
	if (host_id == 0) {
		return;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = host_id;

	Interface_Info info;
	info.name = "eth0";
	info.name_friendly = info.name;
	info.index = "1";
	info.ip_addresses.push_front(_sockaddr2ip((struct sockaddr *)&addr));

	r_interfaces->insert(info.name, info);
}

#else

void IPNX::get_local_interfaces(HashMap<String, Interface_Info> *r_interfaces) const {
	struct ifaddrs *ifAddrStruct = nullptr;
	struct ifaddrs *ifa = nullptr;
	int family;

	getifaddrs(&ifAddrStruct);

	for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr) {
			continue;
		}

		family = ifa->ifa_addr->sa_family;

		if (family != AF_INET && family != AF_INET6) {
			continue;
		}

		HashMap<String, Interface_Info>::Iterator E = r_interfaces->find(ifa->ifa_name);
		if (!E) {
			Interface_Info info;
			info.name = ifa->ifa_name;
			info.name_friendly = ifa->ifa_name;
			info.index = String::num_uint64(if_nametoindex(ifa->ifa_name));
			E = r_interfaces->insert(ifa->ifa_name, info);
			ERR_CONTINUE(!E);
		}

		Interface_Info &info = E->value;
		info.ip_addresses.push_front(_sockaddr2ip(ifa->ifa_addr));
	}

	if (ifAddrStruct != nullptr) {
		freeifaddrs(ifAddrStruct);
	}
}

#endif // NX_ENABLED

void IPNX::make_default() {
	_create = _create_nx;
}

IP *IPNX::_create_nx() {
	return memnew(IPNX);
}

IPNX::IPNX() {
}

#endif // UNIX_ENABLED || NX_ENABLED
