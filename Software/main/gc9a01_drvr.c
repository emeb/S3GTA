/*
 * gc9a01_drvr.c - interface routines for GC9A01 LCD.
 * 08-12-19 E. Brombaugh
 * 10-21-20 E. Brombaugh - updated for f405_codec_v2
 * 10-28-20 E. Brombaugh - updated for f405 feather + tftwing
 * 10-11-21 E. Brombaugh - updated for RP2040
 * 11-26-24 E. Brombaugh - updated for ESP32S3
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_gc9a01.h"
#include "gc9a01_drvr.h"

/* tag for logging */
static const char* TAG = "gc9a01_drvr";

//To speed up transfers, every SPI transfer sends a bunch of rows. This define specifies how many. More means more memory use,
//but less overhead for setting up / finishing transfers.
#define BUF_ROWS 16
#define BUFSZ (BUF_ROWS*GC9A01_TFTWIDTH)

/* swap bytes */
#define __REVSH(x) ((((x)>>8)&0xff)|(((x)&0xff)<<8))

/* GPIO definitions for S3GTA */
#define S3GTA_LCD_HOST  SPI2_HOST
#define S3GTA_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#define S3GTA_PIN_NUM_SCLK           40
#define S3GTA_PIN_NUM_MOSI           39
#define S3GTA_PIN_NUM_MISO           21
#define S3GTA_PIN_NUM_LCD_DC         42
#define S3GTA_PIN_NUM_LCD_RST        38
#define S3GTA_PIN_NUM_LCD_CS         41

// Bit number used to represent command and parameter
#define S3GTA_LCD_CMD_BITS           8
#define S3GTA_LCD_PARAM_BITS         8

/* high level driver interface */
GFX_DRIVER GC9A01_drvr =
{
	GC9A01_TFTHEIGHT,
	GC9A01_TFTWIDTH,
	GC9A01_init,
	GC9A01_setRotation,
    GC9A01_Color565,
    GC9A01_ColorRGB,
	GC9A01_fillRect,
	GC9A01_drawPixel,
	GC9A01_bitblt
};

/* LCD state */
static uint16_t _width, _height;
static volatile uint8_t done = 1;
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *drawbuf = NULL;

/* callback when SPI flush complete */
static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
	done = 1;
    return false;
}

/* ----------------------- Public functions ----------------------- */
// Initialization for GC9A01
void GC9A01_init(void)
{
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = S3GTA_PIN_NUM_SCLK,
        .mosi_io_num = S3GTA_PIN_NUM_MOSI,
        .miso_io_num = S3GTA_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = GC9A01_TFTWIDTH * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(S3GTA_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = S3GTA_PIN_NUM_LCD_DC,
        .cs_gpio_num = S3GTA_PIN_NUM_LCD_CS,
        .pclk_hz = S3GTA_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = S3GTA_LCD_CMD_BITS,
        .lcd_param_bits = S3GTA_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_flush_ready,
        .user_ctx = NULL,
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)S3GTA_LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = S3GTA_PIN_NUM_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_LOGI(TAG, "Install GC9A01 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));

    ESP_LOGI(TAG, "Init panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
	
	// alloc draw buffer
    ESP_LOGI(TAG, "Alloc draw buffer");
    drawbuf = heap_caps_malloc(BUFSZ * sizeof(uint16_t), MALLOC_CAP_DMA);
    assert(drawbuf);
	
	/* set dimensions for clipping */
	_width  = GC9A01_TFTWIDTH;
	_height = GC9A01_TFTHEIGHT;
}

// draw single pixel
void GC9A01_drawPixel(int16_t x, int16_t y, uint16_t color)
{
    esp_err_t ret;

	// clip to max display
	if((x < 0) ||(x >= _width) || (y < 0) || (y >= _height)) return;
	
	// send to display
	while(!done);
	done = 0;
	ret = esp_lcd_panel_draw_bitmap(panel_handle, x, y, x+1, y+1, &color);
    assert(ret==ESP_OK);            //Should have had no issues.
}

// fill a rectangle with a single color
void GC9A01_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	uint32_t i, bsz, rsz;
    esp_err_t ret;
	
	// rudimentary clipping (drawChar w/big text requires this)
	if((x >= _width) || (y >= _height)) return;
	if((x + w - 1) >= _width)  w = _width  - x;
	if((y + h - 1) >= _height) h = _height - y;

	/* compute max rows per buffer */
	rsz = BUFSZ/w;
	
	/* prepare source buffer */
	bsz = h > rsz ? rsz : h;
	bsz *= w;
	for(i=0;i<bsz;i++)
		drawbuf[i] = color;
	
	/* send color to display in chunks of max rsz lines */
	while(h)
	{
		bsz = h > rsz ? rsz : h;
		while(!done);
		done = 0;
		ret = esp_lcd_panel_draw_bitmap(panel_handle, x, y, x+w, y+bsz, drawbuf);
		assert(ret==ESP_OK);            //Should have had no issues.
		
		/* update for next pass */
		h -= bsz;
		y += bsz;
	}
}

// Pass 8-bit (each) R,G,B, get back 16-bit packed color
uint16_t GC9A01_Color565(uint32_t rgb24)
{
	uint16_t color16;
	color16 = 	(((rgb24>>16) & 0xF8) << 8) |
				(((rgb24>>8) & 0xFC) << 3) |
				((rgb24 & 0xF8) >> 3);
	
	return __REVSH(color16);
}

// Pass 16-bit packed color, get back 8-bit (each) R,G,B in 32-bit
uint32_t GC9A01_ColorRGB(uint16_t color16)
{
    uint32_t r,g,b;

	color16 = __REVSH(color16);
	
    r = (color16 & 0xF800)>>8;
    g = (color16 & 0x07E0)>>3;
    b = (color16 & 0x001F)<<3;
	return (r<<16) | (g<<8) | b;
}

// bitblt a region to the display
void GC9A01_bitblt(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *buf)
{
    esp_err_t ret;
	while(!done);
	done = 0;
	ret = esp_lcd_panel_draw_bitmap(panel_handle, x, y, x+w, y+h, buf);
	assert(ret==ESP_OK);            //Should have had no issues.
}

// set orientation of display - unused, handled by ESP driver
void GC9A01_setRotation(uint8_t m)
{
}

// backlight on/off
void GC9A01_setBacklight(uint8_t ena)
{
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, ena ? true : false));
}
