/*
 * menu.c - menu / UI handler for ESP32S2 Audio
 * 03-07-22 E. Brombaugh
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "main.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "button.h"
#include "gfx.h"
#include "widgets.h"
#include "audio.h"
#include "menu.h"
#include "fx.h"
#include "eb_wm8731.h"
#include "eb_adc.h"
#include "dsp_lib.h"
#include "eb_i2s.h"
#include "touch_ring.h"
#ifdef MULTICORE
#include "multicore_audio.h"
#endif

#define MENU_INTERVAL 50000
#define MENU_MAX_PARAMS (FX_MAX_PARAMS+1)
#define MENU_VU_WIDTH 50
#define MENU_NUMBTNS 2

enum save_flags
{
	SAVE_ACT = 1,
	SAVE_VALUE = 2,
	SAVE_ALGO = 4,
};

static const char* TAG = "menu";
static int16_t menu_item_values[FX_NUM_ALGOS][MENU_MAX_PARAMS];
static uint8_t menu_reset, menu_act_item, menu_save_mask;
static uint8_t menu_value_scoreboard[FX_NUM_ALGOS];
static uint16_t menu_algo, menu_save_counter;
static uint64_t menu_time;
static char txtbuf[32];
static btn_widg menu_btns[MENU_NUMBTNS];
static const char *btn_names[MENU_NUMBTNS] =
{
	"<",
	">",
};

/*
 * load values from NVS
 */
esp_err_t menu_load_state(void)
{
	uint8_t i,j, commit=0;
	
    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
        // NVS partition was truncated and needs to be erased
		ESP_LOGW(TAG, "menu_load_values: Erasing NVS");
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

	/* Open NVS */
    ESP_LOGI(TAG, "menu_load_values: Opening NVS");
    nvs_handle_t my_handle;
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if(err != ESP_OK)
	{
        ESP_LOGW(TAG, "menu_load_values: Error (%s) opening NVS", esp_err_to_name(err));
    }
	else
	{
		/* get algo */
		menu_algo = 0;
		err = nvs_get_u16(my_handle, "menu_algo", &menu_algo);
		ESP_LOGI(TAG, "menu_load_state: menu_algo = %d, err = %s", menu_algo, esp_err_to_name(err));
		if(err == ESP_ERR_NVS_NOT_FOUND)
		{
			ESP_LOGI(TAG, "created menu_algo = %d, err = %s", menu_algo, esp_err_to_name(err));
			err = nvs_set_u16(my_handle, "menu_algo", menu_algo);
			commit = 1;
		}
		else if(menu_algo >= FX_NUM_ALGOS)
		{
			ESP_LOGI(TAG, "Bad menu_algo = %d, resetting to 0", menu_algo);
			menu_algo = 0;
			err = nvs_set_u16(my_handle, "menu_algo", menu_algo);
			commit = 1;
		}
		
		/* get active item */
		menu_act_item = 0;
		err = nvs_get_u8(my_handle, "menu_act_item", &menu_act_item);
		ESP_LOGI(TAG, "menu_load_state: menu_act_item = %d, err = %s", menu_act_item, esp_err_to_name(err));
		if(err == ESP_ERR_NVS_NOT_FOUND)
		{
			err = nvs_set_u8(my_handle, "menu_act_item", menu_act_item);
			ESP_LOGI(TAG, "created menu_act_item = %d, err = %s", menu_act_item, esp_err_to_name(err));
			commit = 1;
		}
		
		/* get params */
		ESP_LOGI(TAG, "menu_load_state: getting params...");
		for(i=0;i<FX_NUM_ALGOS;i++)
		{
			for(j=0;j<MENU_MAX_PARAMS;j++)
			{
				int16_t raw_param = 0;
				sprintf(txtbuf, "pvalues_%2d_%2d", i, j);
				err = nvs_get_i16(my_handle, txtbuf, &raw_param);
				//ESP_LOGI(TAG, "get: %s = %d, err = %s", txtbuf, raw_param, esp_err_to_name(err));
				if(err == ESP_ERR_NVS_NOT_FOUND)
				{
					err = nvs_set_i16(my_handle, txtbuf, raw_param);
					ESP_LOGI(TAG, "created: %s = %d, err = %s", txtbuf, raw_param, esp_err_to_name(err));
					commit = 1;
				}
				menu_item_values[i][j] = raw_param;
			}
		}
		
		if(commit)
		{
			err = nvs_commit(my_handle);
			ESP_LOGI(TAG, "commit: err = %s", esp_err_to_name(err));
		} 
		
        nvs_close(my_handle);
	}

	return ESP_OK;
}

/*
 * write updated state back to NVS
 */
void menu_save_state(void)
{
	esp_err_t err;

	/* Open NVS */
    nvs_handle_t my_handle;
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if(err != ESP_OK)
	{
        ESP_LOGW(TAG, "menu_save_value: Error (%s) opening NVS", esp_err_to_name(err));
    }
	else
	{
		//ESP_LOGI(TAG, "menu_save_value: Success Opening NVS");
		
		/* algo */
		if(menu_save_mask & SAVE_ALGO)
		{
			err = nvs_set_u16(my_handle, "menu_algo", menu_algo);
			//ESP_LOGI(TAG, "set menu_algo = %d, err = %s", menu_algo, esp_err_to_name(err));
		}
		
		/* active param */
		if(menu_save_mask & SAVE_ACT)
		{
			err = nvs_set_u8(my_handle, "menu_act_item", menu_act_item);
			//ESP_LOGI(TAG, "set menu_act_item = %d, err = %s", menu_act_item, esp_err_to_name(err));
		}
		
		/* values */
		if(menu_save_mask & SAVE_ACT)
		{
			/* loop over scoreboard and set all params that have been marked */
			for(int i=0;i<FX_NUM_ALGOS;i++)
			{
				for(int j=0;j<MENU_MAX_PARAMS;j++)
				{
					if(menu_value_scoreboard[i] & (1<<j))
					{
						sprintf(txtbuf, "pvalues_%2d_%2d", i, j);
						err = nvs_set_i16(my_handle, txtbuf, menu_item_values[i][j]);
						//ESP_LOGI(TAG, "set %s = %d, err = %s", txtbuf, menu_item_values[i][j], esp_err_to_name(err));
					}
				}
				menu_value_scoreboard[i] = 0;
			}
		}
		
		menu_save_mask = 0;
		
        // Commit written value.
        err = nvs_commit(my_handle);
        //ESP_LOGI(TAG, "commit: %s", esp_err_to_name(err));
		
        nvs_close(my_handle);
	}
}

/*
 * periodic menu updates to dynamic stuff
 */
void menu_timer_callback(void)
{
	uint8_t i;
	
	/* update load */
	gfx_set_forecolor(GFX_WHITE);
#ifdef MULTICORE
	uint64_t period = mc_period;
	uint64_t duration = mc_duration;
#else
	uint64_t period = audio_load[0] - audio_load[2];
	uint64_t duration = audio_load[1] - audio_load[0];
#endif
	uint64_t load;
	
	/* update load indicator */
	if(period != 0)
	{
		load = 100*duration/period;
		sprintf(txtbuf, "%2llu%% ", load);
		gfx_drawstr(80, 80, txtbuf);
	}
	
	/* update state save */
	if(menu_save_counter != 0)
	{
		menu_save_counter--;
		
		if(menu_save_counter == 0)
		{
			ESP_LOGI(TAG, "menu_timer_callback: Saving State");
			menu_save_state();
			gfx_set_forecolor(GFX_GREEN);
			gfx_fillcircle(196, 83, 3);
			gfx_set_forecolor(GFX_WHITE);
		}
	}
	
	/* update algo params */
	for(i=1;i<4;i++)
	{
		menu_item_values[menu_algo][i] = adc_param[i];
		fx_render_parm(i);
	}
	
	/* update mix */
	widg_sliderH(70, 130, 100, 8, adc_val[0]/41);
	
	/* update VU meters & reset levels */
	widg_bargraphHG(60, 140, MENU_VU_WIDTH, 8, audio_get_level(1)/328);
	widg_bargraphHG(60, 150, MENU_VU_WIDTH, 8, audio_get_level(0)/328);
	widg_bargraphHG(140, 140, MENU_VU_WIDTH, 8, audio_get_level(3)/328);
	widg_bargraphHG(140, 150, MENU_VU_WIDTH, 8, audio_get_level(2)/328);
}

/*
 * schedule save state in the future
 */
void menu_sched_save(uint8_t mask)
{
	ESP_LOGI(TAG, "menu_sched_save: Scheduling save, mask = 0x%02X", mask);
	menu_save_mask |= mask;
	if(mask & SAVE_VALUE)
		menu_value_scoreboard[menu_algo] |= 1<<menu_act_item;
	menu_save_counter = 5000000/MENU_INTERVAL;	 // 5 sec
	gfx_set_forecolor(GFX_RED);
	gfx_fillcircle(196, 83, 3);
	gfx_set_forecolor(GFX_WHITE);
}

/*
 * render menu static items
 */
void menu_render(void)
{
	uint8_t i;
	GFX_RECT rect;
	
	/* set constants */
	gfx_set_forecolor(GFX_WHITE);
	
	/* refresh static items */
	if(menu_reset)
	{
		menu_reset = 0;
		
		/* Algo name & params */
		for(i=0;i<MENU_MAX_PARAMS;i++)
		{		
			/* clear whole line */
			rect.x0 = 41;
			rect.y0 = i*10+10+80;
			rect.x1 = 198;
			rect.y1 = rect.y0+7;
			gfx_clrrect(&rect);
				
			if(i == 0)
			{
				/* algo name */
				sprintf(txtbuf, "Algo: %s", fx_get_algo_name());
				txtbuf[20] = 0;	// max 20 chars 
				gfx_drawstr(41, i*10+10+80, txtbuf);
				
				/* slider */
				widg_sliderH(90, 42, 60, 8, menu_algo * 100/FX_NUM_ALGOS);
				
				/* number */
				sprintf(txtbuf, "%2d/%2d", menu_algo+1, FX_NUM_ALGOS);
				gfx_drawstrctr(120, 30, txtbuf);
			}
			else
			{
				/* new name */
				if(i<fx_get_num_parms()+1)
				{
					gfx_drawstr(41, i*10+10+80, fx_get_parm_name(i-1));
				}
			}
		}
	
		/* save state */
		if(menu_save_counter)
			gfx_set_forecolor(GFX_RED);
		else
			gfx_set_forecolor(GFX_GREEN);
		gfx_fillcircle(196, 83, 3);
		
		/* load % */
		gfx_set_forecolor(GFX_WHITE);
		gfx_drawstr(40, 80, "Load");
		
		/* W/D mix */
		gfx_drawstr(40,   130, "Dry");
		gfx_drawstr(176, 130, "Wet");
		
		/* vu meters labels and boxes */
		gfx_drawstr(40, 141, "il");
		widg_bargraphH(60, 140, MENU_VU_WIDTH, 8, 0);
		gfx_drawstr(40, 151, "ir");
		widg_bargraphH(60, 150, MENU_VU_WIDTH, 8, 0);
		gfx_drawstr(120, 141, "ol");
		widg_bargraphH(140, 140, MENU_VU_WIDTH, 8, 0);
		gfx_drawstr(120, 151, "or");
		widg_bargraphH(140, 150, MENU_VU_WIDTH, 8, 0);
		
	}
}

/*
 * initialize menu handler
 */
void menu_init(void)
{
	uint8_t i;
	float th, btn_min[MENU_NUMBTNS], btn_max[MENU_NUMBTNS];
	
 	/* init menu state */
	ESP_LOGI(TAG, "menu_init: zeroing");
	menu_reset = 1;
	menu_save_mask = 0;
	menu_save_counter = 0;
	for(i=0;i<FX_NUM_ALGOS;i++)
		menu_value_scoreboard[i] = 0;
	
	/* load stored state */
	ESP_LOGI(TAG, "menu_init: try to load state...");
	menu_load_state();
	ESP_LOGI(TAG, "menu_init: state loaded.");
#ifdef MULTICORE
			multicore_audio_select_algo(menu_algo);
#else
			fx_select_algo(menu_algo);
#endif
	audio_mute(0);	// initial unmute after algo selected
	ESP_LOGI(TAG, "menu_init: state loaded act=%d, algo=%d", menu_act_item, menu_algo);
	
	/* create virtual buttons */
	for(i=0;i<MENU_NUMBTNS;i++)
	{
		/* create button @ circular location */
		th = (i*2-1) * 3.1416F/5.0F;
		btn_min[i] = th - .31416F;
		btn_max[i] = th + .31416F;
		//printf("%d, %8.4f, %8.4f\n", i, btn_min[i], btn_max[i]);
		menu_btns[i].x = 120+96.0F*sinf(th);
		menu_btns[i].y = 120-96.0F*cosf(th);
		menu_btns[i].r = 20;
		menu_btns[i].on_color = GFX_RED;
		menu_btns[i].off_color = GFX_BLUE;
		menu_btns[i].text_color = GFX_WHITE;
		sprintf(menu_btns[i].text, "%s", btn_names[i]);
		menu_btns[i].state = 2;	// illegal initial to force render
	}
	
	/* tell touch system about button ranges */
	touch_ring_soft_button_init(MENU_NUMBTNS, btn_min, btn_max);
	
	/* init VU gradient */
	widg_gradient_init(MENU_VU_WIDTH);
	
	/* initial draw of menu */
	gfx_clrscreen();
	menu_render();
	widg_button_render(&menu_btns[0], 0);
	widg_button_render(&menu_btns[1], 0);

	/* set time for next update */
	menu_time = esp_timer_get_time() + MENU_INTERVAL;
}

/*
 * periodic menu update call
 */
void menu_update(void)
{
	uint8_t state, re, fe;
	float val;
	
	/* virtual buttons */
	if(touch_ring_soft_button_get(&state, &re, &fe, &val))
	{
		//printf("%d %d %d %8.4f\n", state, re, fe, val);
		
		uint16_t prev_algo = menu_algo;
		
		/* handle button on */
		if(re != TR_MAXBTNS)
		{
			widg_button_render(&menu_btns[re], 1);
			//printf("button %d on\n", fe);
		}
		
		/* handle button off */
		if(fe != TR_MAXBTNS)
		{
			widg_button_render(&menu_btns[fe], 0);
			//printf("button %d off\n", fe);
			
			/* update menu_algo */
			if(fe == 1)
			{
				if(menu_algo < FX_NUM_ALGOS-1)
					menu_algo++;
			}
			else if(fe == 0)
			{
				if(menu_algo > 0)
					menu_algo--;
			}	
		}
		
		if(prev_algo != menu_algo)
		{
			//printf("Algo changed %d -> %d\n", prev_algo, menu_algo);
			ESP_LOGI(TAG, "menu_update: algo = %d", menu_algo);
			audio_mute(1);
			menu_sched_save(SAVE_ALGO);
#ifdef MULTICORE
			multicore_audio_select_algo(menu_algo);
#else
			fx_select_algo(menu_algo);
#endif
			menu_reset = 1;
			menu_render();
			audio_mute(0);
		}
	}
		
	/* periodic updates in foreground to avoid conflicts */
	if(esp_timer_get_time() >= menu_time)
	{
		menu_time = esp_timer_get_time() + MENU_INTERVAL;
		menu_timer_callback();
	}
}
