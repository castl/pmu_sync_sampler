#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cstring>

#include "../sender/packet.h"

#include "reader.hpp"

void process_file(const char* fileName, const char* desc) {
	printf("-- Description --\n%s\n-----------\n", desc);
}

void process_packet(struct packet_header head,
					const char* cmdline, const char* exe,
					struct sample* samples) {
	printf("== Krnl %u, #Ctrs %u, Core %u, Qty %u, Batch %u, Miss %u, 1st Idx %u, PID %u ==\n",
		   head.kernel, head.counters, head.core, head.quantity,
		   head.batch, head.missed, head.first_index, head.pid);
	printf("<< cmd:  %s; exe:  %s >>\n", cmdline, exe);
	for (size_t i=0; i<head.quantity; i++) {
		struct sample c = samples[i];
		printf("\t(%lu): %u,%u,%u,%u,%u,%u\n",
			c.cycles,
			c.counters[0], c.counters[1], c.counters[2],
			c.counters[3], c.counters[4], c.counters[5]);
	}
	printf("\n");
}


int main(int argc, char** argv) {
	if (argc != 2) {
		printf("Usage: %s <data filename>\n", argv[0]);
		return 1;
	}

	return read_file(argv[1]);
}