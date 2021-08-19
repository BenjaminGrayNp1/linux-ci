#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "subunit.h"

extern void gadget(void);
extern unsigned long saved_lr;
static unsigned long exploit[8] = {0};

static void sigill_handler(int signum, siginfo_t *info, void *context)
{
	if (signum != SIGILL) {
		test_failure_detail("rop-user", "wrong signal received");
		exit(1);
	}

	if (info->si_code != ILL_ILLOPN) {
	       test_failure_detail("rop-user", "wrong signal-code received");
	       exit(1);
	}

	test_success("rop-user");
	exit(0);
}

int vuln(void)
{
	unsigned char buf[8];

	/* Save the LR so the gadget can restore it later */
	asm inline ("mflr %0":"=r"(saved_lr)::);

	/* Overwrite the stack-saved LR value with gadget's address */
	for (int i = 0; i < sizeof(exploit) / sizeof(exploit[0]); ++i)
		exploit[i] = (unsigned long)gadget;

	/* Smash that stack! */
	memcpy(buf, exploit, sizeof(exploit));

	return 0;
}

int main(int argc, char *argv[])
{
	struct sigaction sa;

	sa.sa_sigaction = sigill_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGILL, &sa, NULL) == -1) {
		test_failure_detail("rop-user",
				    "cannot install signal handler");
		return 1;
	}

	switch (vuln()) {
	case 666:
		test_failure_detail("rop-user", "ROP attack successful");
		return 1;
	default:
		return 0;
	}
}
