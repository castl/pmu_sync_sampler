
#ifndef ANDROID_ARM_PROJECT_PACKET_H
#define ANDROID_ARM_PROJECT_PACKET_H

#include <stdlib.h>
#include <stdint.h>

#include "sample_buffer.h"

struct packet_header {
        uint8_t kernel;
        uint8_t counters;
        uint8_t core;
        uint8_t quantity;

        uint32_t batch;
        uint32_t missed;		/* WARNING:  Constant across batch! */
        uint32_t first_index;
        uint32_t pid;
};

/*
 * Read a packet from a source using network byte order.
 * Arguments:
 *    bytes -- the data read from the network
 *    n -- the number of bytes in bytes
 *    hdr -- where to read the header into
 *    buf -- where to read samples into (should be at least 256 samples long)
 *    cmdline -- this will be set to where the cmdline starts IN BYTES
 *    exe -- this will be set to where the exe starts IN BYTES
 *    read -- number of bytes read
 *
 * Returns 0 if everything went okay or 1 if more bytes are needed
 */
int packet_read(void *bytes, size_t n, struct packet_header *hdr,
    struct sample *buf, char **cmdline, char **exe, size_t *read);


/*
 * If the given arguments would result in a different packet header
 * or if there are already a maximum amount of samples, then return
 * true; else return false.
 */
int packet_should_create(struct buffer& b, struct sample& s, struct ProcessInfo& pi);

/* Return whether a the current packet is empty. */
int packet_empty();

/*
 * Create a packet from the current state.
 * Returns 0 if everything went okay, and -1 if there was an error.
 * If everything goes okay, then *save will point to the memory of the
 * packet created, which can be NULL if there are no samples.
 * Note that the number of bytes present is given.
 * When things do not go well, errno is set.
 * Note that the pointer should not be free'd and will get clobbered by
 * the next call.
 */
int packet_create(void **save, size_t *bytes);

/*
 * Append the sample to the packet.
 * Returns 0 if the sample was appended, -1 if you should create a
 * packet since this data conflicts with existing data.
 */
int packet_append(struct buffer& b, struct sample& s, struct ProcessInfo& pi, uint32_t missed);

/* Increment to the next batch */
void packet_start_batch();

/* Clean up resources with respect to packets */
void packet_clean();

/* Set debugging on */
void packet_set_debug();

#endif
