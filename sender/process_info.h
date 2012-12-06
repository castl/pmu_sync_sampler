
#ifndef ANDROID_ARM_PROJECT_PROCESS_INFO_H
#define ANDROID_ARM_PROJECT_PROCESS_INFO_H

#include <string>

struct ProcessInfo {
	uint32_t pid;

	struct {
		struct {
			int flag;
			int checks;
		} zygote, cmdexe;
	} flags;

	enum {
		Unknown,
		Kernel,
		User
	} mode;
	std::string cmdline;
	std::string executable;

	ProcessInfo();
};

ProcessInfo& getProcessInfo(unsigned long pid, int check_flags);

#endif
