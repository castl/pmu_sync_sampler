#include "module/sample_buffer.h"

#include <stdlib.h>
#include <stdio.h>
#include <cassert>
#include <stdint.h>

#include <fstream>
#include <streambuf>
#include <unordered_map>
#include <string>

using namespace std;

struct ProcessInfo {
	uint32_t pid;
	enum {
		Unknown,
		Kernel,
		User
	} mode;
	string cmdline;
	string executable;

	ProcessInfo() {
		pid = 0;
		mode = Unknown;
	}
};

unordered_map<unsigned long, ProcessInfo> procMap;

void readInto(unsigned long pid, const char* fn, string& into) {
	char fnBuffer[256];
	snprintf(fnBuffer, 256, "/proc/%lu/%s", pid, fn);
	std::ifstream t(fnBuffer);
	into.assign((std::istreambuf_iterator<char>(t)),
                 std::istreambuf_iterator<char>());
}

void readLinkPathInto(unsigned long pid, const char* fn, string& into) {
	char fnBuffer[256], buf[1024];
	snprintf(fnBuffer, 256, "/proc/%lu/%s", pid, fn);
	ssize_t rc = readlink(fnBuffer, buf, sizeof(buf)-1);
	if (rc != -1) {
		buf[rc] = '\0';
		into = buf;
	} else {
		into = "";
	}
}

void populate(ProcessInfo& pi) {
	readInto(pi.pid, "cmdline", pi.cmdline);
	readLinkPathInto(pi.pid, "exe", pi.executable);
	if (pi.executable == "" && pi.cmdline == "")
		pi.mode = ProcessInfo::Kernel;
	else
		pi.mode = ProcessInfo::User;
}

ProcessInfo& getProcessInfo(unsigned long pid) {
	ProcessInfo& pi = procMap[pid];
	if (pi.mode == ProcessInfo::Unknown) {
		pi.pid = pid;
		populate(pi);
	}
	return pi;
}

void outputBuffer(struct buffer& b) {
	assert(b.num_samples <= BUFFER_ENTRIES);
	for (size_t i=0; i<b.num_samples; i++) {
		struct sample& c = b.samples[i];
		ProcessInfo& pi = getProcessInfo(c.pid);
		printf("%lu,%u,%lu,%u,%u,%u,%u,%u,%u,%s,%s\n", 
			c.pid, b.core, c.cycles,
			c.counters[0], c.counters[1], c.counters[2], 
			c.counters[3], c.counters[4], c.counters[5],
			pi.cmdline.c_str(), pi.executable.c_str());
	}	
}

int main(int argc, const char** argv) {
	FILE* f = fopen("/dev/pmu_samples", "rb");
	if (f == NULL) {
		perror("Error opening samples device:");
		return -1;
	}	

	while (!feof(f)) {
		struct buffer b;
		size_t rc = fread(&b, sizeof(struct buffer), 1, f);
		outputBuffer(b);
	}

	fclose(f);

	return 0;	
}