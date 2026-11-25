#!/usr/bin/env python3
"""Generate the host-build BK4819 transport shim without editing firmware source."""
from pathlib import Path
import sys

src = Path(sys.argv[1]).read_text()
insert = '''#ifdef HOST_EMULATOR
#include "host.h"

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register)
{
	return HOST_BK4819_ReadRegister((uint8_t)Register);
}

void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data)
{
	HOST_BK4819_WriteRegister((uint8_t)Register, Data);
}

void BK4819_WriteU8(uint8_t Data)
{
	(void)Data;
}

void BK4819_WriteU16(uint16_t Data)
{
	(void)Data;
}

#else
'''
needle = 'static uint16_t BK4819_ReadU16(void)'
if needle not in src:
    raise SystemExit('BK4819 low-level reader not found')
src = src.replace(needle, insert + needle, 1)
marker = '\nvoid BK4819_SetAGC(bool enable)'
if marker not in src:
    raise SystemExit('BK4819 AGC marker not found')
src = src.replace(marker, '\n#endif\n' + marker, 1)
out = Path(sys.argv[2])
out.parent.mkdir(parents=True, exist_ok=True)
src = src.replace('../audio.h', 'audio.h').replace('../bsp/', 'bsp/')
src = src.replace('"bk4819.h"', '"driver/bk4819.h"').replace('"gpio.h"', '"driver/gpio.h"').replace('"system.h"', '"driver/system.h"').replace('"systick.h"', '"driver/systick.h"')
out.write_text(src)
