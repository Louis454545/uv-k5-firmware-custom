#include "bsp/dp32g030/gpio.h"
#include "driver/bk1080.h"
#include "driver/gpio.h"

static uint16_t regs[64];
static bool initialized;

uint16_t BK1080_BaseFrequency;
uint16_t BK1080_FrequencyDeviation;

void BK1080_Init0(void)
{
	BK1080_Init(0, 0);
}

uint16_t BK1080_GetFreqLoLimit(uint8_t band)
{
	static const uint16_t lim[] = {875, 760, 760, 640};
	return lim[band % 4];
}

uint16_t BK1080_GetFreqHiLimit(uint8_t band)
{
	static const uint16_t lim[] = {1080, 1080, 900, 760};
	return lim[band % 4];
}

void BK1080_Init(uint16_t freq, uint8_t band)
{
	if (freq) {
		GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BK1080);
		if (!initialized) {
			regs[BK1080_REG_02_POWER_CONFIGURATION] = 0x0201;
			regs[BK1080_REG_05_SYSTEM_CONFIGURATION2] = 0x0a1f;
			initialized = true;
		}
		BK1080_SetFrequency(freq, band);
	} else {
		regs[BK1080_REG_02_POWER_CONFIGURATION] = 0x0241;
		GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_BK1080);
	}
}

uint16_t BK1080_ReadRegister(BK1080_Register_t Register)
{
	return regs[Register & 0x3f];
}

void BK1080_WriteRegister(BK1080_Register_t Register, uint16_t Value)
{
	regs[Register & 0x3f] = Value;
}

void BK1080_Mute(bool Mute)
{
	BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, Mute ? 0x4201 : 0x0201);
}

void BK1080_SetFrequency(uint16_t frequency, uint8_t band)
{
	uint16_t channel = frequency - BK1080_GetFreqLoLimit(band);
	uint16_t regval = BK1080_ReadRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2);
	regval = (regval & ~(0x3u << 6)) | ((band & 0x3u) << 6);
	BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, regval);
	BK1080_WriteRegister(BK1080_REG_03_CHANNEL, channel | 0x8000);
	BK1080_FrequencyDeviation = 24 + (frequency % 17);
}

void BK1080_GetFrequencyDeviation(uint16_t Frequency)
{
	BK1080_BaseFrequency = Frequency;
	BK1080_FrequencyDeviation = 24 + (Frequency % 17);
}
