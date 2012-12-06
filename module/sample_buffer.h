#ifndef _SAMPLE_BUFFER_H_
#define _SAMPLE_BUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define BUFFER_SIZE (4*1024)

struct sample {
    unsigned long cycles;
    unsigned long pid;
    unsigned int counters[6];
};

#define BUFFER_ENTRIES ((BUFFER_SIZE - 12) / sizeof(struct sample))
struct buffer {
    unsigned int core;
    unsigned int num_samples;
    struct buffer *nextBuffer; // For linked list purposes
    struct sample samples[BUFFER_ENTRIES];
};

#ifdef __cplusplus
}
#endif

#endif //_SAMPLE_BUFFER_H_