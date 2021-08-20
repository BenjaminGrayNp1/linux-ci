#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "subunit.h"

#define SPRN_DEXCR_USER 812

/* Copy-pasta from: arch/powerpc/kernel/dexcr.c */
#define DEXCR_PROn_SH(aspect)	(31 - (aspect)) /* Problem State (Userspace) */
#define DEXCR_ASPECT_SBHE	0 /* Speculative Branch Hint Enable */
#define DEXCR_ASPECT_IBRTPD	3 /* Indirect Branch Recurrent Target Prediction Disable */
#define DEXCR_ASPECT_SRAPD	4 /* Subroutine Return Address Prediction Disable */
#define DEXCR_ASPECT_NPHIE	5 /* Non-Privileged Hash Instruction Enable */

#define SYSFS_DEXCR_ASP_SBHE	"/proc/sys/kernel/speculative-branch-hint-enable"
#define DEBUGFS_DEXCR_ASP_NPHIE	"/sys/kernel/debug/powerpc/non-privileged-hash-insn-enable"

static inline unsigned long getdexcr(void)
{
	unsigned long val;
	asm volatile("mfspr %0,%1"
		     : "=r" (val)
		     : "i" (SPRN_DEXCR_USER));
	return val;
}


static int set_dexcr_via_prctl(unsigned long asp, unsigned long val)
{
	if (prctl(PR_PPC_SET_DEXCR_ASPECT, asp, val, 0, 0)) {
		test_failure_detail("dexcr-cntrls", "prctl() call failed");
		return 1;
	}

	return 0;
}

static int check_dexcr_aspect(unsigned long asp)
{
	switch (asp) {
	case DEXCR_ASPECT_SBHE:
	case DEXCR_ASPECT_IBRTPD:
	case DEXCR_ASPECT_SRAPD:
	case DEXCR_ASPECT_NPHIE:
	      break;
	default:
	      test_failure_detail("dexcr-cntrls",
			          "check_dexcr_aspect: invalid aspect");
	      exit(1); /* No point in continuing - test code is junk. */
	}

	return (!!(getdexcr() & (1ul << DEXCR_PROn_SH(asp))));
}

static int check_cap_sysadmin(void)
{
	int rc = 0;
	cap_t caps;
	cap_flag_value_t val;

	caps = cap_get_proc();
	if (!caps) {
		test_failure_detail("dexcr-cntrls", "failed to get caps");
		rc = 1;
		goto out_free_caps;
	}

	if (cap_get_flag(caps, CAP_SYS_ADMIN, CAP_EFFECTIVE, &val)) {
		test_failure_detail("dexcr-cntrls", "failed to get caps flag");
		rc = 1;
	}

	if (val != CAP_SET) {
		test_failure_detail("dexcr-cntrls",
				    "require sys-admin (try w/ 'sudo') caps");
		rc = 1;
	}

out_free_caps:
	if (cap_free(caps)) {
		test_failure_detail("dexcr-cntrls", "failed to release caps");
		return 1;
	}

	return rc;
}

static int set_nphie_via_debugfs(const char *val)
{
	int fd;
	ssize_t rc;
	size_t len;

	fd = open(DEBUGFS_DEXCR_ASP_NPHIE, O_WRONLY);
       	if (fd < 0) {
		test_failure_detail("dexcr-cntrls",
				    "failed to open: "DEBUGFS_DEXCR_ASP_NPHIE);
		return 1;
	}

	len = strlen(val);
	rc = write(fd, val, len); 
	if (rc != len || rc == -1) {
		test_failure_detail("dexcr-cntrls",
				    "failed to write: "DEBUGFS_DEXCR_ASP_NPHIE);
		close(fd);
		return 1;
	}

	close(fd);

	return 0;
}

static int set_sbhe_via_sysfs(const char *val)
{
	int fd;
	ssize_t rc;
	size_t len;

	fd = open(SYSFS_DEXCR_ASP_SBHE, O_WRONLY);
       	if (fd < 0) {
		test_failure_detail("dexcr-cntrls",
				    "failed to open: "SYSFS_DEXCR_ASP_SBHE);
		return 1;
	}

	len = strlen(val);
	rc = write(fd, val, len); 
	if (rc != len || rc == -1) {
		test_failure_detail("dexcr-cntrls",
				    "failed to write: "SYSFS_DEXCR_ASP_SBHE);
		close(fd);
		return 1;
	}

	close(fd);

	return 0;
}

/* Clear all the aspects in the DEXCR and disable overrides */
static int test_clear_dexcr(void)
{
	if (set_sbhe_via_sysfs("-1")) /* Disable override */
		return 1;

	if (set_nphie_via_debugfs("0"))
		return 1;

	if (set_dexcr_via_prctl(PR_PPC_DEXCR_ASPECT_ALL, 0))
		return 1;

	if (check_dexcr_aspect(DEXCR_ASPECT_SBHE) ||
	    check_dexcr_aspect(DEXCR_ASPECT_IBRTPD) ||
	    check_dexcr_aspect(DEXCR_ASPECT_SRAPD) ||
	    check_dexcr_aspect(DEXCR_ASPECT_NPHIE)) {
		test_failure_detail("dexcr-cntrls",
				    "didn't properly clear all DEXCR aspects");
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	if (check_cap_sysadmin())
		return 1;

	if (test_clear_dexcr())
		return 1;

	/* TODO more tests: mixing sysctl,debugfs,and prctl; threads, etc. */

	test_success("dexcr-cntrls");

	return 0;
}
