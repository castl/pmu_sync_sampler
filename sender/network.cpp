
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <error.h>
#include <stdlib.h>

#include "packet.h"
#include "process_info.h"

static int the_socket = 0;
static struct addrinfo *addr = NULL;
static int transmit(size_t *sent);
static int debug = 0;

static FILE *network_debug = NULL;

int network_send(struct buffer &b, uint32_t missed, size_t *total)
{
	size_t sent = 0;
	size_t sent_total = 0;
	size_t index = 0;
	void *data = NULL;

	assert(b.num_samples <= BUFFER_ENTRIES);

	if (debug)
		fprintf(stderr, "STARTING BATCH WITH %u SAMPLES!\n", b.num_samples);

	packet_start_batch();
	for (index = 0; index < b.num_samples; ++index) {
		struct sample &s = b.samples[index];
		struct ProcessInfo& pi = getProcessInfo(s.pid, packet_empty());

		if (packet_should_create(b, s, pi)) {
			if (debug)
				fprintf(stderr, "PACKET MUST BE SENT BEFORE CONTINUING.\n");
			if (transmit(&sent))
				return -1;
			sent_total += sent;
		}

		/* This should never happen given the above statement */
		if (packet_append(b, s, pi, missed))
			return -1;
	}
	/* Anything at the end should be sent */
	if (transmit(&sent))
		return -1;
	sent_total += sent;
	if (total)
		*total = sent_total;
	return 0;
}

void network_set_debug()
{
	debug = 1;
}

int network_init(const char *node, const char *service)
{
	int status = 0;

	struct addrinfo hints;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	the_socket = 0;
	addr = NULL;

	status = getaddrinfo(node, service, &hints, &addr);
	if (status < 0) {
		the_socket = 0;
		addr = NULL;

		fprintf(stderr, "gaierror:  %s\n", gai_strerror(status));
		return -1;
	}

	the_socket = socket(PF_INET, SOCK_STREAM, 0);
	if (the_socket < 0) {
		freeaddrinfo(addr);

		the_socket = 0;
		addr = NULL;

		fprintf(stderr, "socket error:  %s\n", strerror(errno));
		return -1;
	}

	if (connect(the_socket, addr->ai_addr, addr->ai_addrlen)) {
		freeaddrinfo(addr);

		the_socket = 0;
		addr = NULL;

		fprintf(stderr, "connect error:  %s\n", strerror(errno));
		return errno;
	}

	if (debug) {
		fprintf(stderr, "NETWORK INITIALIZED!\n");
		network_debug = fopen("packet.debug", "wb");
	}

	return 0;
}

int network_packet(void *packet, size_t bytes, size_t *sent)
{
	ssize_t sent_bytes = sendto(the_socket, packet, bytes, 0, NULL, 0);

	if (debug)
		fprintf(stderr, "Writing out packet with %zu bytes.\n", bytes);

	if (sent_bytes < 0) {
		fprintf(stderr, "Send error:  %s\n", strerror(errno));
		return -1;
	}
	if (sent_bytes != bytes) {
		fprintf(stderr, "Error, could not send entire packet.\n");
		return -1;
	}
	if (network_debug && !fwrite(packet, 1, bytes, network_debug)) {
		fprintf(stderr, "Error writing out network debug info:  %s\n", strerror(errno));
		return -1;
	}

	if (debug)
		fprintf(stderr, "Packet successfully sent.\n");

	if (sent)
		*sent = sent_bytes;
	return 0;
}

int network_finish()
{
	if (the_socket) {
		shutdown(the_socket, SHUT_RDWR);
		close(the_socket);
		the_socket = 0;
	}

	if (addr) {
		freeaddrinfo(addr);
		addr = NULL;
	}

	if (network_debug) {
		fclose(network_debug);
		network_debug = NULL;
	}

	packet_clean();
	return 0;
}

int transmit(size_t *sent)
{
	void *packet = NULL;
	size_t bytes = 0;

	if (packet_create(&packet, &bytes))
		return -1;
	if (!packet)
		return 0;

	return network_packet(packet, bytes, sent);
}
