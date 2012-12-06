#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cstring>

#include "../sender/packet.h"

#include "reader.hpp"

void read_file_loop(const char* fileName, void* file, size_t file_size) {
        size_t position = strlen((char*)file) + 1;
        struct sample samples[256];
        struct packet_header pkt_head;

        process_file(fileName, (const char*) file);

        while (position < file_size) {
                size_t pbytes;
                char* cmdline;
                char* exe;

                void* currPos = &(((uint8_t*)file)[position]);
                int rc = packet_read(currPos, file_size - position,
                                                         &pkt_head, samples, &cmdline, &exe,
                                                         &pbytes);
                if (rc != 0) {
                        fprintf(stderr, "Error reading packet. Position: %lu, file_size: %lu\n",
                                        position, file_size);
                        break;
                }

                process_packet(pkt_head, cmdline, exe, samples);

                position += pbytes;
        }
}


int read_file(const char* fileName) {
        int fd;
        void* file;
        struct stat file_info;
        size_t filesize;

        if (stat(fileName, &file_info) == -1) {
                perror("Error stat'ing file");
                return 1;
        }

        filesize = file_info.st_size;

        fd = open(fileName, O_RDONLY);
        if (fd == -1) {
                perror("Error opening file for reading");
                return 1;
        }

        file = mmap(0, filesize, PROT_READ, MAP_SHARED, fd, 0);
        if (file == MAP_FAILED) {
                perror("Error mmapping file");
                close(fd);
                return 1;
        }

        read_file_loop(fileName, file, filesize);

        if (munmap(file, filesize) == -1) {
                perror("Error unmapping file");
        }

        if (close(fd)) {
                perror("Error closing file");
        }

        return 0;
}

