#include "host.h"

void SYSTEM_DelayMs(uint32_t Delay)
{
	HOST_SleepUs(Delay * 1000);
}

void SYSTEM_ConfigureClocks(void)
{
}
