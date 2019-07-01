#ifndef _NET_UTIL_H_
#define _NET_UTIL_H_

#include <netdb.h>

int net_dns_lookup(const char* addr, const char *port,
		struct addrinfo** addr_info);


#endif /*_NET_UTIL_H_*/

