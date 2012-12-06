#ifndef __READER_HPP_
#define __READER_HPP_

// Defined by client 
extern void process_packet(struct packet_header head,
					const char* cmdline, const char* exe,
					struct sample* samples);
extern void process_file(const char* fileName, const char* desc);

// Defined by library
extern int read_file(const char* fileName);

#endif // __READER_HPP_

