ccflags-y = -mtune=native -march=native -O2
obj-m += pmu_sync_sample.o 

pmu_sync_sample-objs := pmu_sync_sample_main.o intel.o
obj-$(CONFIG_X86) += intel.o

all:
	make -C /lib/modules/`uname -r`/build M=$(PWD) modules

clean:
	make -C /lib/modules/`uname -r`/build M=$(PWD) clean

