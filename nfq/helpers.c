#define _GNU_SOURCE

#include "helpers.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

void hexdump_limited_dlog(const uint8_t *data, size_t size, size_t limit)
{
	size_t k;
	bool bcut = false;
	if (size > limit)
	{
		size = limit;
		bcut = true;
	}
	if (!size) return;
	for (k = 0; k < size; k++) DLOG("%02X ", data[k]);
	DLOG(bcut ? "... : " : ": ");
	for (k = 0; k < size; k++) DLOG("%c", data[k] >= 0x20 && data[k] <= 0x7F ? (char)data[k] : '.');
	if (bcut) DLOG(" ...");
}

char *strncasestr(const char *s, const char *find, size_t slen)
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

bool load_file(const char *filename, void *buffer, size_t *buffer_size)
{
	FILE *F;

	F = fopen(filename, "rb");
	if (!F) return false;

	*buffer_size = fread(buffer, 1, *buffer_size, F);
	if (ferror(F))
	{
		fclose(F);
		return false;
	}

	fclose(F);
	return true;
}
bool load_file_nonempty(const char *filename, void *buffer, size_t *buffer_size)
{
	bool b = load_file(filename, buffer, buffer_size);
	return b && *buffer_size;
}
bool save_file(const char *filename, const void *buffer, size_t buffer_size)
{
	FILE *F;

	F = fopen(filename, "wb");
	if (!F) return false;

	fwrite(buffer, 1, buffer_size, F);
	if (ferror(F))
	{
		fclose(F);
		return false;
	}

	fclose(F);
	return true;
}


void ntop46(const struct sockaddr *sa, char *str, size_t len)
{
	if (!len) return;
	*str = 0;
	switch (sa->sa_family)
	{
	case AF_INET:
		inet_ntop(sa->sa_family, &((struct sockaddr_in*)sa)->sin_addr, str, len);
		break;
	case AF_INET6:
		inet_ntop(sa->sa_family, &((struct sockaddr_in6*)sa)->sin6_addr, str, len);
		break;
	default:
		snprintf(str, len, "UNKNOWN_FAMILY_%d", sa->sa_family);
	}
}
void ntop46_port(const struct sockaddr *sa, char *str, size_t len)
{
	char ip[40];
	ntop46(sa, ip, sizeof(ip));
	switch (sa->sa_family)
	{
	case AF_INET:
		snprintf(str, len, "%s:%u", ip, ntohs(((struct sockaddr_in*)sa)->sin_port));
		break;
	case AF_INET6:
		snprintf(str, len, "[%s]:%u", ip, ntohs(((struct sockaddr_in6*)sa)->sin6_port));
		break;
	default:
		snprintf(str, len, "%s", ip);
	}
}
void print_sockaddr(const struct sockaddr *sa)
{
	char ip_port[48];

	ntop46_port(sa, ip_port, sizeof(ip_port));
	printf("%s", ip_port);
}

void dbgprint_socket_buffers(int fd)
{
	if (params.debug)
	{
		int v;
		socklen_t sz;
		sz = sizeof(int);
		if (!getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &v, &sz))
			DLOG("fd=%d SO_RCVBUF=%d\n", fd, v)
			sz = sizeof(int);
		if (!getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &v, &sz))
			DLOG("fd=%d SO_SNDBUF=%d\n", fd, v)
	}
}
bool set_socket_buffers(int fd, int rcvbuf, int sndbuf)
{
	DLOG("set_socket_buffers fd=%d rcvbuf=%d sndbuf=%d\n", fd, rcvbuf, sndbuf)
		if (rcvbuf && setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int)) < 0)
		{
			perror("setsockopt (SO_RCVBUF)");
			close(fd);
			return false;
		}
	if (sndbuf && setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(int)) < 0)
	{
		perror("setsockopt (SO_SNDBUF)");
		close(fd);
		return false;
	}
	dbgprint_socket_buffers(fd);
	return true;
}

uint64_t pntoh64(const void *p)
{
	return (uint64_t)*((const uint8_t *)(p)+0) << 56 |
		(uint64_t)*((const uint8_t *)(p)+1) << 48 |
		(uint64_t)*((const uint8_t *)(p)+2) << 40 |
		(uint64_t)*((const uint8_t *)(p)+3) << 32 |
		(uint64_t)*((const uint8_t *)(p)+4) << 24 |
		(uint64_t)*((const uint8_t *)(p)+5) << 16 |
		(uint64_t)*((const uint8_t *)(p)+6) << 8 |
		(uint64_t)*((const uint8_t *)(p)+7) << 0;
}
void phton64(uint8_t *p, uint64_t v)
{
	p[0] = (uint8_t)(v >> 56);
	p[1] = (uint8_t)(v >> 48);
	p[2] = (uint8_t)(v >> 40);
	p[3] = (uint8_t)(v >> 32);
	p[4] = (uint8_t)(v >> 24);
	p[5] = (uint8_t)(v >> 16);
	p[6] = (uint8_t)(v >> 8);
	p[7] = (uint8_t)(v >> 0);
}

#define INVALID_HEX_DIGIT ((uint8_t)-1)
static inline uint8_t parse_hex_digit(char c)
{
	return (c>='0' && c<='9') ? c-'0' : (c>='a' && c<='f') ? c-'a'+0xA : (c>='A' && c<='F') ? c-'A'+0xA : INVALID_HEX_DIGIT;
}
static inline bool parse_hex_byte(const char *s, uint8_t *pbyte)
{
	uint8_t u,l;
	u = parse_hex_digit(s[0]);
	l = parse_hex_digit(s[1]);
	if (u==INVALID_HEX_DIGIT || l==INVALID_HEX_DIGIT)
	{
		*pbyte=0;
		return false;
	}
	else
	{
		*pbyte=(u<<4) | l;
		return true;
	}
}
bool parse_hex_str(const char *s, uint8_t *pbuf, size_t *size)
{
	uint8_t *pe = pbuf+*size;
	*size=0;
	while(pbuf<pe && *s)
	{
		if (!parse_hex_byte(s,pbuf))
			return false;
		pbuf++; s+=2; (*size)++;
	}
	return true;
}

void fill_pattern(uint8_t *buf,size_t bufsize,const void *pattern,size_t patsize)
{
	size_t size;

	while (bufsize)
	{
		size = bufsize>patsize ? patsize : bufsize;
		memcpy(buf,pattern,size);
		buf += size;
		bufsize -= size;
	}
}
