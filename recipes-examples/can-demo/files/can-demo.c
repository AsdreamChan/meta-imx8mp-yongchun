// SPDX-License-Identifier: MIT
/*
 * can-demo: send one classical CAN frame on a SocketCAN interface, then
 * print every frame received on it until interrupted.
 *
 * Usage: can-demo <ifname>      e.g. can-demo vcan0
 */
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

static void print_frame(const struct can_frame *f)
{
	if (f->can_id & CAN_EFF_FLAG)
		printf("%08X", f->can_id & CAN_EFF_MASK);
	else
		printf("     %03X", f->can_id & CAN_SFF_MASK);

	printf("   [%u] ", f->len);

	if (f->can_id & CAN_RTR_FLAG) {
		printf(" remote request\n");
		return;
	}
	for (int i = 0; i < f->len; i++)
		printf(" %02X", f->data[i]);
	printf("\n");
}

int main(int argc, char *argv[])
{
	struct sockaddr_can addr = { .can_family = AF_CAN };
	struct can_frame frame = {
		.can_id = 0x123,
		.len = 4,
		.data = { 0xDE, 0xAD, 0xBE, 0xEF },
	};
	int recv_own = 0;
	int s;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <ifname>\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* A raw CAN socket: whole frames in, whole frames out. */
	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}

	addr.can_ifindex = if_nametoindex(argv[1]);
	if (!addr.can_ifindex) {
		perror(argv[1]);
		return EXIT_FAILURE;
	}

	/*
	 * By default a socket does not get back the frames it sends itself;
	 * other sockets on the same interface (candump) still do. Turn it on
	 * so this one program shows its own frame coming back.
	 */
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
		       &recv_own, sizeof(recv_own)) < 0) {
		perror("setsockopt CAN_RAW_RECV_OWN_MSGS");
		return EXIT_FAILURE;
	}

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return EXIT_FAILURE;
	}

	if (write(s, &frame, sizeof(frame)) != sizeof(frame)) {
		perror("write");
		return EXIT_FAILURE;
	}

	for (;;) {
		ssize_t n = read(s, &frame, sizeof(frame));

		if (n < 0) {
			perror("read");
			return EXIT_FAILURE;
		}
		if (n != sizeof(frame)) {
			fprintf(stderr, "unexpected read of %zd bytes\n", n);
			continue;
		}
		print_frame(&frame);
		fflush(stdout);
	}
}
