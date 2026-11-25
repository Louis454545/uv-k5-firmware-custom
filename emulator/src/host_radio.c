#include <stdlib.h>
#include <string.h>

#include "driver/bk4819-regs.h"
#include "host.h"

static uint16_t regs[128];
static uint16_t latched_irqs;
static bool squelch_open;
static bool squelch_state_valid;

typedef struct {
	uint32_t frequency;  /* 10 Hz units */
	uint8_t peak;
	uint16_t on_ms;
	uint16_t period_ms;
} virtual_station_t;

#define MAX_STATIONS 16

static virtual_station_t stations[MAX_STATIONS] = {
	{43350000, 205, 2200, 8500},  /* 433.500 MHz calling/simplex */
	{44600625, 190, 3000, 11000}, /* PMR446 channel 1 */
	{14550000, 185, 2500, 9500},  /* 145.500 MHz */
	{43998750, 165, 1800, 12700},
};
static size_t station_count = 4;
static bool stations_loaded;

static void load_stations(void)
{
	if (stations_loaded)
		return;
	stations_loaded = true;

	const char *env = getenv("UVK5_STATIONS");
	if (!env || !*env)
		return;

	char *copy = strdup(env);
	if (!copy)
		return;
	station_count = 0;
	char *outer = NULL;
	for (char *entry = strtok_r(copy, ",", &outer);
	     entry && station_count < MAX_STATIONS;
	     entry = strtok_r(NULL, ",", &outer)) {
		char *inner = NULL;
		char *mhz_s = strtok_r(entry, ":", &inner);
		char *peak_s = strtok_r(NULL, ":", &inner);
		char *on_s = strtok_r(NULL, ":", &inner);
		char *period_s = strtok_r(NULL, ":", &inner);
		if (!mhz_s)
			continue;
		double mhz = strtod(mhz_s, NULL);
		if (mhz < 1.0 || mhz > 1300.0)
			continue;
		virtual_station_t *s = &stations[station_count++];
		s->frequency = (uint32_t)(mhz * 100000.0 + 0.5);
		s->peak = peak_s ? (uint8_t)strtoul(peak_s, NULL, 10) : 190;
		s->on_ms = on_s ? (uint16_t)strtoul(on_s, NULL, 10) : 2500;
		s->period_ms = period_s ? (uint16_t)strtoul(period_s, NULL, 10) : 9000;
		if (!s->period_ms)
			s->period_ms = 9000;
	}
	free(copy);
}

static uint32_t noise_random(uint32_t x)
{
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

static uint32_t current_frequency(void)
{
	return (uint32_t)regs[BK4819_REG_38] | ((uint32_t)regs[BK4819_REG_39] << 16);
}

static uint8_t rssi_for_frequency(uint32_t frequency)
{
	load_stations();
	uint32_t now = HOST_Millis();
	uint8_t rssi = 18 + (noise_random((frequency >> 3) ^ (now / 90)) % 18);

	for (unsigned i = 0; i < station_count; i++) {
		const virtual_station_t *s = &stations[i];
		uint32_t diff = frequency > s->frequency ? frequency - s->frequency : s->frequency - frequency;
		if (diff > 6000)
			continue;
		bool transmitting = (now % s->period_ms) < s->on_ms;
		if (!transmitting)
			continue;
		uint8_t level = s->peak;
		if (diff > 1250)
			level = (uint8_t)(level * 2 / 3);
		if (diff > 3000)
			level = (uint8_t)(level / 2);
		level += noise_random(now + frequency + i) % 14;
		if (level > rssi)
			rssi = level;
	}
	return rssi;
}

static bool evaluate_squelch(uint8_t rssi)
{
	uint8_t open_threshold = (regs[BK4819_REG_78] >> 8) & 0xff;
	uint8_t close_threshold = regs[BK4819_REG_78] & 0xff;

	if (!squelch_state_valid) {
		squelch_open = rssi >= open_threshold && open_threshold != 0xff;
		squelch_state_valid = true;
		return squelch_open;
	}
	if (!squelch_open && rssi >= open_threshold && (open_threshold != 0 || rssi > 0))
		squelch_open = true;
	else if (squelch_open && rssi < close_threshold)
		squelch_open = false;
	return squelch_open;
}

static void latch_irq(uint16_t bit)
{
	latched_irqs |= bit;
	regs[BK4819_REG_0C] |= 1u;
	regs[BK4819_REG_02] |= bit;
}

static void update_radio_irq(uint8_t rssi)
{
	bool open = evaluate_squelch(rssi);
	static bool last_open;
	static bool initialized;

	if (!initialized) {
		last_open = open;
		initialized = true;
		return;
	}
	if (open != last_open) {
		latch_irq(open ? (1u << 3) : (1u << 2));
		last_open = open;
	}
}

uint16_t HOST_BK4819_ReadRegister(uint8_t reg)
{
	reg &= 0x7f;
	uint32_t frequency = current_frequency();
	uint8_t rssi = rssi_for_frequency(frequency);
	update_radio_irq(rssi);

	switch (reg) {
	case BK4819_REG_67:
		return rssi;
	case BK4819_REG_65:
		return squelch_open ? 18 + (rssi % 11) : 80 + (rssi % 35);
	case BK4819_REG_63:
		return squelch_open ? 8 + (rssi % 9) : 65 + (rssi % 45);
	case BK4819_REG_64:
		return squelch_open ? 900 + (noise_random(HOST_Millis()) % 5000) : 40 + (rssi % 50);
	case BK4819_REG_0D: {
		if (!(regs[BK4819_REG_32] & 1u))
			return 0x8000;
		return (frequency >> 16) & 0x7ff;
	}
	case BK4819_REG_0E:
		return frequency & 0xffff;
	case BK4819_REG_68:
	case BK4819_REG_69:
		/* CTCSS/CDCSS scan: no tone on the virtual noise floor.  A strong
		 * virtual station can be extended later to return a tone here. */
		return 0x8000;
	default:
		return regs[reg];
	}
}

void HOST_BK4819_WriteRegister(uint8_t reg, uint16_t value)
{
	reg &= 0x7f;
	regs[reg] = value;
	if (reg == BK4819_REG_02 && value == 0) {
		latched_irqs = 0;
		regs[BK4819_REG_02] = 0;
		regs[BK4819_REG_0C] &= ~1u;
	}
	if (reg == BK4819_REG_38 || reg == BK4819_REG_39) {
		squelch_state_valid = false;
	}
}
