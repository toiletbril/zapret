#define _GNU_SOURCE

#include "helpers.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

char *strncasestr(const char *s,const char *find, size_t slen)
{
	char c, sc;
	size_t len;

	if ((c = *find++) != '\0')
	{
		len = strlen(find);
		do
		{
			do
			{
				if (slen-- < 1 || (sc = *s++) == '\0') return NULL;
			} while (toupper(c) != toupper(sc));
			if (len > slen)	return NULL;
		} while (strncasecmp(s, find, len) != 0);
		s--;
	}
	return (char *)s;
}

void ntop46(const struct sockaddr *sa, char *str, size_t len)
{
	if (!len) return;
	*str=0;
	switch (sa->sa_family)
	{
	case AF_INET:
		inet_ntop(sa->sa_family, &((struct sockaddr_in*)sa)->sin_addr, str, len);
		break;
	case AF_INET6:
		inet_ntop(sa->sa_family, &((struct sockaddr_in6*)sa)->sin6_addr, str, len);
		break;
	default:
		snprintf(str,len,"UNKNOWN_FAMILY_%d",sa->sa_family);
	}
}
void ntop46_port(const struct sockaddr *sa, char *str, size_t len)
{
	char ip[40];
	ntop46(sa,ip,sizeof(ip));
	switch (sa->sa_family)
	{
	case AF_INET:
		snprintf(str,len,"%s:%u",ip,ntohs(((struct sockaddr_in*)sa)->sin_port));
		break;
	case AF_INET6:
		snprintf(str,len,"[%s]:%u",ip,ntohs(((struct sockaddr_in6*)sa)->sin6_port));
		break;
	default:
		snprintf(str,len,"%s",ip);
	}
}
void print_sockaddr(const struct sockaddr *sa)
{
	char ip_port[48];

	ntop46_port(sa,ip_port,sizeof(ip_port));
	printf("%s",ip_port);
}


// -1 = error,  0 = not local, 1 = local
bool check_local_ip(const struct sockaddr *saddr)
{
	struct ifaddrs *addrs,*a;

	if (is_localnet(saddr))
		return true;

	if (getifaddrs(&addrs)<0) return false;
	a  = addrs;

	bool bres=false;
	while (a)
	{
		if (a->ifa_addr && sacmp(a->ifa_addr,saddr))
		{
			bres=true;
			break;
		}
		a = a->ifa_next;
	}

	freeifaddrs(addrs);
	return bres;
}
void print_addrinfo(const struct addrinfo *ai)
{
	char str[64];
	while (ai)
	{
		switch (ai->ai_family)
		{
		case AF_INET:
			if (inet_ntop(ai->ai_family, &((struct sockaddr_in*)ai->ai_addr)->sin_addr, str, sizeof(str)))
				printf("%s\n", str);
			break;
		case AF_INET6:
			if (inet_ntop(ai->ai_family, &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr, str, sizeof(str)))
				printf( "%s\n", str);
			break;
		}
		ai = ai->ai_next;
	}
}



bool saismapped(const struct sockaddr_in6 *sa)
{
	// ::ffff:1.2.3.4
	return !memcmp(sa->sin6_addr.s6_addr,"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff",12);
}
bool samappedcmp(const struct sockaddr_in *sa1,const struct sockaddr_in6 *sa2)
{
	return saismapped(sa2) && !memcmp(sa2->sin6_addr.s6_addr+12,&sa1->sin_addr.s_addr,4);
}
bool sacmp(const struct sockaddr *sa1,const struct sockaddr *sa2)
{
	return sa1->sa_family==AF_INET && sa2->sa_family==AF_INET && !memcmp(&((struct sockaddr_in*)sa1)->sin_addr,&((struct sockaddr_in*)sa2)->sin_addr,sizeof(struct in_addr)) ||
		sa1->sa_family==AF_INET6 && sa2->sa_family==AF_INET6 && !memcmp(&((struct sockaddr_in6*)sa1)->sin6_addr,&((struct sockaddr_in6*)sa2)->sin6_addr,sizeof(struct in6_addr)) ||
		sa1->sa_family==AF_INET && sa2->sa_family==AF_INET6 && samappedcmp((struct sockaddr_in*)sa1,(struct sockaddr_in6*)sa2) ||
		sa1->sa_family==AF_INET6 && sa2->sa_family==AF_INET && samappedcmp((struct sockaddr_in*)sa2,(struct sockaddr_in6*)sa1);
}
uint16_t saport(const struct sockaddr *sa)
{
	return htons(sa->sa_family==AF_INET ? ((struct sockaddr_in*)sa)->sin_port :
		     sa->sa_family==AF_INET6 ? ((struct sockaddr_in6*)sa)->sin6_port : 0);
}
bool saconvmapped(struct sockaddr_storage *a)
{
	if ((a->ss_family == AF_INET6) && saismapped((struct sockaddr_in6*)a))
	{
		uint32_t ip4 = *(uint32_t*)(((struct sockaddr_in6*)a)->sin6_addr.s6_addr+12);
		uint16_t port = ((struct sockaddr_in6*)a)->sin6_port;
		a->ss_family = AF_INET;
		((struct sockaddr_in*)a)->sin_addr.s_addr = ip4;
		((struct sockaddr_in*)a)->sin_port = port;
		return true;
	}
	return false;
}

bool is_localnet(const struct sockaddr *a)
{
	// 0.0.0.0, ::ffff:0.0.0.0 = localhost in linux
	return a->sa_family==AF_INET && (*(char*)&((struct sockaddr_in *)a)->sin_addr.s_addr==127 || !((struct sockaddr_in *)a)->sin_addr.s_addr) ||
		a->sa_family==AF_INET6 && saismapped((struct sockaddr_in6 *)a) && (((struct sockaddr_in6 *)a)->sin6_addr.s6_addr[12]==127 || !*(uint32_t*)(((struct sockaddr_in6 *)a)->sin6_addr.s6_addr+12));
}
bool is_linklocal(const struct sockaddr_in6 *a)
{
	// fe80::/10
	return a->sin6_addr.s6_addr[0]==0xFE && (a->sin6_addr.s6_addr[1] & 0xC0)==0x80;
}
bool is_private6(const struct sockaddr_in6* a)
{
	// fc00::/7
	return (a->sin6_addr.s6_addr[0] & 0xFE) == 0xFC;
}



bool set_keepalive(int fd)
{
	int yes=1;
	return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int))!=-1;
}
bool set_ttl(int fd, int ttl)
{
	return setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl))!=-1;
}
bool set_hl(int fd, int hl)
{
	return setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hl, sizeof(hl))!=-1;
}
bool set_ttl_hl(int fd, int ttl)
{
	bool b1,b2;
	// try to set both but one may fail if family is wrong
	b1=set_ttl(fd, ttl);
	b2=set_hl(fd, ttl);
	return b1 || b2;
}
int get_so_error(int fd)
{
	// getsockopt(SO_ERROR) clears error
	int errn;
	socklen_t optlen = sizeof(errn);
	if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &errn, &optlen) == -1)
		errn=errno;
	return errn;
}
