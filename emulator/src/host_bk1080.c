#include "bsp/dp32g030/gpio.h"
#include "driver/bk1080.h"
#include "driver/gpio.h"
#include "host.h"

static uint16_t regs[64];
static uint16_t current_frequency;
static bool seek_direction_down;
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

static unsigned fm_station_strength(void)
{
	/* Virtual broadcast carriers in 0.1 MHz units.  Keeping these sparse lets
	 * the firmware's own FM seek logic find real stops instead of locking on
	 * every channel. */
	static const uint16_t carriers[] = {885, 919, 949, 985, 1015, 1047};
	unsigned best = 0;
	for (unsigned i = 0; i < sizeof(carriers) / sizeof(carriers[0]); i++) {
		uint16_t diff = current_frequency > carriers[i] ? current_frequency - carriers[i] : carriers[i] - current_frequency;
		unsigned strength = diff == 0 ? 100 : diff == 1 ? 55 : diff == 2 ? 22 : 0;
		if (strength > best)
			best = strength;
	}
	return best;
}

uint16_t BK1080_ReadRegister(BK1080_Register_t Register)
{
	const unsigned strength = fm_station_strength();
	switch (Register) {
	case BK1080_REG_07: {
		const unsigned jitter = strength ? HOST_Millis() % 35 : HOST_Millis() % 900;
		const uint16_t deviation = strength ? (seek_direction_down ? 4085 + (jitter % 8) : 8 + (jitter % 10)) : 1600 + jitter;
		const uint16_t snr = strength >= 55 ? 8 + (strength / 30) : strength >= 22 ? 3 : 0;
		return (deviation << BK1080_REG_07_SHIFT_FREQD) | (snr << BK1080_REG_07_SHIFT_SNR);
	}
	case BK1080_REG_10: {
		const uint16_t rssi = strength >= 55 ? 34 + strength / 3 : strength >= 22 ? 8 : 2;
		return BK1080_REG_10_AFCRL_NOT_RAILED | rssi;
	}
	default:
		return regs[Register & 0x3f];
	}
}

void BK1080_WriteRegister(BK1080_Register_t Register, uint16_t Value)
{
	regs[Register & 0x3f] = Value;
}

void BK1080_Mute(bool Mute)
{
	BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, Mute ? 0x4201 : 0x0201);
}

bool HOST_BK1080_AudioActive(void)
{
	return !GPIO_CheckBit(&GPIOB->DATA, GPIOB_PIN_BK1080) &&
		!(regs[BK1080_REG_02_POWER_CONFIGURATION] & 0x4000);
}

void BK1080_SetFrequency(uint16_t frequency, uint8_t band)
{
	seek_direction_down = current_frequency && frequency < current_frequency;
	current_frequency = frequency;
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
