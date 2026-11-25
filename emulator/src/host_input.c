#include <pthread.h>

#include "bsp/dp32g030/gpio.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "host.h"

KEY_Code_t gKeyReading0     = KEY_INVALID;
KEY_Code_t gKeyReading1     = KEY_INVALID;
uint16_t   gDebounceCounter = 0;
bool       gWasFKeyPressed  = false;

static pthread_mutex_t key_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool key_down[KEY_INVALID + 1];

static const KEY_Code_t scan_order[] = {
	KEY_SIDE1, KEY_SIDE2,
	KEY_MENU, KEY_1, KEY_4, KEY_7,
	KEY_UP, KEY_2, KEY_5, KEY_8,
	KEY_DOWN, KEY_3, KEY_6, KEY_9,
	KEY_EXIT, KEY_STAR, KEY_0, KEY_F,
};

void HOST_SetKey(KEY_Code_t key, bool down)
{
	if (key < 0 || key >= KEY_INVALID)
		return;
	pthread_mutex_lock(&key_mutex);
	key_down[key] = down;
	pthread_mutex_unlock(&key_mutex);
}

bool HOST_IsKeyDown(KEY_Code_t key)
{
	if (key < 0 || key >= KEY_INVALID)
		return false;
	pthread_mutex_lock(&key_mutex);
	const bool down = key_down[key];
	pthread_mutex_unlock(&key_mutex);
	return down;
}

KEY_Code_t HOST_PollKey(void)
{
	KEY_Code_t result = KEY_INVALID;
	pthread_mutex_lock(&key_mutex);
	for (unsigned i = 0; i < sizeof(scan_order) / sizeof(scan_order[0]); i++) {
		if (key_down[scan_order[i]]) {
			result = scan_order[i];
			break;
		}
	}
	pthread_mutex_unlock(&key_mutex);
	return result;
}

KEY_Code_t KEYBOARD_Poll(void)
{
	return HOST_PollKey();
}

void HOST_UpdateInputs(void)
{
	if (HOST_IsKeyDown(KEY_PTT))
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_PTT);
	else
		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_PTT);
}
