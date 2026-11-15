// SPDX-License-Identifier: MIT
/*
 * Minimal UDS (ISO 14229-1) server over an ISO-TP socket (ISO 15765-2).
 *
 * The kernel's can-isotp module does the segmentation, flow control and
 * reassembly, so one read() returns one complete UDS request and one
 * write() sends one complete response, however many CAN frames it takes.
 *
 * Services:
 *   0x10 DiagnosticSessionControl  default (0x01) and extended (0x03)
 *   0x22 ReadDataByIdentifier      one DID per request: 0xF190, 0xF187
 *   0x27 SecurityAccess            level 1: requestSeed 0x01, sendKey 0x02
 *                                  extended session only
 *   0x2E WriteDataByIdentifier     0xF190 only, 17 bytes, extended
 *                                  session and unlocked
 * Anything else gets a negative response (0x7F).
 *
 * The seed/key algorithm is a demonstration, not security: the key is
 * the seed XORed with SECRET_MASK. A session change locks the server
 * again. Three wrong keys lock out requestSeed for LOCKOUT_SECONDS.
 *
 * Not implemented: functional addressing (0x7DF), the S3 session
 * timeout, TesterPresent (0x3E), several DIDs in one 0x22 request.
 *
 * Usage: uds-server <ifname> [rx_id] [tx_id]   (defaults 0x7E0 0x7E8)
 */
#include <errno.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/isotp.h>

#define SID_SESSION_CONTROL	0x10
#define SID_READ_DID		0x22
#define SID_SECURITY_ACCESS	0x27
#define SID_WRITE_DID		0x2E
#define SID_NEGATIVE_RESPONSE	0x7F
#define POSITIVE_OFFSET		0x40

#define NRC_SERVICE_NOT_SUPPORTED		0x11
#define NRC_SUBFUNCTION_NOT_SUPPORTED		0x12
#define NRC_INCORRECT_LENGTH			0x13
#define NRC_REQUEST_SEQUENCE_ERROR		0x24
#define NRC_REQUEST_OUT_OF_RANGE		0x31
#define NRC_SECURITY_ACCESS_DENIED		0x33
#define NRC_INVALID_KEY				0x35
#define NRC_EXCEEDED_ATTEMPTS			0x36
#define NRC_TIME_DELAY_NOT_EXPIRED		0x37
#define NRC_NOT_SUPPORTED_IN_SESSION		0x7F

#define SESSION_DEFAULT		0x01
#define SESSION_EXTENDED	0x03
#define SUPPRESS_POS_RSP	0x80

#define SA_REQUEST_SEED		0x01
#define SA_SEND_KEY		0x02
#define SEED_LEN		4
#define SECRET_MASK		0x5A3C96F0u
#define MAX_ATTEMPTS		3
#define LOCKOUT_SECONDS		10

/* Timing reported in the 0x10 response: P2 in 1 ms, P2* in 10 ms units. */
#define P2_SERVER_MS		50
#define P2_STAR_SERVER_10MS	500	/* 5000 ms */

#define UDS_MAX_LEN		4095	/* ISO-TP classic length limit */
#define DID_MAX_LEN		32

struct did {
	uint16_t id;
	uint8_t data[DID_MAX_LEN];
	size_t len;
	int writable;
};

static struct did dids[] = {
	/* VIN: 17 bytes, needs segmentation; writable when unlocked */
	{ 0xF190, "YONGCHUNIMX8MP001", 17, 1 },
	/* spare part number */
	{ 0xF187, "YC-UDS-0001", 11, 0 },
};

static uint8_t session = SESSION_DEFAULT;
static int unlocked;
static int seed_pending;
static uint32_t seed;
static int failed_attempts;
static time_t lockout_until;

static size_t negative(uint8_t *rsp, uint8_t sid, uint8_t nrc)
{
	rsp[0] = SID_NEGATIVE_RESPONSE;
	rsp[1] = sid;
	rsp[2] = nrc;
	return 3;
}

static struct did *find_did(uint16_t id)
{
	size_t i;

	for (i = 0; i < sizeof(dids) / sizeof(dids[0]); i++)
		if (dids[i].id == id)
			return &dids[i];
	return NULL;
}

/* Returns the response length; 0 means send nothing. */
static size_t session_control(const uint8_t *req, size_t len, uint8_t *rsp)
{
	uint8_t sub, suppress;

	if (len != 2)
		return negative(rsp, req[0], NRC_INCORRECT_LENGTH);

	sub = req[1] & ~SUPPRESS_POS_RSP;
	suppress = req[1] & SUPPRESS_POS_RSP;

	if (sub != SESSION_DEFAULT && sub != SESSION_EXTENDED)
		return negative(rsp, req[0], NRC_SUBFUNCTION_NOT_SUPPORTED);

	session = sub;
	unlocked = 0;		/* any session change locks again */
	seed_pending = 0;
	printf("session -> 0x%02x, locked\n", session);

	if (suppress)
		return 0;

	rsp[0] = req[0] + POSITIVE_OFFSET;
	rsp[1] = sub;
	rsp[2] = P2_SERVER_MS >> 8;
	rsp[3] = P2_SERVER_MS & 0xFF;
	rsp[4] = P2_STAR_SERVER_10MS >> 8;
	rsp[5] = P2_STAR_SERVER_10MS & 0xFF;
	return 6;
}

static size_t read_did(const uint8_t *req, size_t len, uint8_t *rsp)
{
	struct did *d;

	if (len != 3)
		return negative(rsp, req[0], NRC_INCORRECT_LENGTH);

	d = find_did((uint16_t)(req[1] << 8 | req[2]));
	if (!d)
		return negative(rsp, req[0], NRC_REQUEST_OUT_OF_RANGE);

	rsp[0] = req[0] + POSITIVE_OFFSET;
	rsp[1] = req[1];
	rsp[2] = req[2];
	memcpy(&rsp[3], d->data, d->len);
	return 3 + d->len;
}

static uint32_t next_seed(void)
{
	static uint32_t state = 0x12345678u;

	/* xorshift32: changes every request, never 0 for a nonzero state */
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static size_t security_access(const uint8_t *req, size_t len, uint8_t *rsp)
{
	uint8_t sub, suppress;
	uint32_t key;
	int i;

	if (len < 2)
		return negative(rsp, req[0], NRC_INCORRECT_LENGTH);

	sub = req[1] & ~SUPPRESS_POS_RSP;
	suppress = req[1] & SUPPRESS_POS_RSP;

	if (sub != SA_REQUEST_SEED && sub != SA_SEND_KEY)
		return negative(rsp, req[0], NRC_SUBFUNCTION_NOT_SUPPORTED);

	if (session != SESSION_EXTENDED)
		return negative(rsp, req[0], NRC_NOT_SUPPORTED_IN_SESSION);

	if (sub == SA_REQUEST_SEED) {
		if (len != 2)
			return negative(rsp, req[0], NRC_INCORRECT_LENGTH);
		if (time(NULL) < lockout_until)
			return negative(rsp, req[0], NRC_TIME_DELAY_NOT_EXPIRED);

		rsp[0] = req[0] + POSITIVE_OFFSET;
		rsp[1] = sub;
		if (unlocked) {
			/* already unlocked: a zero seed says so */
			memset(&rsp[2], 0, SEED_LEN);
		} else {
			seed = next_seed();
			seed_pending = 1;
			for (i = 0; i < SEED_LEN; i++)
				rsp[2 + i] = seed >> (24 - 8 * i);
		}
		return 2 + SEED_LEN;	/* the seed is the point: never suppressed */
	}

	/* sendKey */
	if (len != 2 + SEED_LEN)
		return negative(rsp, req[0], NRC_INCORRECT_LENGTH);
	if (!seed_pending)
		return negative(rsp, req[0], NRC_REQUEST_SEQUENCE_ERROR);

	seed_pending = 0;
	key = (uint32_t)req[2] << 24 | (uint32_t)req[3] << 16 |
	      (uint32_t)req[4] << 8 | req[5];

	if (key != (seed ^ SECRET_MASK)) {
		if (++failed_attempts >= MAX_ATTEMPTS) {
			failed_attempts = 0;
			lockout_until = time(NULL) + LOCKOUT_SECONDS;
			return negative(rsp, req[0], NRC_EXCEEDED_ATTEMPTS);
		}
		return negative(rsp, req[0], NRC_INVALID_KEY);
	}

	failed_attempts = 0;
	unlocked = 1;
	printf("security level 1 unlocked\n");

	if (suppress)
		return 0;

	rsp[0] = req[0] + POSITIVE_OFFSET;
	rsp[1] = sub;
	return 2;
}

static size_t write_did(const uint8_t *req, size_t len, uint8_t *rsp)
{
	struct did *d;

	if (len < 4)
		return negative(rsp, req[0], NRC_INCORRECT_LENGTH);

	if (session != SESSION_EXTENDED)
		return negative(rsp, req[0], NRC_NOT_SUPPORTED_IN_SESSION);

	d = find_did((uint16_t)(req[1] << 8 | req[2]));
	if (!d || !d->writable)
		return negative(rsp, req[0], NRC_REQUEST_OUT_OF_RANGE);

	if (len - 3 != d->len)
		return negative(rsp, req[0], NRC_INCORRECT_LENGTH);

	if (!unlocked)
		return negative(rsp, req[0], NRC_SECURITY_ACCESS_DENIED);

	memcpy(d->data, &req[3], d->len);
	printf("DID 0x%04X written\n", d->id);

	rsp[0] = req[0] + POSITIVE_OFFSET;
	rsp[1] = req[1];
	rsp[2] = req[2];
	return 3;
}

static size_t dispatch(const uint8_t *req, size_t len, uint8_t *rsp)
{
	switch (req[0]) {
	case SID_SESSION_CONTROL:
		return session_control(req, len, rsp);
	case SID_READ_DID:
		return read_did(req, len, rsp);
	case SID_SECURITY_ACCESS:
		return security_access(req, len, rsp);
	case SID_WRITE_DID:
		return write_did(req, len, rsp);
	default:
		return negative(rsp, req[0], NRC_SERVICE_NOT_SUPPORTED);
	}
}

static void dump(const char *tag, const uint8_t *buf, size_t len)
{
	size_t i;

	printf("%s", tag);
	for (i = 0; i < len; i++)
		printf(" %02X", buf[i]);
	printf("\n");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	struct sockaddr_can addr = { 0 };
	uint8_t req[UDS_MAX_LEN], rsp[UDS_MAX_LEN];
	ssize_t n;
	size_t out;
	int s;

	if (argc < 2 || argc > 4) {
		fprintf(stderr, "usage: %s <ifname> [rx_id] [tx_id]\n", argv[0]);
		return 1;
	}

	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[1]);
	if (!addr.can_ifindex) {
		fprintf(stderr, "%s: no such interface\n", argv[1]);
		return 1;
	}
	addr.can_addr.tp.rx_id = argc > 2 ? strtoul(argv[2], NULL, 16) : 0x7E0;
	addr.can_addr.tp.tx_id = argc > 3 ? strtoul(argv[3], NULL, 16) : 0x7E8;

	s = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP);
	if (s < 0) {
		perror("socket (is can-isotp loaded?)");
		return 1;
	}
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	printf("UDS server on %s: rx 0x%03X tx 0x%03X\n", argv[1],
	       addr.can_addr.tp.rx_id, addr.can_addr.tp.tx_id);
	fflush(stdout);

	for (;;) {
		n = read(s, req, sizeof(req));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("read");
			return 1;
		}
		if (n == 0)
			continue;

		dump("req:", req, n);
		out = dispatch(req, n, rsp);
		if (out == 0)
			continue;

		dump("rsp:", rsp, out);
		if (write(s, rsp, out) < 0)
			perror("write");
	}
}
