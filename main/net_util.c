#include "net_util.h"
#include "util.h"

int net_dns_lookup(const char* addr, const char *port,
		struct addrinfo** addr_info)
{
	int err;
	struct in_addr* raddr;
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	err = getaddrinfo(addr, port, &hints, addr_info);

	if(err || !*addr_info) {
		LOGE("DNS lookup failure %d\n", err);
		if(*addr_info) {
			freeaddrinfo(*addr_info);
		}
		return -1;
	}
	// DNS lookup OK
	raddr = &((struct sockaddr_in *)(*addr_info)->ai_addr)->sin_addr;
	LOGE("DNS lookup succeeded. IP=%s\n", inet_ntoa(*raddr));

	return 0;
}

