// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013-2015, Michael Ellerman, IBM Corp.
 */

#define _GNU_SOURCE	/* For CPU_ZERO etc. */

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/limits.h>

#include "utils.h"

struct bench_stats
{
	unsigned long count;
	double maximum;
	double minimum;
	double sum;
	double mean;
	double median;
	double stdev;
};

int compare_double(const void* a, const void* b)
{
	return *(const double*)a - *(const double*)b > 0 ? 1 : -1;
}

struct bench_stats get_stats(const double *values, unsigned long count)
{
	struct bench_stats stats = {
		.count = count,
		.sum = 0.0,
		.maximum = 0.0,
		.minimum = 0.0,
		.mean = 0.0,
		.median = 0.0,
		.stdev = 0.0,
	};

	if (count == 0)
		return stats;

	for (unsigned long i = 0; i < count; i++) {
		stats.sum += values[i];
	}

	stats.mean = stats.sum / (double) count;

	if (count == 1) {
		stats.stdev = NAN;
	} else {
		for (size_t i = 0; i < count; i++) {
			double diff = values[i] - stats.mean;
			stats.stdev += diff * diff;
		}
		stats.stdev = stats.stdev / (double) (count - 1);
	}

	double *sorted = malloc(count * sizeof(double));
	memcpy(sorted, values, count * sizeof(double));
	qsort(sorted, count, sizeof(double), compare_double);

	stats.median = sorted[count / 2];
	stats.maximum = sorted[count - 1];
	stats.minimum = sorted[0];

	free(sorted);

	return stats;
}

void print_stats(const double *values, unsigned long count)
{
	unsigned long i = 0;
	for (; i + 6 < count; i += 6) {
		printf("%12.6f  %12.6f  %12.6f  %12.6f  %12.6f  %12.6f\n", values[i], values[i + 1], values[i + 2], values[i + 3], values[i + 4], values[i + 5]);
	}
	for (; i < count; i++) {
		printf("%12.6f  ", values[i]);
	}
	printf("\n");

	struct bench_stats stats = get_stats(values, count);
	printf("samples: %lu\n", stats.count);
	printf("\n");
	printf("    min: %.6f  (-%.6f, %.2f%%)\n", stats.minimum, stats.mean - stats.minimum, 100.0 * (stats.mean - stats.minimum) / stats.mean);
	printf("   mean: %.6f\n", stats.mean);
	printf(" median: %.6f\n", stats.median);
	printf("    max: %.6f  (+%.6f, %.2f%%)\n", stats.maximum, stats.maximum - stats.mean, 100.0 * (stats.maximum - stats.mean) / stats.mean);
	printf("\n");
	printf("  range: %.6f\n", stats.maximum - stats.minimum);
	printf("  stdev: %.6f (%.2f%%)\n", stats.stdev, 100 * stats.stdev / stats.mean);
}

static char auxv[4096];

int read_auxv(char *buf, ssize_t buf_size)
{
	ssize_t num;
	int rc, fd;

	fd = open("/proc/self/auxv", O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -errno;
	}

	num = read(fd, buf, buf_size);
	if (num < 0) {
		perror("read");
		rc = -EIO;
		goto out;
	}

	if (num > buf_size) {
		printf("overflowed auxv buffer\n");
		rc = -EOVERFLOW;
		goto out;
	}

	rc = 0;
out:
	close(fd);
	return rc;
}

void *find_auxv_entry(int type, char *auxv)
{
	ElfW(auxv_t) *p;

	p = (ElfW(auxv_t) *)auxv;

	while (p->a_type != AT_NULL) {
		if (p->a_type == type)
			return p;

		p++;
	}

	return NULL;
}

void *get_auxv_entry(int type)
{
	ElfW(auxv_t) *p;

	if (read_auxv(auxv, sizeof(auxv)))
		return NULL;

	p = find_auxv_entry(type, auxv);
	if (p)
		return (void *)p->a_un.a_val;

	return NULL;
}

int pick_online_cpu(void)
{
	int ncpus, cpu = -1;
	cpu_set_t *mask;
	size_t size;

	ncpus = get_nprocs_conf();
	size = CPU_ALLOC_SIZE(ncpus);
	mask = CPU_ALLOC(ncpus);
	if (!mask) {
		perror("malloc");
		return -1;
	}

	CPU_ZERO_S(size, mask);

	if (sched_getaffinity(0, size, mask)) {
		perror("sched_getaffinity");
		goto done;
	}

	/* We prefer a primary thread, but skip 0 */
	for (cpu = 8; cpu < ncpus; cpu += 8)
		if (CPU_ISSET_S(cpu, size, mask))
			goto done;

	/* Search for anything, but in reverse */
	for (cpu = ncpus - 1; cpu >= 0; cpu--)
		if (CPU_ISSET_S(cpu, size, mask))
			goto done;

	printf("No cpus in affinity mask?!\n");

done:
	CPU_FREE(mask);
	return cpu;
}

bool is_ppc64le(void)
{
	struct utsname uts;
	int rc;

	errno = 0;
	rc = uname(&uts);
	if (rc) {
		perror("uname");
		return false;
	}

	return strcmp(uts.machine, "ppc64le") == 0;
}

int read_sysfs_file(char *fpath, char *result, size_t result_size)
{
	char path[PATH_MAX] = "/sys/";
	int rc = -1, fd;

	strncat(path, fpath, PATH_MAX - strlen(path) - 1);

	if ((fd = open(path, O_RDONLY)) < 0)
		return rc;

	rc = read(fd, result, result_size);

	close(fd);

	if (rc < 0)
		return rc;

	return 0;
}

int read_debugfs_file(char *debugfs_file, int *result)
{
	int rc = -1, fd;
	char path[PATH_MAX];
	char value[16];

	strcpy(path, "/sys/kernel/debug/");
	strncat(path, debugfs_file, PATH_MAX - strlen(path) - 1);

	if ((fd = open(path, O_RDONLY)) < 0)
		return rc;

	if ((rc = read(fd, value, sizeof(value))) < 0)
		return rc;

	value[15] = 0;
	*result = atoi(value);
	close(fd);

	return 0;
}

int write_debugfs_file(char *debugfs_file, int result)
{
	int rc = -1, fd;
	char path[PATH_MAX];
	char value[16];

	strcpy(path, "/sys/kernel/debug/");
	strncat(path, debugfs_file, PATH_MAX - strlen(path) - 1);

	if ((fd = open(path, O_WRONLY)) < 0)
		return rc;

	snprintf(value, 16, "%d", result);

	if ((rc = write(fd, value, strlen(value))) < 0)
		return rc;

	close(fd);

	return 0;
}

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
		int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu,
		      group_fd, flags);
}

static void perf_event_attr_init(struct perf_event_attr *event_attr,
					unsigned int type,
					unsigned long config)
{
	memset(event_attr, 0, sizeof(*event_attr));

	event_attr->type = type;
	event_attr->size = sizeof(struct perf_event_attr);
	event_attr->config = config;
	event_attr->read_format = PERF_FORMAT_GROUP;
	event_attr->disabled = 1;
	event_attr->exclude_kernel = 1;
	event_attr->exclude_hv = 1;
	event_attr->exclude_guest = 1;
}

int perf_event_open_counter(unsigned int type,
			    unsigned long config, int group_fd)
{
	int fd;
	struct perf_event_attr event_attr;

	perf_event_attr_init(&event_attr, type, config);

	fd = perf_event_open(&event_attr, 0, -1, group_fd, 0);

	if (fd < 0)
		perror("perf_event_open() failed");

	return fd;
}

int perf_event_enable(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error while enabling perf events");
		return -1;
	}

	return 0;
}

int perf_event_disable(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error disabling perf events");
		return -1;
	}

	return 0;
}

int perf_event_reset(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error resetting perf events");
		return -1;
	}

	return 0;
}

int using_hash_mmu(bool *using_hash)
{
	char line[128];
	FILE *f;
	int rc;

	f = fopen("/proc/cpuinfo", "r");
	FAIL_IF(!f);

	rc = 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		if (!strcmp(line, "MMU		: Hash\n") ||
		    !strcmp(line, "platform	: Cell\n") ||
		    !strcmp(line, "platform	: PowerMac\n")) {
			*using_hash = true;
			goto out;
		}

		if (strcmp(line, "MMU		: Radix\n") == 0) {
			*using_hash = false;
			goto out;
		}
	}

	rc = -1;
out:
	fclose(f);
	return rc;
}
