#include "host.h"

void SYSTICK_Init(void)
{
}

void SYSTICK_DelayUs(uint32_t Delay)
{
	/* One-microsecond bus bit-bang delays do not need wall-clock time in the
	 * host model; keeping them would make every register access needlessly slow. */
	if (Delay >= 20)
		HOST_SleepUs(Delay);
}
