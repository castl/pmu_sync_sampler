#include "pmu_api.h"


#include <linux/interrupt.h>
#include <asm/apic.h>
#include <asm/msr-index.h>
#include <asm/perf_event.h>
#include <asm/nmi.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>

unsigned long num_ctrs = 4;

uint64_t read_ccnt(void) {
	uint64_t c;
	rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR1, c);
	return c;
}

uint64_t read_pmn(unsigned i) {
	uint64_t c;
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR0 + i, c);
	return c;
}

#define write_ccnt(V) wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR1, (V))
#define read_cnf(I, V) rdmsrl(MSR_ARCH_PERFMON_EVENTSEL0 + (I), V)
#define pmn_config(I, C) wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0 + (I), \
            ((0xFFFF & C)) | (1ULL << 16) /* USR */   \
            | (1ULL << 17) /* OS */       \
            | (1ULL << 22) /* EN */ );

void dump_regs(void) {
    uint64_t c, c0, c1, c2, c3, g;
    c = read_ccnt();
    c0 = native_read_msr(MSR_ARCH_PERFMON_PERFCTR0 + 0); 
    c1 = native_read_msr(MSR_ARCH_PERFMON_PERFCTR0 + 1); 
    c2 = native_read_msr(MSR_ARCH_PERFMON_PERFCTR0 + 2); 
    c3 = native_read_msr(MSR_ARCH_PERFMON_PERFCTR0 + 3); 
    
    printk(KERN_ERR "[%u] %llu, %llu, %llu, %llu, %llu",
        smp_processor_id(), c, c0, c1, c2, c3 );

    read_cnf(0, c0);
    read_cnf(1, c1);
    read_cnf(2, c2);
    read_cnf(3, c3);
    

    rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, c);
    rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, g);
    printk(KERN_ERR "[%u] config (%llx, %llx) %llx, %llx, %llx, %llx",
        smp_processor_id(), c, g, c0, c1, c2, c3 );

}

static int __kprobes
my_nmi_handler(struct notifier_block *self, unsigned long cmd, void *__args) {
    size_t i;
    total_interrupts += 1;

    wrmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, (1ULL << 63) | (1ULL << 62) | (0xF));

    gatherSample();

    write_ccnt(0xFFFFFFFFFFFF - period);
    for (i=0; i<num_ctrs; i++) {
        wrmsrl(MSR_ARCH_PERFMON_PERFCTR0 + i, 0);
    }

    if (shutdown != 0) {
        wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);
    }

    native_apic_mem_write(APIC_LVTPC, APIC_DM_NMI); 
    return NOTIFY_STOP;
}

static __read_mostly struct notifier_block my_nmi_notifier = {
    .notifier_call  = my_nmi_handler,
    .next           = NULL,
    .priority       = 0,
};

void register_interrupt(void) {
    register_die_notifier(&my_nmi_notifier);
}

void deregister_interrupt(void) {
    // De-register NMI interrupt handler
    unregister_die_notifier(&my_nmi_notifier);
}

void EnablePerfVect(uint32_t wantEnable) {
    if (wantEnable) {
        native_apic_mem_write(APIC_LVTPC, APIC_DM_NMI); //PERF_MON_VECTOR);
    } else {
        native_apic_mem_write(APIC_LVTPC, APIC_LVT_MASKED);
    }
    return;
}

void stopCtrsLocal(void* d) {
    wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);
    wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0, 0);

    EnablePerfVect(0);
}

void startCtrsLocal(unsigned long* cfgs) {
	int i;
    wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);

    EnablePerfVect(1);

    // Overflow once every 'period' cycles
    write_ccnt(0xFFFFFFFFFFFF - period);
    for (i=0; i<num_ctrs; i++) {
	    pmn_config(i, cfgs[i]); 
    }

    wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, (0xAULL << 4));

    wrmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL,
            (1ULL << 63) | (1ULL << 62) | (1ULL << 33) | (0xF));
    wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0xF | (1ULL << 33));
}

int initialize_arch(void) {
    //Reserve PCMx
    if (!reserve_perfctr_nmi(MSR_ARCH_PERFMON_PERFCTR0)) {
        printk(KERN_ERR "   Error: couldn't reserve perfctr!");
        return -EBUSY;
    }

    //Reserve PerfEvtSelx
    if (!reserve_evntsel_nmi(MSR_ARCH_PERFMON_EVENTSEL0)) {
        release_perfctr_nmi(MSR_ARCH_PERFMON_PERFCTR0);
        printk(KERN_ERR "   Error: couldn't reserve perfctr!");
        return -EBUSY;
    }

    num_ctrs = 4;

    return 0;
}

void cleanup_arch(void) {
    //Release PCMx
    release_perfctr_nmi(MSR_ARCH_PERFMON_PERFCTR0);

    //Release PerfEvtSelx
    release_evntsel_nmi(MSR_ARCH_PERFMON_EVENTSEL0);
}

