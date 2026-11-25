#define _GNU_SOURCE

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "bsp/dp32g030/gpio.h"
#include "driver/backlight.h"
#include "driver/bk4819-regs.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/uart.h"
#include "host.h"
#include "stb_easy_font.h"

extern void Main(void);
extern void SystickHandler(void);
extern void __real_APP_Update(void);
extern bool gSetting_set_inv;

void __wrap_APP_Update(void)
{
	/* The real firmware deliberately spins in a bare-metal main loop.  Keep
	 * that cooperative structure, but yield a little host CPU between passes;
	 * radio time still comes from the independent 10 ms scheduler thread. */
	__real_APP_Update();
	HOST_SleepUs(150);
}

#define WINDOW_W 640
#define WINDOW_H 760
#define LCD_X 64
#define LCD_Y 48
#define LCD_SCALE 4

typedef struct {
	KEY_Code_t key;
	const char *label;
	SDL_Rect rect;
} button_t;

static button_t buttons[] = {
	{KEY_MENU, "MENU", { 80, 350, 120, 64}},
	{KEY_1,    "1",    {208, 350, 120, 64}},
	{KEY_4,    "4",    {336, 350, 120, 64}},
	{KEY_7,    "7",    {464, 350, 120, 64}},
	{KEY_UP,   "UP",   { 80, 422, 120, 64}},
	{KEY_2,    "2",    {208, 422, 120, 64}},
	{KEY_5,    "5",    {336, 422, 120, 64}},
	{KEY_8,    "8",    {464, 422, 120, 64}},
	{KEY_DOWN, "DOWN", { 80, 494, 120, 64}},
	{KEY_3,    "3",    {208, 494, 120, 64}},
	{KEY_6,    "6",    {336, 494, 120, 64}},
	{KEY_9,    "9",    {464, 494, 120, 64}},
	{KEY_EXIT, "EXIT", { 80, 566, 120, 64}},
	{KEY_STAR, "*",    {208, 566, 120, 64}},
	{KEY_0,    "0",    {336, 566, 120, 64}},
	{KEY_F,    "#",    {464, 566, 120, 64}},
	{KEY_SIDE1,"SIDE1", { 8, 350, 58, 64}},
	{KEY_SIDE2,"SIDE2", { 8, 422, 58, 64}},
	{KEY_PTT,  "PTT",  { 8, 494, 58, 136}},
};

typedef struct {
	KEY_Code_t key;
	uint32_t down_at;
	uint32_t up_at;
	bool down_done;
	bool up_done;
} script_event_t;

static script_event_t script_events[256];
static size_t script_count;
static uint32_t script_start;
static bool script_finished;

static void *firmware_thread(void *arg)
{
	(void)arg;
	Main();
	return NULL;
}

static void *tick_thread(void *arg)
{
	(void)arg;
	while (!gHostShutdown) {
		HOST_SleepUs(10000);
		HOST_UpdateInputs();
		SystickHandler();
	}
	return NULL;
}

static KEY_Code_t key_from_name(const char *name)
{
	if (!strcasecmp(name, "MENU")) return KEY_MENU;
	if (!strcasecmp(name, "UP")) return KEY_UP;
	if (!strcasecmp(name, "DOWN")) return KEY_DOWN;
	if (!strcasecmp(name, "EXIT")) return KEY_EXIT;
	if (!strcasecmp(name, "STAR") || !strcmp(name, "*")) return KEY_STAR;
	if (!strcasecmp(name, "F") || !strcmp(name, "#")) return KEY_F;
	if (!strcasecmp(name, "PTT")) return KEY_PTT;
	if (!strcasecmp(name, "SIDE1")) return KEY_SIDE1;
	if (!strcasecmp(name, "SIDE2")) return KEY_SIDE2;
	if (strlen(name) == 1 && name[0] >= '0' && name[0] <= '9')
		return (KEY_Code_t)(KEY_0 + name[0] - '0');
	return KEY_INVALID;
}

static void parse_script(const char *text)
{
	char *copy = strdup(text);
	char *save = NULL;
	uint32_t at = 4000;
	for (char *tok = strtok_r(copy, ",; ", &save); tok && script_count < 255; tok = strtok_r(NULL, ",; ", &save)) {
		char *colon = strchr(tok, ':');
		uint32_t hold = 180;
		if (colon) {
			*colon = 0;
			hold = (uint32_t)strtoul(colon + 1, NULL, 10);
		}
		KEY_Code_t key = key_from_name(tok);
		if (key == KEY_INVALID)
			continue;
		script_event_t *ev = &script_events[script_count++];
		ev->key = key;
		ev->down_at = at;
		ev->up_at = at + hold;
		at += hold + 180;
	}
	free(copy);
	script_start = HOST_Millis();
}

static KEY_Code_t key_from_sdl(SDL_Keycode sym)
{
	if (sym >= SDLK_0 && sym <= SDLK_9)
		return (KEY_Code_t)(KEY_0 + sym - SDLK_0);
	if (sym >= SDLK_KP_0 && sym <= SDLK_KP_9)
		return (KEY_Code_t)(KEY_0 + sym - SDLK_KP_0);
	switch (sym) {
	case SDLK_m: case SDLK_RETURN: return KEY_MENU;
	case SDLK_UP: return KEY_UP;
	case SDLK_DOWN: return KEY_DOWN;
	case SDLK_ESCAPE: case SDLK_BACKSPACE: return KEY_EXIT;
	case SDLK_ASTERISK: case SDLK_KP_MULTIPLY: return KEY_STAR;
	case SDLK_HASH: case SDLK_f: case SDLK_KP_HASH: return KEY_F;
	case SDLK_SPACE: case SDLK_p: return KEY_PTT;
	case SDLK_F1: case SDLK_q: return KEY_SIDE1;
	case SDLK_F2: case SDLK_w: return KEY_SIDE2;
	default: return KEY_INVALID;
	}
}

static void draw_text(SDL_Renderer *ren, int x, int y, int scale, const char *text)
{
	char vertices[32768];
	int quads = stb_easy_font_print(0, 0, (char *)text, NULL, vertices, sizeof(vertices));
	for (int q = 0; q < quads; q++) {
		float xy[4][4];
		memcpy(xy, vertices + q * 4 * 16, sizeof(xy));
		float minx = xy[0][0], maxx = xy[0][0], miny = xy[0][1], maxy = xy[0][1];
		for (int v = 1; v < 4; v++) {
			if (xy[v][0] < minx) minx = xy[v][0];
			if (xy[v][0] > maxx) maxx = xy[v][0];
			if (xy[v][1] < miny) miny = xy[v][1];
			if (xy[v][1] > maxy) maxy = xy[v][1];
		}
		SDL_Rect r = {
			x + (int)(minx * scale),
			y + (int)(miny * scale),
			(int)((maxx - minx) * scale) + 1,
			(int)((maxy - miny) * scale) + 1
		};
		SDL_RenderFillRect(ren, &r);
	}
}

static int text_width(const char *text, int scale)
{
	return stb_easy_font_width((char *)text) * scale;
}

static void draw_centered_text(SDL_Renderer *ren, const SDL_Rect *rect, const char *text, int scale)
{
	int w = text_width(text, scale);
	int x = rect->x + (rect->w - w) / 2;
	int y = rect->y + (rect->h - 8 * scale) / 2;
	draw_text(ren, x, y, scale, text);
}

static bool point_in_rect(int x, int y, const SDL_Rect *r)
{
	return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void render_lcd(SDL_Renderer *ren)
{
	static uint32_t pixels[LCD_WIDTH * LCD_HEIGHT];
	static bool texture_created;
	static SDL_Texture *texture;

	if (!texture_created) {
		texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, LCD_WIDTH, LCD_HEIGHT);
		texture_created = true;
	}

	uint8_t brightness = BACKLIGHT_GetBrightness();
	unsigned level = 70 + brightness * 18;
	if (level > 255)
		level = 255;

	uint8_t bg_r = 72, bg_g = (uint8_t)level, bg_b = 58;
	uint8_t fg_r = 5, fg_g = 18, fg_b = 8;
	if (brightness == 0) {
		bg_r = 22; bg_g = 45; bg_b = 25;
	}
	bool inverse = gSetting_set_inv;
	uint32_t bg = inverse ? ((fg_r << 16) | (fg_g << 8) | fg_b) : ((bg_r << 16) | (bg_g << 8) | bg_b);
	uint32_t fg = inverse ? ((bg_r << 16) | (bg_g << 8) | bg_b) : ((fg_r << 16) | (fg_g << 8) | fg_b);

	for (unsigned x = 0; x < LCD_WIDTH; x++) {
		for (unsigned bit = 0; bit < 8; bit++) {
			pixels[bit * LCD_WIDTH + x] = (gStatusLine[x] & (1u << bit)) ? fg : bg;
		}
	}
	for (unsigned page = 0; page < FRAME_LINES; page++) {
		for (unsigned x = 0; x < LCD_WIDTH; x++) {
			for (unsigned bit = 0; bit < 8; bit++) {
				unsigned y = (page + 1) * 8 + bit;
				pixels[y * LCD_WIDTH + x] = (gFrameBuffer[page][x] & (1u << bit)) ? fg : bg;
			}
		}
	}

	SDL_UpdateTexture(texture, NULL, pixels, LCD_WIDTH * sizeof(uint32_t));
	SDL_Rect dst = {LCD_X, LCD_Y, LCD_WIDTH * LCD_SCALE, LCD_HEIGHT * LCD_SCALE};
	SDL_RenderCopy(ren, texture, NULL, &dst);
}

static void render_ui(SDL_Renderer *ren)
{
	SDL_SetRenderDrawColor(ren, 18, 20, 22, 255);
	SDL_RenderClear(ren);

	SDL_SetRenderDrawColor(ren, 30, 32, 34, 255);
	SDL_Rect body = {0, 0, WINDOW_W, 760};
	SDL_RenderFillRect(ren, &body);

	SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
	SDL_Rect bezel = {LCD_X - 16, LCD_Y - 18, LCD_WIDTH * LCD_SCALE + 32, LCD_HEIGHT * LCD_SCALE + 36};
	SDL_RenderFillRect(ren, &bezel);
	render_lcd(ren);

	SDL_SetRenderDrawColor(ren, 200, 200, 190, 255);
	draw_centered_text(ren, &(SDL_Rect){0, 8, WINDOW_W, 28}, "UV-K5 FIRMWARE EMULATOR", 2);

	for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		button_t *b = &buttons[i];
		bool down = HOST_IsKeyDown(b->key);
		if (b->key == KEY_PTT)
			SDL_SetRenderDrawColor(ren, down ? 160 : 95, down ? 32 : 22, down ? 28 : 22, 255);
		else
			SDL_SetRenderDrawColor(ren, down ? 78 : 42, down ? 84 : 46, down ? 96 : 52, 255);
		SDL_RenderFillRect(ren, &b->rect);
		SDL_SetRenderDrawColor(ren, 105, 112, 122, 255);
		SDL_RenderDrawRect(ren, &b->rect);
		SDL_SetRenderDrawColor(ren, 220, 224, 230, 255);
		draw_centered_text(ren, &b->rect, b->label, b->rect.w < 70 ? 1 : 2);
	}

	/* Physical radio indicators driven by the same BK4819/GPIO outputs as
	 * the firmware: green RX, red TX, and the side flashlight LED. */
	const uint16_t gpio_out = HOST_BK4819_ReadRegister(BK4819_REG_33);
	const bool green = gpio_out & (0x40u >> BK4819_GPIO6_PIN2_GREEN);
	const bool red = gpio_out & (0x40u >> BK4819_GPIO5_PIN1_RED);
	const bool torch = GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
	SDL_Rect rx_led = {505, 654, 42, 12};
	SDL_Rect tx_led = {553, 654, 42, 12};
	SDL_Rect torch_led = {8, 638, 58, 10};
	SDL_SetRenderDrawColor(ren, green ? 35 : 8, green ? 220 : 45, green ? 70 : 18, 255);
	SDL_RenderFillRect(ren, &rx_led);
	SDL_SetRenderDrawColor(ren, red ? 235 : 55, red ? 35 : 8, red ? 28 : 8, 255);
	SDL_RenderFillRect(ren, &tx_led);
	SDL_SetRenderDrawColor(ren, torch ? 255 : 65, torch ? 210 : 55, torch ? 40 : 20, 255);
	SDL_RenderFillRect(ren, &torch_led);
	SDL_SetRenderDrawColor(ren, 135, 142, 150, 255);
	draw_text(ren, 508, 642, 1, "RX");
	draw_text(ren, 556, 642, 1, "TX");
	draw_text(ren, 9, 628, 1, "LIGHT");

	SDL_SetRenderDrawColor(ren, 135, 142, 150, 255);
	draw_text(ren, 18, 674, 1, "KEYBOARD: M/ENTER MENU  ARROWS UP/DOWN  ESC EXIT  * STAR  F/# FUNCTION");
	draw_text(ren, 18, 690, 1, "SPACE/P PTT  Q/F1 SIDE1  W/F2 SIDE2  MOUSE CLICKS ALSO WORK");
	char info[512];
	snprintf(info, sizeof(info), "EEPROM %s   UART %s", HOST_EEPROM_Path(), HOST_UART_Path());
	draw_text(ren, 18, 716, 1, info);

	SDL_RenderPresent(ren);
}

static bool save_screenshot(SDL_Renderer *ren, const char *path)
{
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, WINDOW_W, WINDOW_H, 32, SDL_PIXELFORMAT_XRGB8888);
	if (!surface)
		return false;
	if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_XRGB8888, surface->pixels, surface->pitch) != 0) {
		SDL_FreeSurface(surface);
		return false;
	}
	int rc = SDL_SaveBMP(surface, path);
	SDL_FreeSurface(surface);
	return rc == 0;
}

#define AUDIO_RATE 48000

static SDL_AudioDeviceID audio_device;
static double tone_phase1, tone_phase2, rx_phase, rx_mod_phase;
static uint32_t noise_state = 0x12345678;

static double bk_tone_frequency(uint16_t encoded)
{
	/* BK4819 tone words are frequency / 10.3244 Hz. */
	return encoded * 10000.0 / 103244.0;
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	(void)userdata;
	int16_t *out = (int16_t *)stream;
	int frames = len / (int)sizeof(*out);
	const uint16_t tone_cfg = HOST_BK4819_ReadRegister(BK4819_REG_70);
	const uint16_t irq = HOST_BK4819_ReadRegister(BK4819_REG_0C);
	const bool audio_path = GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	static int rx_audio = -1;
	if (rx_audio < 0) {
		const char *env = getenv("UVK5_RX_AUDIO");
		rx_audio = !env || strcmp(env, "0") != 0;
	}

	const double f1 = bk_tone_frequency(HOST_BK4819_ReadRegister(BK4819_REG_71));
	const double f2 = bk_tone_frequency(HOST_BK4819_ReadRegister(BK4819_REG_72));
	const double g1 = ((tone_cfg >> BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN) & 0x7f) / 127.0;
	const double g2 = ((tone_cfg >> BK4819_REG_70_SHIFT_TONE2_TUNING_GAIN) & 0x7f) / 127.0;
	const bool tone1 = audio_path && (tone_cfg & BK4819_REG_70_MASK_ENABLE_TONE1) && f1 > 20.0;
	const bool tone2 = audio_path && (tone_cfg & BK4819_REG_70_MASK_ENABLE_TONE2) && f2 > 20.0;
	const bool receiver_open = audio_path && rx_audio && (irq & (1u << BK4819_REG_3F_SHIFT_SQUELCH_FOUND));

	for (int i = 0; i < frames; i++) {
		tone_phase1 += f1 / AUDIO_RATE;
		tone_phase2 += f2 / AUDIO_RATE;
		rx_phase += 540.0 / AUDIO_RATE;
		rx_mod_phase += 3.7 / AUDIO_RATE;
		if (tone_phase1 >= 1.0) tone_phase1 -= floor(tone_phase1);
		if (tone_phase2 >= 1.0) tone_phase2 -= floor(tone_phase2);
		if (rx_phase >= 1.0) rx_phase -= floor(rx_phase);
		if (rx_mod_phase >= 1.0) rx_mod_phase -= floor(rx_mod_phase);

		double sample = 0.0;
		if (tone1)
			sample += sin(2.0 * M_PI * tone_phase1) * g1 * 0.30;
		if (tone2)
			sample += sin(2.0 * M_PI * tone_phase2) * g2 * 0.30;
		if (receiver_open) {
			noise_state = noise_state * 1664525u + 1013904223u;
			const double noise = ((int32_t)(noise_state & 0xffff) - 32768) / 32768.0;
			const double modulation = 0.65 + 0.35 * sin(2.0 * M_PI * rx_mod_phase);
			sample += noise * 0.018;
			sample += sin(2.0 * M_PI * rx_phase) * modulation * 0.026;
		}
		if (sample > 1.0) sample = 1.0;
		else if (sample < -1.0) sample = -1.0;
		out[i] = (int16_t)(sample * 32767.0);
	}
}

static void init_audio(void)
{
	SDL_AudioSpec want = {0};
	want.freq = AUDIO_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = 256;
	want.callback = audio_callback;
	audio_device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
	if (audio_device)
		SDL_PauseAudioDevice(audio_device, 0);
}

static void usage(const char *program)
{
	printf("Usage: %s [options]\n"
		"  --eeprom=PATH          persistent EEPROM image (default ./uvk5-eeprom.bin)\n"
		"  --uart=PATH            serial-port symlink (default /tmp/uvk5-uart)\n"
		"  --no-rx-audio          disable simulated received-audio noise\n"
		"  --stations=SPEC        MHz[:peak[:on_ms[:period_ms]]], comma separated\n"
		"  --script=KEYS          comma list such as F,5 or PTT:1500\n"
		"  --screenshot=PATH      save a BMP screenshot\n"
		"  --screenshot-at=MS     screenshot time after startup (default 6000)\n"
		"  --quit-after-shot      exit after saving the screenshot\n"
		"  --help                 show this help\n", program);
}

int main(int argc, char **argv)
{
	const char *screenshot_path = NULL;
	const char *script = NULL;
	uint32_t screenshot_at = 6000;
	bool headless_quit = false;

	for (int i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "--screenshot=", 13))
			screenshot_path = argv[i] + 13;
		else if (!strncmp(argv[i], "--screenshot-at=", 16))
			screenshot_at = (uint32_t)strtoul(argv[i] + 16, NULL, 10);
		else if (!strncmp(argv[i], "--script=", 9))
			script = argv[i] + 9;
		else if (!strncmp(argv[i], "--eeprom=", 9))
			setenv("UVK5_EEPROM", argv[i] + 9, 1);
		else if (!strncmp(argv[i], "--uart=", 7))
			setenv("UVK5_UART", argv[i] + 7, 1);
		else if (!strncmp(argv[i], "--stations=", 11))
			setenv("UVK5_STATIONS", argv[i] + 11, 1);
		else if (!strcmp(argv[i], "--no-rx-audio"))
			setenv("UVK5_RX_AUDIO", "0", 1);
		else if (!strcmp(argv[i], "--quit-after-shot"))
			headless_quit = true;
		else if (!strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "unknown option: %s\n", argv[i]);
			usage(argv[0]);
			return 2;
		}
	}

	HOST_HW_Init();
	HOST_EEPROM_Init();
	UART_Init();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Window *window = SDL_CreateWindow("UV-K5 emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
	if (!window) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}
	init_audio();
	SDL_Renderer *ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren)
		ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if (!ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		return 1;
	}

	pthread_t fw, tick;
	pthread_create(&fw, NULL, firmware_thread, NULL);
	pthread_detach(fw);
	pthread_create(&tick, NULL, tick_thread, NULL);
	pthread_detach(tick);

	if (script)
		parse_script(script);

	KEY_Code_t mouse_key = KEY_INVALID;
	uint32_t start = HOST_Millis();
	bool shot_done = false;

	while (!gHostShutdown) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT)
				gHostShutdown = true;
			else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
				KEY_Code_t key = key_from_sdl(ev.key.keysym.sym);
				if (key != KEY_INVALID)
					HOST_SetKey(key, ev.type == SDL_KEYDOWN);
			} else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
				for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
					if (point_in_rect(ev.button.x, ev.button.y, &buttons[i].rect)) {
						mouse_key = buttons[i].key;
						HOST_SetKey(mouse_key, true);
						break;
					}
				}
			} else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
				if (mouse_key != KEY_INVALID) {
					HOST_SetKey(mouse_key, false);
					mouse_key = KEY_INVALID;
				}
			}
		}

		uint32_t elapsed = HOST_Millis() - start;
		if (script_count && !script_finished) {
			uint32_t script_elapsed = HOST_Millis() - script_start;
			bool all_done = true;
			for (size_t i = 0; i < script_count; i++) {
				script_event_t *se = &script_events[i];
				if (!se->down_done && script_elapsed >= se->down_at) {
					HOST_SetKey(se->key, true);
					se->down_done = true;
				}
				if (!se->up_done && script_elapsed >= se->up_at) {
					HOST_SetKey(se->key, false);
					se->up_done = true;
				}
				all_done &= se->up_done;
			}
			script_finished = all_done;
		}

		HOST_UpdateInputs();
		render_ui(ren);

		if (screenshot_path && !shot_done && elapsed >= screenshot_at) {
			if (!save_screenshot(ren, screenshot_path))
				fprintf(stderr, "failed to save screenshot to %s\n", screenshot_path);
			shot_done = true;
			if (headless_quit)
				gHostShutdown = true;
		}

		SDL_Delay(16);
	}

	if (mouse_key != KEY_INVALID)
		HOST_SetKey(mouse_key, false);
	if (audio_device)
		SDL_CloseAudioDevice(audio_device);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
