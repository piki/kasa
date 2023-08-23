/* Raw TP-Link (Kasa) command sender/receiver
 * Each invocation sends a single command and receives a single packet in response.
 *
 * This program is a minimal substitute for the tplink-lightbulb Node package.
 * Because it's C, it's about 40x faster than the Node package.  On a Raspberry
 * Pi, that matters.
 *
 * Building:
 *   make
 *
 * Usage:
 *   ./kasa <ip-address> <json-blob>
 *
 * There's a good list of JSON blobs to try here:
 *   https://github.com/softScheck/tplink-smartplug/blob/master/tplink-smarthome-commands.txt
 * Especially:
 *   - get bulb info: ./kasa <ip> '{"system":{"get_sysinfo":null}}'
 *   - turn bulb on:  ./kasa <ip> '{"system":{"set_relay_state":{"state":1}}}'
 *   - turn bulb off: ./kasa <ip> '{"system":{"set_relay_state":{"state":0}}}'
 *
 * Written by Patrick Reynolds <dukepiki@gmail.com>
 * Released into the public domain, or Creative Commons CC0, your choice.
 */

#include <errno.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#define DEFAULT_TIMEOUT 5
#define MAX_BROADCAST_ADDRESSES 255
#define KASA_PORT 9999
#define ALIAS_MARKER "\"alias\":\""
#define MODEL_MARKER "\"model\":\""

static void scan(int timeout);
static void scan_range(int timeout, uint32_t start, uint32_t end);
static void command(int timeout, const char *ip_str, const char *json);
static int bind_udp_socket();
static void send_udp(int sock, struct in_addr addr, int port, uint8_t *str, int len);
static uint8_t *kasa_encrypt(const uint8_t *p, int len);
static uint8_t *kasa_decrypt(const uint8_t *p, int len);
static void usage(const char *prog);

int main(int argc, char* const* argv) {
	int timeout = DEFAULT_TIMEOUT;
	int c;
	while ((c = getopt(argc, argv, "t:")) != -1) {
		switch (c) {
			case 't': timeout = atoi(optarg); break;
			default: usage(argv[0]);
		}
	}

	if (optind < argc && !strcmp(argv[optind], "scan")) {
		scan(timeout);
	}
	else if (optind < argc-1) {
		command(timeout, argv[optind], argv[optind+1]);
	}
	else {
		usage(argv[0]);
	}
	
	return 0;
}

static void usage(const char *prog) {
	fprintf(stderr, "Usage:\n  %s ip json-command\n", prog);
	exit(1);
}

static uint8_t *kasa_crypto(const uint8_t *p, int len, int enc) {
	uint8_t *ret = malloc(len+1);
	ret[len] = '\0';
	uint8_t k = 0xab;
	int i;
	for (i=0; i<len; i++) {
		ret[i] = (uint8_t)(k ^ p[i]);
		k = enc ? ret[i] : p[i];
	}
	return ret;
}

static uint8_t *kasa_encrypt(const uint8_t *p, int len) {
	return kasa_crypto(p, len, 1);
}

static uint8_t *kasa_decrypt(const uint8_t *p, int len) {
	return kasa_crypto(p, len, 0);
}

static void scan(int timeout) {
	struct ifaddrs *ifaddr, *ifa;

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(1);
	}

	for (ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) continue;
		if (ifa->ifa_flags & IFF_LOOPBACK) continue;
		if (ifa->ifa_addr->sa_family != AF_INET) continue;
		if (ifa->ifa_netmask->sa_family != AF_INET) continue;

		const struct sockaddr_in *ip_sin = (struct sockaddr_in*)ifa->ifa_addr;
		char ip[INET_ADDRSTRLEN];
		inet_ntop(ifa->ifa_addr->sa_family, &ip_sin->sin_addr, ip, sizeof(ip));
		const struct sockaddr_in *netmask_sin = (struct sockaddr_in*)ifa->ifa_netmask;
		char netmask[INET_ADDRSTRLEN];
		inet_ntop(ifa->ifa_netmask->sa_family, &netmask_sin->sin_addr, netmask, sizeof(netmask));
		fprintf(stderr, "Interface: %s\t Address: %s Netmask: %s\n", ifa->ifa_name, ip, netmask);

		uint32_t ip_int = ntohl(ip_sin->sin_addr.s_addr);
		uint32_t mask_int = ntohl(netmask_sin->sin_addr.s_addr);
		scan_range(timeout, (ip_int&mask_int)+1, ((ip_int&mask_int)|~mask_int)-1);
	}

	freeifaddrs(ifaddr);
}

static void scan_range(int timeout, uint32_t start, uint32_t end) {
	if (end-start+1 > MAX_BROADCAST_ADDRESSES) {
		fprintf(stderr, "Skipping range with %d addresses\n", end-start+1);
		return;
	}

	const char *json = "{\"system\":{\"get_sysinfo\":{}}}";
	uint8_t *enc = kasa_encrypt((uint8_t*)json, strlen(json));
	int sock = bind_udp_socket();

	for (uint32_t x=start; x<=end; x++) {
		struct in_addr addr = { .s_addr = htonl(x) };
		send_udp(sock, addr, KASA_PORT, enc, strlen(json));
	}
	free(enc);

	while (1) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);
		struct timeval tv = { .tv_sec = timeout, .tv_usec = 0 };
		int res = select(sock+1, &rfds, 0, 0, &tv);
		if (res == -1) {
			perror("select");
			exit(1);
		}
		if (res == 0) {
			return;
		}

		struct sockaddr_in sin;
		char buf[4096];
		socklen_t slen = sizeof(sin);
		res = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&sin, &slen);

		char *dec = (char*)kasa_decrypt((uint8_t*)buf, res);
		char *alias = strstr(dec, ALIAS_MARKER);
		if (alias) {
			alias += strlen(ALIAS_MARKER);
			const char *end = strchr(alias, '"');
			alias = end ? strndup(alias, end-alias) : 0;
		}
		char *model = strstr(dec, MODEL_MARKER);
		if (model) {
			model += strlen(MODEL_MARKER);
			const char *end = strchr(model, '"');
			model = end ? strndup(model, end-model) : 0;
		}
		if (alias && model) {
			printf("%s - %s - %s\n", inet_ntoa(sin.sin_addr), alias, model);
		}
		if (alias) free(alias);
		if (model) free(model);
		free(dec);
	}
}

static void command(int timeout, const char *ip_str, const char *json) {
	struct in_addr addr;
	if (!inet_aton(ip_str, &addr)) {
		fprintf(stderr, "Could not parse \"%s\" as an IP\n", ip_str);
		exit(1);
	}

	uint8_t *enc = kasa_encrypt((uint8_t*)json, strlen(json));

	int sock = bind_udp_socket();
	send_udp(sock, addr, KASA_PORT, enc, strlen(json));
	free(enc);

	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	struct timeval tv = { .tv_sec = timeout, .tv_usec = 0 };
	int res = select(sock+1, &rfds, 0, 0, &tv);
	if (res == -1) {
		perror("select");
		exit(1);
	}
	if (res == 0) {
		puts("Timeout");
		exit(0);
	}

	struct sockaddr_in sin;
	char buf[4096];
	socklen_t slen = sizeof(sin);
	res = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&sin, &slen);
	//fprintf(stderr, "res=%d slen=%d remote=%s:%d\n", res, slen, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

	uint8_t *dec = kasa_decrypt((uint8_t*)buf, res);
	fwrite(dec, 1, res, stdout);
	puts("");
	free(dec);
}

static int bind_udp_socket() {
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock == -1) {
		perror("socket");
		exit(1);
	}

	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(0);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	int res = bind(sock, (struct sockaddr*)&sin, sizeof(sin));
	if (res == -1) {
		perror("bind");
		exit(1);
	}

	return sock;
}

static void send_udp(int sock, struct in_addr addr, int port, uint8_t *str, int len) {
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr = addr;
	struct iovec iov[1];
	iov[0].iov_base = str;
	iov[0].iov_len = len;
	struct msghdr msg;
	memcpy(&msg.msg_name, &sin, sizeof(sin));
	msg.msg_namelen = sizeof(sin);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;
	int res = sendto(sock, str, len, 0, (struct sockaddr*)&sin, sizeof(sin));
	if (res == -1) {
		perror("sendto");
		exit(1);
	}
}
