/*
 * gc9a01_drvr.h - interface routines for GC9A01 LCD.
 * 08-28-20 E. Brombaugh
 * 10-21-20 E. Brombaugh - updated for f405_codec_v2
 * 10-28-20 E. Brombaugh - updated for f405 feather + tftwing
 * 10-11-21 E. Brombaugh - updated for RP2040
 * 11-26-24 E. Brombaugh - updated for ESP32S3
 */

#ifndef __gc9a01_drvr__
#define __gc9a01_drvr__

#include "gfx.h"

// dimensions for LCD on tiny TFT wing
#define GC9A01_TFTWIDTH 240
#define GC9A01_TFTHEIGHT 240

extern GFX_DRIVER GC9A01_drvr;

void GC9A01_init(void);
void GC9A01_setAddrWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void GC9A01_drawPixel(int16_t x, int16_t y, uint16_t color);
void GC9A01_fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
	uint16_t color);
uint16_t GC9A01_Color565(uint32_t rgb24);
uint32_t GC9A01_ColorRGB(uint16_t color16);
void GC9A01_bitblt(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *buf);
void GC9A01_setRotation(uint8_t m);
void GC9A01_setBacklight(uint8_t ena);

#endif
