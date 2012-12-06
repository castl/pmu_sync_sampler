
#include "process_info.h"
#include "sample_buffer.h"

#include <string.h>

#include <stdlib.h>
#include <stdio.h>

#include <fstream>
#include <streambuf>
#include <unordered_map>

#define PROCESS_INFO_FLAG_CHECKS (10)

using namespace std;

static unordered_map<unsigned long, ProcessInfo> procMap;
static unsigned long earliest_zygote = 0;

int read_cmdline(unsigned long pid, string& into);
int read_executable(unsigned long pid, string& into);
int load_process_info(struct ProcessInfo& pi);

ProcessInfo::ProcessInfo()
{
	flags.zygote.flag = 0;
	flags.cmdexe.flag = 0;
	flags.zygote.checks = PROCESS_INFO_FLAG_CHECKS;
	flags.cmdexe.checks = PROCESS_INFO_FLAG_CHECKS;

	pid = 0;
	mode = Unknown;
}


ProcessInfo& getProcessInfo(unsigned long pid, int check_flags)
{
	ProcessInfo& pi = procMap[pid];

	/* After this the mode is set, so we only ever do this once. */
	if (pi.mode == ProcessInfo::Unknown) {
		pi.pid = pid;
		pi.flags.cmdexe.flag = load_process_info(pi);
		if (!pi.flags.cmdexe.flag) {
			pi.flags.cmdexe.checks = 0;
			pi.flags.zygote.flag = !strcmp(pi.cmdline.c_str(), "zygote");
			if (pi.flags.zygote.flag)
				earliest_zygote = earliest_zygote ? min(pid, earliest_zygote) : pid;
			else
				pi.flags.zygote.checks = 0;
		}
		return pi;
	}

	/* Once we get a valid response we set the cmdexe checks to 0,
	 * so that we never backtrack to this point again.  We only move
	 * forward.  If don't get zygote, then we set its checks to zero
	 * and never do that either.
	 */
	if (check_flags && pi.flags.cmdexe.flag && pi.flags.cmdexe.checks) {
		--pi.flags.cmdexe.checks;
		pi.flags.cmdexe.flag = load_process_info(pi);
		if (!pi.flags.cmdexe.flag) {
			pi.flags.cmdexe.checks = 0;
			pi.flags.zygote.flag = !strcmp(pi.cmdline.c_str(), "zygote");
			if (pi.flags.zygote.flag)
				earliest_zygote = earliest_zygote ? min(pid, earliest_zygote) : pid;
			else
				pi.flags.zygote.checks = 0;
		}
		return pi;
	}

	/* Once we get a valid response we never do this again.  If we get
	 * a bad loading, we just run with it.  So we only ever come back
	 * when we are a zygote still.
	 * Additionally -- we only check zygotes a certain number of times
	 * UNLESS there is an earlier zygote.  In which case we always check.
	 */
	if (check_flags && pi.flags.zygote.flag && (pid > earliest_zygote || pi.flags.zygote.checks)) {
		if (pi.flags.zygote.checks)
			--pi.flags.zygote.checks;
		/* If things go bad, just let them be bad... */
		if (load_process_info(pi)) {
			pi.flags.zygote.checks = 0;
		} else {
			pi.flags.zygote.flag = !strcmp(pi.cmdline.c_str(), "zygote");
			if (!pi.flags.zygote.flag)
				pi.flags.zygote.checks = 0;
		}
		return pi;
	}

	return pi;
}

int load_process_info(struct ProcessInfo& pi)
{
	int rval = 0;

	if (read_cmdline(pi.pid, pi.cmdline))
		rval = 1;
	if (read_executable(pi.pid, pi.executable))
		rval = 1;
	pi.mode = (!*pi.cmdline.c_str() && !*pi.executable.c_str()) ? ProcessInfo::Kernel : ProcessInfo::User;

	return rval;
}

int read_cmdline(unsigned long pid, string& into)
{
	char file[256] = {0};
	int failed = 1;

	snprintf(&file[0], 256, "/proc/%lu/cmdline", pid);
	std::ifstream strm(file);
	if (strm.fail()) {
		into = "ifstream::fail()";
	} else try {
		into.assign(std::istreambuf_iterator<char>(strm),
			std::istreambuf_iterator<char>());
		failed = 0;
	} catch (std::ios_base::failure f) {
		into = "ios_base::failure";
	}

	return failed;
}

int read_executable(unsigned long pid, string& into)
{
	char file[256] = {0};
	char link[1024] = {0};
	ssize_t rc = 0;
	int failed = 1;

	snprintf(&file[0], 256, "/proc/%lu/exe", pid);
	rc = readlink(&file[0], &link[0], 1023);
	failed = rc < 0;

	rc = failed ? 0 : rc;
	link[rc] = '\0';
	into = link;

	return failed;
}
