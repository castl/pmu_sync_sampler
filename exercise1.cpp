#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <cmath>
#include <time.h>
#include <sys/time.h>

volatile float f = 50.5;

int main(int argc, char **argv) {
    printf("[%u] Exercise is supposedly good for the body...\n", getpid());

	struct timeval last, now;
	gettimeofday(&last, NULL);

    uint64_t ctr = 0;
    while (1) {
    	#pragma omp parallel for
    	for (size_t i=0; i<10000; i++) {
	    	f = f * 3000;
	    	f = sin(f);
    	}

    	ctr++;

		gettimeofday(&now, NULL);
		uint64_t d = ((((uint64_t)now.tv_sec) * 1000000 + now.tv_usec)
				  - (((uint64_t)last.tv_sec) * 1000000 + last.tv_usec));
		if (d > 5000000) {
			printf("Ctrs/second = %lf\n", ((double)ctr) * 1.0e6 / d);
			last = now;
			ctr = 0;
		}
    }

    return 0;
}

