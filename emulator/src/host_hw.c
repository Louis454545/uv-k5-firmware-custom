#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "bsp/dp32g030/gpio.h"
#include "driver/gpio.h"
#include "host.h"

volatile bool gHostShutdown;
static struct timespec host_start;

static void HOST_Fail(const char *what)
{
	fprintf(stderr, "uvk5 emulator: %s: %s\n", what, strerror(errno));
	exit(1);
}

void HOST_HW_Init(void)
{
	const uintptr_t base = 0x40000000ULL;
	const size_t size = 0x00100000;
	void *p = mmap((void *)base, size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (p == MAP_FAILED)
		HOST_Fail("cannot map virtual DP32G030 peripheral space");
	if (p != (void *)base) {
		fprintf(stderr, "uvk5 emulator: unexpected peripheral mapping %p\n", p);
		exit(1);
	}

	/* Inputs on the radio are pulled up and active low. */
	GPIOA->DATA = 0x0000ffff;
	GPIOB->DATA = 0x0000ffff;
	/* PTT is an idle-high input and the BK4819 serial pins idle high.  The
	 * flashlight and audio-path outputs start low rather than inheriting a
	 * nonsensical all-ones reset state. */
	GPIOC->DATA = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 5);

	clock_gettime(CLOCK_MONOTONIC, &host_start);
}

uint32_t HOST_Millis(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	time_t sec = now.tv_sec - host_start.tv_sec;
	long nsec = now.tv_nsec - host_start.tv_nsec;
	if (nsec < 0) {
		sec--;
		nsec += 1000000000L;
	}
	return (uint32_t)(sec * 1000 + nsec / 1000000);
}

void HOST_SleepUs(uint32_t usec)
{
	if (usec == 0)
		return;
	struct timespec ts = {
		.tv_sec = usec / 1000000,
		.tv_nsec = (long)(usec % 1000000) * 1000,
	};
	nanosleep(&ts, NULL);
}

void HOST_RequestReset(void)
{
	/* Firmware resets are used after unlocking/flashing-related actions.  A
	 * source-level emulator cannot unwind Main() portably, so treat this as a
	 * clean emulator shutdown rather than silently ignoring the request. */
	gHostShutdown = true;
}
