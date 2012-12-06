/* 
 *  Kernel module to use ARMv7 counters
 */

#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_ERR */
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "sample_buffer.h"
#include "pmu_api.h"

#define MIN_PERIOD 10000

uint64_t period = 100000;
volatile unsigned char shutdown = 0;
volatile uint64_t total_interrupts = 0;

struct blist {
    spinlock_t lock;
    struct buffer* head;
    struct buffer* tail;
};

void init_blist(struct blist* list) {
    spin_lock_init(&list->lock);
    list->head = NULL;
    list->tail = NULL;
}

void append_blist(struct blist* list, struct buffer* b) {
    unsigned long flags;
    if (b == NULL) {
        printk(KERN_ERR "Error in append_blist: NULL b");
        return;
    }

    if (list == NULL) {
        printk(KERN_ERR "Error in append_blist: NULL list");
        return;
    }

    spin_lock_irqsave(&list->lock, flags);

    b->nextBuffer = NULL;
    if (list->head == NULL || list->tail == NULL) {
        list->head = b;
        list->tail = b;
    } else {
        list->tail->nextBuffer = b;
        list->tail = b;
    }

    spin_unlock_irqrestore(&list->lock, flags);
}

struct buffer* pop_blist(struct blist* list) {
    unsigned long flags;
    struct buffer* ret = NULL;

    if (list == NULL) {
        printk(KERN_ERR "Error in append_blist: NULL list");
        return NULL;
    }

    spin_lock_irqsave(&list->lock, flags);
    if (list->head == NULL)
        goto exit;

    ret = list->head;
    list->head = ret->nextBuffer;
    if (list->head == NULL)
        list->tail = NULL;
    ret->nextBuffer = NULL;

exit:
    spin_unlock_irqrestore(&list->lock, flags);
    return ret;
}

DEFINE_PER_CPU(struct buffer*, lbuffer);
static struct blist  empty_buffers;
static struct blist  full_buffers;

DECLARE_WAIT_QUEUE_HEAD (read_queue);

int my_open(struct inode *inode,struct file *filep);
int my_release(struct inode *inode,struct file *filep);
ssize_t my_read(struct file *filep,char *buff,size_t count,loff_t *offp );
ssize_t my_write(struct file *filep,const char *buff,size_t count,loff_t *offp );

struct file_operations my_fops={
    open: my_open,
    read: my_read,
    write: my_write,
    release:my_release,
};

int my_open(struct inode *inode,struct file *filep)
{
    // Don't do any open  -- stateless outlet
    return 0;
}

int my_release(struct inode *inode,struct file *filep)
{
    // Don't do any close -- stateless outlet
    return 0;
}

ssize_t my_read(struct file *filep,char *buff,size_t count,loff_t *offp )
{
    struct buffer *b = NULL;
    ssize_t ret;

    if (count < BUFFER_SIZE) {
        printk(KERN_ERR "PMU Sync Usage warning: buffer must be at least %u bytes long.", BUFFER_SIZE);
        return -EINVAL;
    }

    if (shutdown != 0)
        return 0;

    if (wait_event_interruptible(read_queue,
            (  ( (b = pop_blist(&full_buffers)) != NULL)
            || ( shutdown == 2 ) ) ) != 0) 
        return 0;

    if (b == NULL)
        return 0;

    if (copy_to_user(buff, b, sizeof(struct buffer)) != 0) {
        printk(KERN_ERR "PMU Sync error: could not copy to userspace");
        ret = -EINVAL;
    } else {
        ret = sizeof(struct buffer);
    }

    append_blist(&empty_buffers, b);

    return ret;
}
ssize_t my_write(struct file *filep,const char *buff,size_t count,loff_t *offp )
{
    // Ignore writes
    printk(KERN_ERR "PMU Sync usage warning: writes are ignored");
    return -EPERM;
}

struct int_attr {
    struct attribute attr;
    unsigned int value;
};

static struct int_attr period_attr = {
    .attr.name="period",
    .attr.mode = 0644,
    .value = 1000000,
};

static struct int_attr status_attr = {
    .attr.name="status",
    .attr.mode = 0644,
    .value = 0,
};

static struct int_attr missed_attr = {
    .attr.name="missed",
    .attr.mode = 0644,
    .value = 0,
};

static struct int_attr ctr0_attr = {
    .attr.name="0",
    .attr.mode = 0644,
    .value = 0x8,
};

static struct int_attr ctr1_attr = {
    .attr.name="1",
    .attr.mode = 0644,
    .value = 0,
};

static struct int_attr ctr2_attr = {
    .attr.name="2",
    .attr.mode = 0644,
    .value = 0,
};

static struct int_attr ctr3_attr = {
    .attr.name="3",
    .attr.mode = 0644,
    .value = 0,
};


static void initialize_buffer(struct buffer* b) {
    b->core = smp_processor_id();
    b->num_samples = 0;
}


void gatherSample(void) {
    unsigned int proc = smp_processor_id();
    struct buffer* b = per_cpu(lbuffer, proc); 
    struct sample* s;
    unsigned i;

    if (b == NULL) {
        b = pop_blist(&empty_buffers);
        if (b == NULL) {
            // No available buffers!
            missed_attr.value += 1;
            return;
        }
        initialize_buffer(b);
        per_cpu(lbuffer, proc) = b;
    }
    s = &b->samples[b->num_samples++];
    s->cycles = read_ccnt();
    s->cycles += period;
    s->pid = current->pid;
    for (i=0; i<num_ctrs; i++) {
        s->counters[i] = read_pmn(i);
    }    

    if (b->num_samples >= BUFFER_ENTRIES) {
        append_blist(&full_buffers, b);
        per_cpu(lbuffer, proc) = NULL;
        wake_up_all(&read_queue);
    }
}


static void startCtrs(void* d) {
    unsigned int proc = smp_processor_id();
    unsigned long cfgs[6] = {
        ctr0_attr.value,  
        ctr1_attr.value, 
        ctr2_attr.value, 
        ctr3_attr.value
    };
    printk(KERN_ERR "Configuring PMU on core %u\n", proc);

    if (per_cpu(lbuffer, proc) != NULL)
        append_blist(&empty_buffers, per_cpu(lbuffer, proc));
    per_cpu(lbuffer, proc) = NULL;

    startCtrsLocal(cfgs);
}

static void dumpCtrs(void* d) {
    dump_regs();
}


static void stopAll(void) {
    shutdown = 1;
    // De-configure the counters
    on_each_cpu(stopCtrsLocal, NULL, 1);

    deregister_interrupt();

    shutdown = 2;

    wake_up_all(&read_queue);
}

static void process_status_update(void) {
    switch (status_attr.value) {
        case 0:
            printk(KERN_ERR "Turning off Sync-PMU");
            stopAll();
            break;
        case 1:
            printk(KERN_ERR "Turning on Sync-PMU");
            if (period_attr.value < MIN_PERIOD) {
                printk(KERN_ERR "    Period value (%u) too low. Increasing.",
                            period_attr.value);
                period_attr.value = MIN_PERIOD;
            }
            period = period_attr.value;

            // De-configure the counters
            shutdown = 0;
            register_interrupt();
            on_each_cpu(startCtrs, NULL, 1);
            break;
        case 2:
            printk(KERN_ERR "    Interrupts taken: %llu", total_interrupts);
            on_each_cpu(dumpCtrs, NULL, 1);
            break;
        default:
            printk(KERN_ERR "Sync-PMU: unknown code %u", status_attr.value);
            status_attr.value = !shutdown;
            break;
    }
}

static struct attribute * myattr[] = {
    &period_attr.attr,
    &status_attr.attr,
    &missed_attr.attr,
    &ctr0_attr.attr,
    &ctr1_attr.attr,
    &ctr2_attr.attr,
    &ctr3_attr.attr,
    NULL
};

static ssize_t default_show(struct kobject *kobj, struct attribute *attr,
        char *buf)
{
    struct int_attr *a = container_of(attr, struct int_attr, attr);
    return scnprintf(buf, PAGE_SIZE, "%d\n", a->value);
}

static ssize_t default_store(struct kobject *kobj, struct attribute *attr,
        const char *buf, size_t len)
{
    struct int_attr *a = container_of(attr, struct int_attr, attr);
    unsigned int value;
    if (strlen(buf) > 2 && 
        buf[0] == '0' && buf[1] == 'x' &&
        sscanf(buf, "0x%x", &value) == 1) {
        a->value = value;
        if (a == &status_attr)
            process_status_update();
    } else if (sscanf(buf, "%u", &value) == 1) {
        a->value = value;
        if (a == &status_attr)
            process_status_update();
    }
    return len;
}

static struct sysfs_ops myops = {
    .show = default_show,
    .store = default_store,
};

static struct kobj_type mytype = {
    .sysfs_ops = &myops,
    .default_attrs = myattr,
};

struct kobject *mykobj;
static int init_sysfs_entries(void)
{
    int err = -1;
    mykobj = kzalloc(sizeof(*mykobj), GFP_KERNEL);
    if (mykobj) {
        kobject_init(mykobj, &mytype);
        if (kobject_add(mykobj, NULL, "%s", "sync_pmu")) {
             err = -1;
             printk("Sysfs creation failed\n");
             kobject_put(mykobj);
             mykobj = NULL;
        }
        err = 0;
    }
    return err;
}

int init_module(void)
{
    int i, rc;
    printk(KERN_ERR "Initializing PMU Synchronous Sampler...");
    printk(KERN_ERR "    Attempting to turn on EMU...");

    if ((rc =initialize_arch()) != 0) {
        return rc;
    }

    init_blist(&empty_buffers);
    init_blist(&full_buffers);

    printk(KERN_ERR "    Configuring interrupt handler");

    init_sysfs_entries();

    // Initialize buffers
    for (i=0; i<8; i++) { 
        append_blist(&empty_buffers, kzalloc(sizeof(struct buffer), GFP_KERNEL));
    }

    // Set up char device
    if(register_chrdev(222,"pmu_samples", &my_fops)){
        printk("<1>failed to register");
    }  

    printk(KERN_ERR "    Finished initializing.");
    return 0;
}

void cleanup_module(void)
{
    unsigned int proc;
    struct buffer* b;
    printk(KERN_ERR "Shutting down PMU Synchronuous Samples...");
    printk(KERN_ERR "    Interrupts taken: %llu", total_interrupts);

    shutdown = 1;
    asm volatile("");

    printk(KERN_ERR "    De-allocating sysfs entries");
    if (mykobj) {
        kobject_put(mykobj);
        kfree(mykobj);
    }

    unregister_chrdev(222, "pmu_samples");

    printk(KERN_ERR "    Turning off interrupt handler");

    printk(KERN_ERR "    De-configuring counters");

    stopAll();
    cleanup_arch();

    printk(KERN_ERR "Flushing data");

    for (proc=0; proc < nr_cpu_ids; proc++) {
        if (per_cpu(lbuffer, proc)) {
            append_blist(&full_buffers, per_cpu(lbuffer, proc));
            per_cpu(lbuffer, proc) = NULL;
        }
    } 

    printk(KERN_ERR "    Freeing memory");
    while ((b = pop_blist(&empty_buffers))) {
        kfree(b);
    }
    while ((b = pop_blist(&full_buffers))) {
        kfree(b);
    }


    printk(KERN_ERR "Done\n");
}

MODULE_LICENSE("GPL");

