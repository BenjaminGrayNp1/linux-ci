// SPDX-License-Identifier: GPL-2.0-only

#define _GNU_SOURCE

#include <fcntl.h>
#include <linux/futex.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

#define ITERATIONS 96

#define NOP_TRACER "nop"
#define FUNCTION_TRACER "function"

#define set_tracer(tracer, name) FAIL_IF_EXIT(write(tracer, name, strlen(name)) != strlen(name));

static double function_times[ITERATIONS];
static double nop_times[ITERATIONS];

int test_ftrace_activation(void)
{
	struct timespec ts_start, ts_end;
	int current_tracer = open("/sys/kernel/debug/tracing/current_tracer", O_WRONLY);
	FAIL_IF_EXIT(current_tracer < 0);

	// Warm up and reset to known state
	set_tracer(current_tracer, FUNCTION_TRACER);
	set_tracer(current_tracer, NOP_TRACER);


	for (int i = 0; i < ITERATIONS; i++) {
		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		set_tracer(current_tracer, FUNCTION_TRACER);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
		function_times[i] = ts_end.tv_sec - ts_start.tv_sec + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;

		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		set_tracer(current_tracer, NOP_TRACER);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
		nop_times[i] = ts_end.tv_sec - ts_start.tv_sec + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;
	}

	printf("Tracer '" FUNCTION_TRACER "':\n");
	print_stats(function_times, ITERATIONS);
	printf("\n");

	printf("Tracer '" NOP_TRACER "':\n");
	print_stats(nop_times, ITERATIONS);
	printf("\n");

	close(current_tracer);
	return 0;
}

int main(void)
{
	test_harness_set_timeout(300);
	return test_harness(test_ftrace_activation, "ftrace_bench");
}
