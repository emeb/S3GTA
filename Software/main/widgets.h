/*
 * widgets.c - UI graphics widgets for ESP32S2 Audio
 * 03-20-22 E. Brombaugh
 */

#ifndef __widgets__
#define __widgets__

#include "gfx.h"

/* button structure */
typedef struct
{
	int16_t x, y, r;
	GFX_COLOR on_color, off_color, text_color;
	char text[5];
	uint8_t state;
}  btn_widg;

void widg_bargraphH(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t v);
esp_err_t widg_gradient_init(int16_t width);
void widg_bargraphHG(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t v);
void widg_sliderH(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t v);
void widg_button_render(btn_widg *btn, uint8_t state);

#endif