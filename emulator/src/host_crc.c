#include "driver/crc.h"

void CRC_Init(void) {}

uint16_t CRC_Calculate(const void *pBuffer, uint16_t Size)
{
	const uint8_t *data = (const uint8_t *)pBuffer;
	uint16_t crc = 0;
	for (uint16_t i = 0; i < Size; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (unsigned bit = 0; bit < 8; bit++)
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
	}
	return crc;
}
