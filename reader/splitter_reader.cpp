#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cstring>
#include <unordered_map>
#include <cassert>
#include <errno.h>
#include <boost/foreach.hpp>
#include <limits.h>

using namespace std;

#include "../sender/packet.h"
#include "reader.hpp"

const char* topDir;
enum {
	Text,
	Binary
} outputFormat;

const uint32_t zeros[7] = {0, 0, 0, 0, 0, 0, 0};

#define CKRC(rc, msg) if ((rc) < 0) { perror(msg); exit(-1); }

size_t putbinstr(const char* str, FILE* f) {
	return fwrite(str, strlen(str) + 1, 1, f);
}

void outputStringToFile(const char* fn, const char* str) { 
	int fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	CKRC(fd, "Opening file");

	size_t dlen = strlen(str);
	ssize_t wrc = write(fd, str, dlen);
	CKRC(wrc, "writing to file");
	close(fd);
}

class CoreOutput {
	struct FileInfo {
		FILE* f;
		uint32_t pid;
		string exe, cmdline, fileName;

		FileInfo() : f(NULL) { }

		FileInfo(FILE* f, uint32_t pid, string exe,
				 string cmdline, string fileName) :
			f(f), pid(pid), exe(exe), cmdline(cmdline), fileName(fileName) { }

		void close(FILE* indexfd) {
	        fprintf(indexfd, "%u,%s,%s,%s\n", pid,
	        	cmdline.c_str(), exe.c_str(), fileName.c_str());
	        fclose(f);
		}
	};
	static unordered_map<uint32_t, FileInfo> pid_files;
    static FILE* indexfd;

	bool has_last_head;
	struct packet_header last_head;
	FILE* currFile;

public:
	CoreOutput() : has_last_head(false), currFile(NULL) { }

    static void startIndex() {
        char buf[1024];
        snprintf(buf, 1024, "%s/index.csv", topDir);
        indexfd = fopen(buf, "w");
        if (indexfd == NULL) {
            perror("Error opening index file for output: ");
        }
    }

	static void closeAll() {
		BOOST_FOREACH(auto p, pid_files) {
			p.second.close(indexfd);
		}
		pid_files.clear();

        fclose(indexfd);
        indexfd = NULL;
	}

	void checkIncrFile(struct packet_header new_head, 
					   const char* cmdline, const char* exe) {

		// Update info
		auto& currInfo = pid_files[new_head.pid];
		currInfo.cmdline = cmdline;
		currInfo.exe = exe;

		if (!has_last_head ||
			last_head.pid != new_head.pid) {

			if (currFile != NULL) {
				switch(outputFormat) {
					case Text:
						fprintf(currFile, ",,,,,,\n");
						break;
					case Binary:
						fwrite(zeros, sizeof(uint32_t), 7, currFile);
						break;
					default:
						assert(false);
				}
			}

			has_last_head = true;
			last_head = new_head;

			currFile = currInfo.f; 

			if (currFile == NULL) {
				char fn[1024], fn2[PATH_MAX];
				snprintf(fn, 1024, "%s/%u.csv", topDir, new_head.pid);
				currFile = fopen(fn, "w");
				if (currFile == NULL) {
					perror("Error opening new data file");
					exit (-1);
				}


				if (realpath(fn, fn2) == NULL) {
					perror("Error resolving realpath: ");
					exit(-1);
				};

				currInfo = FileInfo(currFile, new_head.pid, exe, cmdline, fn2);

				switch(outputFormat) {
					case Text:
						fprintf(currFile, "%s, %s\n", cmdline, exe);
						break;
					case Binary:
						putbinstr("Trace binary file\n", currFile);
						putbinstr(cmdline, currFile);
						putbinstr(exe, currFile);
						break;
					default:
						assert(false);
				}
			} 
		} 

		assert(currFile != NULL);
	}

	void process_packet(struct packet_header head,
						const char* cmdline, const char* exe,
						struct sample* samples) {
		checkIncrFile(head, cmdline, exe);

		for (size_t i=0; i<head.quantity; i++) {
			struct sample c = samples[i];
			switch (outputFormat) {
				case Text:
					fprintf(currFile, "%lu,%u,%u,%u,%u,%u,%u\n",
						c.cycles,
						c.counters[0], c.counters[1], c.counters[2],
						c.counters[3], c.counters[4], c.counters[5]);
					break;
				case Binary: {
					uint32_t nums[7] = {
						(uint32_t)c.cycles,
						c.counters[0], c.counters[1], c.counters[2],
						c.counters[3], c.counters[4], c.counters[5] };
					fwrite(nums, sizeof(uint32_t), 7, currFile);
					break;
				}
				default:
					assert(false);
			}

		}
	}
};

unordered_map<uint32_t, CoreOutput::FileInfo> CoreOutput::pid_files;
FILE* CoreOutput::indexfd;
CoreOutput cores[16];

void process_file(const char* fileName, const char* desc) {
	char setupfn[500];
	snprintf(setupfn, 500, "%s/setup.txt", topDir);
	outputStringToFile(setupfn, desc);
}

void process_packet(struct packet_header head,
					const char* cmdline, const char* exe,
					struct sample* samples) {
	if (head.core > 16) {
		fprintf(stderr, "Possible data corruption: head.core (%d) more than 16. Skipping packet", head.core);
		return;
	}
	assert(head.core < 16);
	cores[head.core].process_packet(head, cmdline, exe, samples);

	// printf("== Krnl %u, #Ctrs %u, Core %u, Qty %u, Batch %u, Miss %u, 1st Idx %u, PID %u ==\n",
	// 	   head.kernel, head.counters, head.core, head.quantity,
	// 	   head.batch, head.missed, head.first_index, head.pid);
	// printf("<< cmd:  %s; exe:  %s >>\n", cmdline, exe);
	// for (size_t i=0; i<head.quantity; i++) {
	// 	struct sample c = samples[i];
	// 	printf("\t(%lu): %u,%u,%u,%u,%u,%u\n",
	// 		c.cycles,
	// 		c.counters[0], c.counters[1], c.counters[2],
	// 		c.counters[3], c.counters[4], c.counters[5]);
	// }
	// printf("\n");
}


int main(int argc, char** argv) {
	if (argc < 3) {
		printf("Usage: %s <data filename> <output dir> [format]\n", argv[0]);
		return 1;
	}

	if (argc > 3) {
		switch (argv[3][0]) {
			case 't':
				outputFormat = Text;
				break;
			case 'b':
				outputFormat = Binary;
				break;
			default:
				fprintf(stderr, "Unknown output format: %c!\n", argv[3][0]);
				return -1;
		}
	} else {
		outputFormat = Binary;
	}


	topDir = argv[2];
	if (mkdir(topDir, 0777) != 0) {
		perror("Error creating directory: ");
		return -1;
	}

	CoreOutput::startIndex();
	int rc = read_file(argv[1]);
	CoreOutput::closeAll();
	return rc;
}
