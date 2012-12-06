#include "pmu_api.h"


#include <linux/platform_device.h>
#include <../mach-omap2/prm44xx.h>
#include <../mach-omap2/cm2_44xx.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/irq_work.h>
#include <asm/cti.h>
#include <asm/uaccess.h>

#include "v7_pmu.h"

unsigned long num_ctrs = 6;
static struct cti omap4_cti[2];

uint64_t read_ccnt(void) {
	return read_ccnt_int();
}

uint64_t read_pmn(unsigned i) {
	return read_pmn_int(i);
}


void dump_regs(void) {
    u32 val;
    unsigned int cnt;

    printk(KERN_INFO "PMNC Core %u registers dump:\n", smp_processor_id());

    asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
    printk(KERN_INFO "PMNC  =0x%08x\n", val);

    asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r" (val));
    printk(KERN_INFO "CNTENS=0x%08x\n", val);

    asm volatile("mrc p15, 0, %0, c9, c14, 1" : "=r" (val));
    printk(KERN_INFO "INTENS=0x%08x\n", val);

    asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));
    printk(KERN_INFO "FLAGS =0x%08x\n", val);

    asm volatile("mrc p15, 0, %0, c9, c12, 5" : "=r" (val));
    printk(KERN_INFO "SELECT=0x%08x\n", val);

    asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
    printk(KERN_INFO "CCNT  =0x%08x\n", val);

    asm volatile("mrc p15, 0, %0, c9, c14, 0" : "=r" (val));
    printk(KERN_INFO "USREN =0x%08x\n", val);
    
    asm volatile("mrc p14, 0, %0, c7, c14, 6" : "=r" (val));
    printk(KERN_INFO "DBGAUTHSTATUS: %x", val);

    for (cnt = 0; cnt < num_ctrs; cnt++) {
        printk(KERN_INFO "CNT[%d] count =0x%08x\n",
            cnt, read_pmn_int(cnt));
        asm volatile("mrc p15, 0, %0, c9, c13, 1" : "=r" (val));
        printk(KERN_INFO "CNT[%d] evtsel=0x%08x\n",
            cnt, val);
    }

}

static irqreturn_t sample_handle_irq(int irqnum, void* dev)
{
    unsigned int flags;
    // unsigned int ccnt = read_ccnt();

    if (irqnum == OMAP44XX_IRQ_CTI0) {
        cti_irq_ack(&omap4_cti[0]);
        // printk(KERN_INFO " -- CTI0 --");
    } else if (irqnum == OMAP44XX_IRQ_CTI1) {
        cti_irq_ack(&omap4_cti[1]);
        // printk(KERN_INFO " -- CTI1 --");
    } else {
         return IRQ_NONE;
    }

    disable_pmu();

    // printk(KERN_INFO "== Interrupt Dump (CCNT: %u) ==", ccnt);

    flags = read_flags(); 
    if (flags == 0) {
        printk(KERN_WARNING "Possible interrupt error. Flags: 0x%x", flags);
        return IRQ_NONE;
    }

    gatherSample();

    reset_pmn();
    if (shutdown == 0)
        write_ccnt(0xFFFFFFFF - period);

    // Reset overflow flags
    write_flags(0xFFFFFFFF);

    if (shutdown == 0)
        enable_pmu();

    return IRQ_HANDLED;
}

/**
 * omap4_pmu_runtime_resume - PMU runtime resume callback
 * @dev     OMAP PMU device
 *
 * Platform specific PMU runtime resume callback for OMAP4430 devices to
 * configure the cross trigger interface for routing PMU interrupts. This
 * is called by the PM runtime framework.
 */
static void pmu_cti_on(void)
{
    /* configure CTI0 for PMU IRQ routing */
    cti_unlock(&omap4_cti[0]);
    cti_map_trigger(&omap4_cti[0], 1, 6, 2);
    cti_enable(&omap4_cti[0]);

    /* configure CTI1 for PMU IRQ routing */
    cti_unlock(&omap4_cti[1]);
    cti_map_trigger(&omap4_cti[1], 1, 6, 3);
    cti_enable(&omap4_cti[1]);
}

/**
 * omap4_pmu_runtime_suspend - PMU runtime suspend callback
 * @dev     OMAP PMU device
 *
 * Platform specific PMU runtime suspend callback for OMAP4430 devices to
 * disable the cross trigger interface interrupts. This is called by the
 * PM runtime framework.
 */
static void pmu_cti_off(void)
{
    cti_disable(&omap4_cti[0]);
    cti_disable(&omap4_cti[1]);
}

/**
 * omap4_init_cti - initialise cross trigger interface instances
 *
 * Initialises two cross trigger interface (CTI) instances in preparation
 * for routing PMU interrupts to the OMAP interrupt controller. Note that
 * this does not configure the actual CTI hardware but just the CTI
 * software structures to be used.
 */
static int omap4_init_cti(void)
{
    omap4_cti[0].base = ioremap(OMAP44XX_CTI0_BASE, SZ_4K);
    omap4_cti[1].base = ioremap(OMAP44XX_CTI1_BASE, SZ_4K);

    if (!omap4_cti[0].base || !omap4_cti[1].base) {
        pr_err("ioremap for OMAP4 CTI failed\n");
        return -ENOMEM;
    }

    cti_init(&omap4_cti[0], omap4_cti[0].base, OMAP44XX_IRQ_CTI0, 6);
    cti_init(&omap4_cti[1], omap4_cti[1].base, OMAP44XX_IRQ_CTI1, 6);

    return 0;
}

static int config_irq(void) {
    unsigned long irq_flags;
    int rc = omap4_init_cti();
    if (rc)
        return rc;

    pmu_cti_on();

    irq_flags = IRQF_NOBALANCING;

    if ((rc = irq_set_affinity(OMAP44XX_IRQ_CTI0, cpumask_of(0)))) {
        printk(KERN_ERR "    unable to set irq affinity on processor 0");
        return rc;
    }

    rc = request_irq(OMAP44XX_IRQ_CTI0, sample_handle_irq, irq_flags,
                    "sync-pmu", NULL);
    if (rc)
        return rc;

    if ((rc = irq_set_affinity(OMAP44XX_IRQ_CTI1, cpumask_of(1)))) {
        printk(KERN_ERR "    unable to set irq affinity on processor 1");
        free_irq(OMAP44XX_IRQ_CTI0, NULL);
        return rc;
    }

    rc = request_irq(OMAP44XX_IRQ_CTI1, sample_handle_irq, irq_flags,
                    "sync-pmu", NULL);
    if (rc) {
        free_irq(OMAP44XX_IRQ_CTI0, NULL);
        return rc;
    }

    return 0;
}

// No-ops
void register_interrupt(void) { }
void deregister_interrupt(void) { }

void stopCtrsLocal(void* d) {
    unsigned int i;

    disable_ccnt_irq();
    disable_ccnt();
    for (i = 0; i < num_ctrs; i++) {
        disable_pmn(i); 
    }
    disable_pmu();
}

void startCtrsLocal(unsigned long* cfgs) {
    unsigned int i;
    unsigned int proc = smp_processor_id();
    printk(KERN_INFO "Configuring PMU on core %u\n", proc);

    disable_pmu();
    disable_ccnt();

    reset_ccnt();
    reset_pmn();

    // Reset overflow flags
    write_flags(0xFFFFFFFF);

    // Overflow once every 'period' cycles
    write_ccnt(0xFFFFFFFF - period);
    for (i=0; i<num_ctrs; i++) {
        pmn_config(i, cfgs[i]); 
    }

    dump_regs();

    enable_ccnt_irq();
    for (i = 0; i < num_ctrs; i++) {
        enable_pmn(i);
    }

    enable_ccnt();
    enable_pmu();
}

int initialize_arch(void) {
    int rc;
    unsigned long val;

    //Enabling PMU clock and power domains
    __raw_writel(2, OMAP4430_CM_EMU_CLKSTCTRL);
    __raw_writel(1, OMAP4430_CM_L3INSTR_L3_3_CLKCTRL);
    __raw_writel(1, OMAP4430_CM_L3INSTR_L3_INSTR_CLKCTRL);
    __raw_writel(1, OMAP4430_CM_PRM_PROFILING_CLKCTRL);
    __raw_writel(3l << 24, OMAP4430_PM_EMU_PWRSTST);

    do {
        val = __raw_readl(OMAP4430_CM_EMU_DEBUGSS_CLKCTRL);
    } while ((val & 0x30000) != 0);

    __raw_writel(0x101, OMAP4430_RM_EMU_DEBUGSS_CONTEXT);


    num_ctrs = getPMN();

    printk(KERN_INFO "    Found %lu counters", num_ctrs);
    printk(KERN_INFO "    Configuring interrupt handler");

    rc = config_irq();
    if (rc) {
        printk(KERN_ERR "    -> Error configuring interrupts (%d)!!!", rc);
    }

    return 0;
}

void cleanup_arch(void) {
    pmu_cti_off();
    free_irq(OMAP44XX_IRQ_CTI0, NULL);
    free_irq(OMAP44XX_IRQ_CTI1, NULL);
}

