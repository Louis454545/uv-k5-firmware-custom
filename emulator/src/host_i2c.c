#include <string.h>

#include "driver/i2c.h"

void I2C_Start(void) {}
void I2C_Stop(void) {}
uint8_t I2C_Read(bool bFinal) { (void)bFinal; return 0xff; }
int I2C_Write(uint8_t Data) { (void)Data; return 0; }

int I2C_ReadBuffer(void *pBuffer, uint8_t Size)
{
	memset(pBuffer, 0xff, Size);
	return Size;
}

int I2C_WriteBuffer(const void *pBuffer, uint8_t Size)
{
	(void)pBuffer;
	return Size;
}
