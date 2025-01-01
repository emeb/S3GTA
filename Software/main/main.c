/*
 * main.c - top level of s3gta audio + touch module
 * 11-18-24 E. Brombaugh
 */

#include <stdio.h>
#include "main.h"
#include "gfx.h"
#include "gc9a01_drvr.h"
#include "touch_ring.h"
#include "menu.h"
#include "eb_wm8731.h"
#include "eb_i2s.h"
#include "eb_adc.h"
#include "button.h"
#include "audio.h"
#include "splash.h"
#ifdef MULTICORE
#include "multicore_audio.h"
#endif

/* tag for logging */
static const char *TAG = "main";

/* build version in simple format */
const char *fwVersionStr = "V0.1";

/* build time */
const char *bdate = __DATE__;
const char *btime = __TIME__;

/*
 * entry point
 */
void app_main(void)
{
	/* start logging of main app */
	printf("\n\nS3GTA - Audio DSP module %s starting\n\r", fwVersionStr);
	printf("Build Date: %s\n\r", bdate);
	printf("Build Time: %s\n\r", btime);
	printf("\n");
	
	/* init ADC */
    ESP_LOGI(TAG, "Init ADC");
	eb_adc_init();
	
	/* init audio */
    ESP_LOGI(TAG, "Init Audio");
#ifdef MULTICORE
	multicore_audio_init();
#else
	audio_init();
#endif
	
	/* init codec */
    ESP_LOGI(TAG, "Init WM8731 Codec");
	eb_wm8731_Init();
	eb_wm8731_Reset();
	eb_wm8731_Mute(0);
	//eb_wm8731_Bypass(1);
	
	/* init button */
    ESP_LOGI(TAG, "Init button");
	button_init();
	
	/* init the touch ring to run in background */
    ESP_LOGI(TAG, "Start Touch Ring handler");
	touch_ring_init();

	/* init the LCD and show splash screen */
    ESP_LOGI(TAG, "Init GFX with GC9A01");
	gfx_init(&GC9A01_drvr);
	gfx_set_forecolor(GFX_BLACK);
	gfx_set_backcolor(0xc7def6);
	gfx_clrscreen();
	gfx_bitblt(0, 80, 160, 80, (uint16_t *)splash_png_565);
	gfx_drawstrctr(120, 110, "S3GTA");
	gfx_drawstrctr(120, 120, "Audio");
	gfx_drawstrctr(120, 130, (char *)fwVersionStr);
	GC9A01_setBacklight(1);
	gfx_set_forecolor(GFX_WHITE);
	gfx_set_backcolor(GFX_BLACK);
	vTaskDelay(pdMS_TO_TICKS(3000));
	
	/* init menu */
    ESP_LOGI(TAG, "Init menu");
	menu_init();
	
	/* report buffer info */
	//i2s_diag();
	
	/* foreground loop just handles menu */
    ESP_LOGI(TAG, "Looping...");
    while(1)
	{
		menu_update();
		
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
