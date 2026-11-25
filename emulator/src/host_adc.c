#include "driver/adc.h"

static ADC_CH_MASK configured_channels;

uint8_t ADC_GetChannelNumber(ADC_CH_MASK Mask)
{
	for (uint8_t i = 0; i < 16; i++)
		if (Mask & (1u << i))
			return i;
	return 0;
}

void ADC_Disable(void) {}
void ADC_Enable(void) {}
void ADC_SoftReset(void) {}
uint32_t ADC_GetClockConfig(void) { return 0; }
void ADC_Configure(ADC_Config_t *pAdc) { configured_channels = pAdc ? pAdc->CH_SEL : 0; }
void ADC_Start(void) { (void)configured_channels; }
bool ADC_CheckEndOfConversion(ADC_CH_MASK Mask) { (void)Mask; return true; }

uint16_t ADC_GetValue(ADC_CH_MASK Mask)
{
	/* A charged 1600 mAh pack and no charge current.  These raw counts work
	 * with the seeded calibration in host_eeprom.c and produce roughly 76%. */
	if (Mask & ADC_CH4)
		return 2028;
	if (Mask & ADC_CH9)
		return 100;
	return 0;
}
