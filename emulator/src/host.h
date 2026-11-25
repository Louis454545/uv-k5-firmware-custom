#ifndef UVK5_HOST_H
#define UVK5_HOST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "driver/keyboard.h"

extern volatile bool gHostShutdown;

void HOST_HW_Init(void);
uint32_t HOST_Millis(void);
void HOST_SleepUs(uint32_t usec);
void HOST_RequestReset(void);

void HOST_SetKey(KEY_Code_t key, bool down);
bool HOST_IsKeyDown(KEY_Code_t key);
KEY_Code_t HOST_PollKey(void);
void HOST_UpdateInputs(void);

void HOST_EEPROM_Init(void);
const char *HOST_EEPROM_Path(void);

void HOST_UART_Start(void);
void HOST_UART_Stop(void);
const char *HOST_UART_Path(void);

uint16_t HOST_BK4819_ReadRegister(uint8_t reg);
void HOST_BK4819_WriteRegister(uint8_t reg, uint16_t value);

#endif
