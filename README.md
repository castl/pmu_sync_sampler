pmu_sync_sampler
================

A Linux module for PMU sampling that synchronously samples all counters.

Supports Intel and ARM. Tested chips: Intel Xeon 5550 and TI OMAP4460.
- Intel: mv Makefile.intel Makefile
- Arm:   mv Makefile.arm   Makefile


Bugs: 
- Has a crashing bug related to opening the virtual device at the wrong time.
- For Intel, works on 2.6.32 kernel. 3.2 kernels seem to have some interference with perf_event
