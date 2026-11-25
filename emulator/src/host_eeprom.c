#include <pthread.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

#define EEPROM_SIZE 0x2000

static uint8_t eeprom_data[EEPROM_SIZE];
static pthread_mutex_t eeprom_mutex = PTHREAD_MUTEX_INITIALIZER;
static char eeprom_path[512] = "uvk5-eeprom.bin";
static bool eeprom_ready;

static void put16le(uint16_t addr, uint16_t value)
{
	eeprom_data[addr + 0] = value & 0xff;
	eeprom_data[addr + 1] = value >> 8;
}

static void put32le(uint16_t addr, uint32_t value)
{
	eeprom_data[addr + 0] = value & 0xff;
	eeprom_data[addr + 1] = (value >> 8) & 0xff;
	eeprom_data[addr + 2] = (value >> 16) & 0xff;
	eeprom_data[addr + 3] = value >> 24;
}

static void seed_mr_channel(uint8_t channel, uint32_t frequency, uint8_t band, const char *name, bool narrow)
{
	const uint16_t base = channel * 16;
	put32le(base + 0, frequency);
	put32le(base + 4, 0);
	memset(eeprom_data + base + 8, 0, 8);
	eeprom_data[base + 12] = narrow ? 2 : 0;
	eeprom_data[base + 14] = 4;  /* 12.5 kHz step */

	const uint16_t attr_base = 0x0d60 + (channel & ~7u);
	eeprom_data[attr_base + (channel & 7u)] = (band & 0x0f) | 0xc0;

	uint8_t namebuf[16] = {0};
	snprintf((char *)namebuf, sizeof(namebuf), "%.10s", name);
	memcpy(eeprom_data + 0x0f50 + channel * 16, namebuf, sizeof(namebuf));
}

static void seed_defaults(void)
{
	memset(eeprom_data, 0xff, sizeof(eeprom_data));

	/* Plausible factory calibration values.  Without these, an erased EEPROM
	 * makes the firmware conclude that the battery is critically flat. */
	const uint16_t battery[] = {1285, 1774, 1876, 1952, 2028, 2300};
	for (unsigned i = 0; i < sizeof(battery) / sizeof(battery[0]); i++)
		put16le(0x1f40 + i * 2, battery[i]);

	/* Squelch and RSSI calibration tables.  These are the normal factory
	 * threshold curves rather than an erased 0xff pattern, so scanning and
	 * squelch behave like a calibrated radio. */
	const uint8_t radio_cal[] = {
		0x0a,0x4b,0x53,0x56,0x59,0x5c,0x5f,0x62,0x64,0x66,0xff,0xff,0xff,0xff,0xff,0xff,
		0x05,0x46,0x50,0x53,0x56,0x59,0x5c,0x5f,0x62,0x64,0xff,0xff,0xff,0xff,0xff,0xff,
		0x5a,0x2d,0x29,0x26,0x23,0x20,0x1d,0x1a,0x17,0x14,0xff,0xff,0xff,0xff,0xff,0xff,
		0x64,0x30,0x2d,0x29,0x26,0x23,0x20,0x1d,0x1a,0x17,0xff,0xff,0xff,0xff,0xff,0xff,
		0x5a,0x14,0x11,0x0e,0x0b,0x08,0x03,0x02,0x02,0x02,0xff,0xff,0xff,0xff,0xff,0xff,
		0x64,0x11,0x0e,0x0b,0x08,0x05,0x05,0x04,0x04,0x04,0xff,0xff,0xff,0xff,0xff,0xff,
		0x32,0x68,0x6b,0x6e,0x6f,0x72,0x75,0x77,0x79,0x7b,0xff,0xff,0xff,0xff,0xff,0xff,
		0x28,0x64,0x67,0x6a,0x6c,0x6e,0x71,0x73,0x76,0x78,0xff,0xff,0xff,0xff,0xff,0xff,
		0x41,0x32,0x2d,0x28,0x24,0x21,0x1e,0x1a,0x17,0x16,0xff,0xff,0xff,0xff,0xff,0xff,
		0x46,0x37,0x32,0x2d,0x28,0x25,0x22,0x1e,0x1b,0x19,0xff,0xff,0xff,0xff,0xff,0xff,
		0x5a,0x19,0x0f,0x0a,0x09,0x08,0x07,0x06,0x05,0x04,0xff,0xff,0xff,0xff,0xff,0xff,
		0x64,0x1e,0x14,0x0f,0x0d,0x0c,0x0b,0x0a,0x09,0x08,0xff,0xff,0xff,0xff,0xff,0xff,
		0x6e,0x00,0x78,0x00,0x82,0x00,0x8c,0x00,0xb4,0x00,0xbe,0x00,0xc8,0x00,0xd2,0x00,
		0x32,0x32,0x32,0x64,0x64,0x64,0x8c,0x8c,0x8c,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
		0x32,0x32,0x32,0x64,0x64,0x64,0x8c,0x8c,0x8c,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
		0x5f,0x5f,0x5f,0x69,0x69,0x69,0x87,0x87,0x87,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	};
	memcpy(eeprom_data + 0x1e00, radio_cal, sizeof(radio_cal));

	/* Xtal trim, volume and DAC defaults (same shape as a normal radio). */
	eeprom_data[0x1f88] = 0x00; eeprom_data[0x1f89] = 0x00;
	eeprom_data[0x1f8a] = 0x46; eeprom_data[0x1f8b] = 0x00;
	eeprom_data[0x1f8c] = 0x50; eeprom_data[0x1f8d] = 0x00;
	eeprom_data[0x1f8e] = 0x2c;
	eeprom_data[0x1f8f] = 0x0e;

	/* Valid factory-style frequency-channel records.  A completely erased
	 * EEPROM otherwise falls back to the 400 MHz band edge in both VFOs. */
	eeprom_data[0x0e80] = 202;       /* screen A: VHF frequency channel */
	eeprom_data[0x0e82] = 202;
	eeprom_data[0x0e83] = 205;       /* screen B: UHF frequency channel */
	eeprom_data[0x0e85] = 205;
	eeprom_data[0x0e28 + 2] = 0xc2;  /* valid attrs: VHF band, scanlists 1+2 */
	eeprom_data[0x0e28 + 5] = 0xc5;  /* valid attrs: UHF band, scanlists 1+2 */

	const struct {
		uint16_t base;
		uint32_t frequency;
	} vfos[] = {
		{0x0cc0, 14550000},  /* band 3, VFO A, 145.500 MHz */
		{0x0cd0, 14550000},  /* band 3, VFO B backing record */
		{0x0d20, 43350000},  /* band 6, VFO A backing record */
		{0x0d30, 43350000},  /* band 6, VFO B, 433.500 MHz */
	};
	for (unsigned i = 0; i < sizeof(vfos) / sizeof(vfos[0]); i++) {
		put32le(vfos[i].base + 0, vfos[i].frequency);
		put32le(vfos[i].base + 4, 0);
		memset(eeprom_data + vfos[i].base + 8, 0, 8);
		eeprom_data[vfos[i].base + 14] = 4;  /* 12.5 kHz step */
	}

	/* A small useful factory memory image.  Every entry remains ordinary
	 * EEPROM data, so users can edit or overwrite it from the radio or CPS. */
	seed_mr_channel(0, 14550000, 2, "VHF CALL", false);
	seed_mr_channel(1, 43350000, 5, "UHF CALL", false);
	seed_mr_channel(2, 44600625, 5, "PMR 1", true);
	seed_mr_channel(3, 43998750, 5, "70CM RPT", false);
	seed_mr_channel(4, 14480000, 2, "APRS", false);
}

static void persist_locked(void)
{
	FILE *f = fopen(eeprom_path, "r+b");
	if (!f)
		f = fopen(eeprom_path, "w+b");
	if (!f) {
		fprintf(stderr, "emulator: cannot persist EEPROM image %s\n", eeprom_path);
		return;
	}
	fseek(f, 0, SEEK_SET);
	fwrite(eeprom_data, 1, sizeof(eeprom_data), f);
	fflush(f);
	fclose(f);
}

void HOST_EEPROM_Init(void)
{
	const char *path = getenv("UVK5_EEPROM");
	if (path && *path) {
		snprintf(eeprom_path, sizeof(eeprom_path), "%s", path);
	}

	memset(eeprom_data, 0xff, sizeof(eeprom_data));
	FILE *f = fopen(eeprom_path, "rb");
	if (f) {
		fread(eeprom_data, 1, sizeof(eeprom_data), f);
		fclose(f);
	} else {
		seed_defaults();
		pthread_mutex_lock(&eeprom_mutex);
		persist_locked();
		pthread_mutex_unlock(&eeprom_mutex);
	}
	eeprom_ready = true;
}

const char *HOST_EEPROM_Path(void)
{
	return eeprom_path;
}

void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size)
{
	if (!eeprom_ready)
		HOST_EEPROM_Init();
	pthread_mutex_lock(&eeprom_mutex);
	for (unsigned i = 0; i < Size; i++) {
		uint32_t addr = (uint32_t)Address + i;
		((uint8_t *)pBuffer)[i] = addr < EEPROM_SIZE ? eeprom_data[addr] : 0xff;
	}
	pthread_mutex_unlock(&eeprom_mutex);
}

void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer)
{
	if (!pBuffer || Address >= EEPROM_SIZE)
		return;
	if (!eeprom_ready)
		HOST_EEPROM_Init();
	pthread_mutex_lock(&eeprom_mutex);
	bool changed = false;
	for (unsigned i = 0; i < 8 && Address + i < EEPROM_SIZE; i++) {
		uint8_t v = ((const uint8_t *)pBuffer)[i];
		if (eeprom_data[Address + i] != v) {
			eeprom_data[Address + i] = v;
			changed = true;
		}
	}
	if (changed) {
		/* Update only the eight bytes the firmware wrote.  Whole-image rewrites
		 * would erase independent edits made by another emulator instance. */
		FILE *f = fopen(eeprom_path, "r+b");
		bool recreated = false;
		if (!f) {
			f = fopen(eeprom_path, "w+b");
			recreated = f != NULL;
		}
		if (!f)
			fprintf(stderr, "emulator: cannot persist EEPROM image %s\n", eeprom_path);
		else {
			flock(fileno(f), LOCK_EX);
			if (recreated) {
				fseek(f, 0, SEEK_SET);
				fwrite(eeprom_data, 1, sizeof(eeprom_data), f);
			} else {
				fseek(f, Address, SEEK_SET);
				fwrite(eeprom_data + Address, 1, 8, f);
			}
			fflush(f);
			flock(fileno(f), LOCK_UN);
			fclose(f);
		}
	}
	pthread_mutex_unlock(&eeprom_mutex);
}
