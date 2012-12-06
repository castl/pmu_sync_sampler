
#include <arpa/inet.h>

#include <string.h>
#include <stdio.h>

#include "packet.h"
#include "process_info.h"

#define NUM_COUNTERS (6)

static struct {
	void *ptr;
	size_t n;
} memory;

static struct packet_header header;

static struct {
	const char *cmdline;
	const char *exe;
} info;

static struct sample samples[256];
static int initialized = 0;
static int current_index = 0;

static int debug = 0;

static void initialize();
static void clear_data();
static size_t demand_memory();
static void read_header(void *base, struct packet_header *hdr);
static void read_samples(void *base, struct sample *buf, struct packet_header *hdr);
static int read_info(char *base, size_t bytes, char **cmdline, char **exe, size_t *read);
static void* write_header(void *base);
static void* write_samples(void *base);
static void* write_info(void *base);

#define offset(ptr, amt) ((void *)(((size_t)(ptr)) + (amt)))

void packet_set_debug()
{
	debug = 1;
}

int packet_read(void *base, size_t n, struct packet_header *hdr,
    struct sample *buf, char **cmdline, char **exe, size_t *read)
{
	size_t amt = 0;
	*read = 0;

	amt = sizeof(struct packet_header);
	if (n < amt)
		return 1;

	read_header(base, hdr);
	n -= amt;
	*read += amt;
	base = offset(base, amt);

	amt = sizeof(uint32_t) * (NUM_COUNTERS + 1) * hdr->quantity;

	if (n < amt)
		return 1;

	read_samples(base, buf, hdr);
	n -= amt;
	*read += amt;
	base = offset(base, amt);

	return read_info((char *)(base), n, cmdline, exe, read);
}

int packet_should_create(struct buffer& b, struct sample& s, struct ProcessInfo& pi)
{
	int make = 0;
	initialize();

	if (!header.quantity)
		return 0;

	make =	(header.quantity == 255 ||
		 (header.kernel && pi.mode != ProcessInfo::Kernel) ||
 		 header.core != (uint8_t)(b.core) ||
		 header.pid != s.pid);

	if (debug && make) {
		fprintf(stderr, "CREATING:");
		if (header.quantity == 255)
			fprintf(stderr, "  HEADER FULL!");
		if (header.kernel && pi.mode != ProcessInfo::Kernel)
			fprintf(stderr, "  MODE CHANGE!");
		if (header.core != (uint8_t)(b.core))
			fprintf(stderr, "  DIFFERENT CORE!");
		if (header.pid != s.pid)
			fprintf(stderr, "  DIFFERENT PID!");
		fprintf(stderr, "\n");
	}

	return make;
}

int packet_empty()
{
	return (header.quantity == 0);
}

int packet_create(void **save, size_t *packet_size)
{
	void *ptr = NULL;

	initialize();
	*packet_size = 0;
	if (!header.quantity) {
		if (debug)
			fprintf(stderr, "CREATING EMPTY PACKET.\n");

		clear_data();
		*save = NULL;
		return 0;
	}

	*packet_size = demand_memory();
	if (!*packet_size)
		return -1;

	if (debug)
		fprintf(stderr, "CREATING PACKET WITH %zu BYTES!\n", *packet_size);

	ptr = write_header(memory.ptr);
	ptr = write_samples(ptr);
	ptr = write_info(ptr);

	clear_data();
	*save = memory.ptr;
	return 0;
}

int packet_append(struct buffer& b, struct sample& s, struct ProcessInfo& pi, uint32_t missed)
{
	initialize();
	if (packet_should_create(b, s, pi))
		return -1;

	if (!header.quantity) {
		if (debug)
			fprintf(stderr, "INITIALIZING HEADER!\n");

		header.kernel = (pi.mode == ProcessInfo::Kernel) ? 1 : 0;
		header.missed = missed;
		header.first_index = current_index;
		header.counters = (uint8_t) NUM_COUNTERS;
		header.core = (uint8_t)(b.core);
		header.pid = s.pid;

		info.cmdline = pi.cmdline.c_str();
		info.exe = pi.executable.c_str();
	}

	if (debug)
		fprintf(stderr, "ADDING SAMPLE!\n");

	samples[header.quantity] = s;
	++header.quantity;
	++current_index;
	return 0;
}

void packet_start_batch()
{
	if (debug)
		fprintf(stderr, "STARTING BATCH!\n");

	initialize();
	clear_data();
	current_index = 0;
	++header.batch;
}

void packet_clean()
{
	initialize();
	if (memory.ptr)
		free(memory.ptr);
	memory.ptr = NULL;
	memory.n = 0;
}

void clear_data()
{
	uint32_t batch = header.batch;
	memset(&header, 0, sizeof(header));
	memset(&samples[0], 0, sizeof(struct sample) * 256);
	memset(&info, 0, sizeof(info));
	header.batch = batch;
}

size_t demand_memory()
{
	size_t amt = 0;

	amt = 20 + /* Bytes written to a packet header */
	      (4 * (NUM_COUNTERS + 1) * header.quantity) +
	      strlen(info.cmdline)+1 + strlen(info.exe)+1;

	if (amt <= memory.n)
		return amt;
	memory.ptr = realloc(memory.ptr, amt);
	if (!memory.ptr) {
		memory.n = 0;
		return 0;
	}
	memory.n = amt;
	return amt;
}

void read_header(void *base, struct packet_header *hdr)
{
	uint8_t *bytes = (uint8_t *)(base);
	uint32_t *ints = NULL;

	hdr->kernel = bytes[0];
	hdr->counters = bytes[1];
	hdr->core = bytes[2];
	hdr->quantity = bytes[3];
	bytes += 4;

	ints = (uint32_t *)(bytes);
	hdr->batch = ntohl(ints[0]);
	hdr->missed = ntohl(ints[1]);
	hdr->first_index = ntohl(ints[2]);
	hdr->pid = ntohl(ints[3]);
}

void *write_header(void *base)
{
	uint8_t *bytes = (uint8_t *)(base);
	uint32_t *ints = NULL;

	if (debug)
		fprintf(stderr, "WRITING HEADER!\n");

	bytes[0] = header.kernel;
	bytes[1] = header.counters;
	bytes[2] = header.core;
	bytes[3] = header.quantity;
	bytes += 4;

	ints = (uint32_t *)(bytes);
	ints[0] = htonl(header.batch);
	ints[1] = htonl(header.missed);
	ints[2] = htonl(header.first_index);
	ints[3] = htonl(header.pid);
	ints += 4;

	return (void *)(ints);
}

void read_samples(void *base, struct sample *buf, struct packet_header *hdr)
{
	uint32_t *ints = (uint32_t *)(base);
	uint32_t s = 0;
	uint8_t c = 0;

	for (s = 0; s < hdr->quantity; ++s) {
		buf->cycles = ntohl(ints[0]);
		buf->pid = hdr->pid;
		for (c = 0; c < NUM_COUNTERS; ++c)
			buf->counters[c] = ntohl(ints[c+1]);
		ints += NUM_COUNTERS + 1;
		++buf;
	}
}

void *write_samples(void *base)
{
	uint32_t *ints = (uint32_t *)(base);
	uint32_t s = 0;
	uint8_t c = 0;

	if (debug)
		fprintf(stderr, "WRITING %zu SAMPLES!\n", (size_t)(header.quantity));

	for (s = 0; s < header.quantity; ++s) {
		ints[0] = htonl(samples[s].cycles);
		for (c = 0; c < NUM_COUNTERS; ++c)
			ints[1+c] = htonl(samples[s].counters[c]);
		ints += NUM_COUNTERS + 1;
	}
	return (void *)(ints);
}

int read_info(char *ptr, size_t n, char **cmdline, char **exe, size_t *read)
{
	*cmdline = ptr;
	for (; n && *ptr; --n) {
		++ptr;
		++*read;
	}

	--n;
	if (!n)
		return 1;
	++ptr;
	++*read;

	*exe = ptr;
	for (; n && *ptr; --n) {
		++ptr;
		++*read;
	}

	--n;
	++ptr;
	++*read;

	return 0;
}


void *write_info(void *base)
{
	uint8_t *bytes = (uint8_t *)(base);
	size_t cmdlen = strlen(info.cmdline);
	size_t exelen = strlen(info.exe);

	if (debug)
		fprintf(stderr, "WRITING INFO!\n");

	memcpy(bytes, info.cmdline, cmdlen+1);
	bytes += cmdlen+1;
	memcpy(bytes, info.exe, exelen+1);
	bytes += exelen+1;

	return (void *)(bytes);
}

void initialize()
{
	if (initialized)
		return;

	if (debug)
		fprintf(stderr, "INITIALIZING PACKET!\n");

	memset(&header, 0, sizeof(packet_header));
	memset(&memory, 0, sizeof(memory));
	memset(&info, 0, sizeof(info));
	memset(&samples[0], 0, sizeof(struct sample) * 256);
	initialized = 1;
}
