
#ifndef ANDROID_ARM_PROJECT_NETWORK_H
#define ANDROID_ARM_PROJECT_NETWORK_H

#include "sample_buffer.h"

void network_set_debug();

int network_init(const char *node, const char *service);
int network_finish();
int network_send(struct buffer &b, uint32_t missed, size_t *total);
int network_packet(void *name, size_t bytes, size_t *sent); /* Direct write! */

#endif
