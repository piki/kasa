#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define KASA_PORT 9999

uint8_t *kasa_crypto(const uint8_t *p, int len, int enc) {
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

uint8_t *kasa_encrypt(const uint8_t *p, int len) {
	return kasa_crypto(p, len, 1);
}

uint8_t *kasa_decrypt(const uint8_t *p, int len) {
	return kasa_crypto(p, len, 0);
}

int main(int argc, const char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:\n  %s ip json-command\n", argv[0]);
		exit(1);
	}

	struct in_addr addr;
	if (!inet_aton(argv[1], &addr)) {
		fprintf(stderr, "Could not parse \"%s\" as an IP\n", argv[1]);
		exit(1);
	}

	uint8_t *enc = kasa_encrypt((const uint8_t*)argv[2], strlen(argv[2]));

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

	sin.sin_family = AF_INET;
	sin.sin_port = htons(KASA_PORT);
	sin.sin_addr = addr;
	res = sendto(sock, enc, strlen(argv[2]), 0, (struct sockaddr*)&sin, sizeof(sin));
	if (res == -1) {
		perror("sendto");
		exit(1);
	}
	free(enc);

	char buf[4096];
	socklen_t slen = sizeof(sin);
	res = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&sin, &slen);
	if (res == -1) {
		perror("recvfrom");
		exit(1);
	}

	uint8_t *dec = kasa_decrypt((const uint8_t*)buf, res);
	fwrite((const char*)dec, 1, res, stdout);
	putchar('\n');
	free(dec);

	return 0;
}
