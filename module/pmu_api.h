#ifndef __PMU_API_H__
#define __PMU_API_H__

#include <linux/types.h>

// Provided in architecture-specific c file
extern unsigned long num_ctrs;
uint64_t read_ccnt(void);
uint64_t read_pmn(unsigned);
int initialize_arch(void);
void cleanup_arch(void);
void startCtrsLocal(unsigned long *);
void stopCtrsLocal(void*);
void dump_regs(void);
void register_interrupt(void);
void deregister_interrupt(void);

// Used in architecture-specific interrupt
void gatherSample(void);
extern volatile uint64_t total_interrupts;
extern uint64_t period;
extern volatile unsigned char shutdown;

#endif