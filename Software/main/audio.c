/*
 * audio.c - audio via I2S.
 * 03-9-22 E. Brombaugh
 */
 
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "audio.h"
#include "eb_i2s.h"
#include "eb_adc.h"
#include "fx.h"
#include "dsp_lib.h"

static const char* TAG = "audio";
int16_t audio_sl[4];
uint64_t audio_load[3];
int16_t audio_mute_state, audio_mute_cnt;
int16_t *rxbuf = NULL;
int16_t prc[2*FRAMESZ];
uint32_t frq[2] = { 0x02aaaaaa, 0}, phs[2] = { 0, 0};

/*
 * NOTE: ESP32 HW interleaves stereo R/L/R/L with R @ index 0
 * (opposite of most other systems)
 */
 
/*
 * signal level calc
 */
void IRAM_ATTR level_calc(int16_t sig, int16_t *level)
{
	/* rectify */
	sig = (sig < 0) ? -sig : sig;

	/* peak hold - externally reset */
	if(*level < sig)
		*level = sig;
}

/*
 * Audio processing callbacks
 */
void audio_proc_cb(int16_t *dst, int16_t *src, uint32_t len)
{
	uint8_t i;
	int32_t wet, dry, mix;
	
	/* update start time for load calcs */
	audio_load[2] = audio_load[0];
	audio_load[0] = esp_timer_get_time();
	
	len >>= 2;	// len input is in bytes - we need stereo 16-bit samples
	
	/* check input levels */
	for(i=0;i<len;i++)
	{
		level_calc(src[2*i], &audio_sl[0]);
		level_calc(src[2*i+1], &audio_sl[1]);
	}
	
	/* process the selected algorithm */
	fx_proc(prc, src, len);
	
	/* set W/D mix gain */	
	wet = adc_val[0];
	dry = 0xfff - wet;
	
	/* handle output */
	for(i=0;i<len;i++)
	{
		/* W/D with saturation */
		mix = prc[2*i] * wet + src[2*i] * dry;
		dst[2*i] = dsp_ssat16(mix>>12);
		mix = prc[2*i+1] * wet + src[2*i+1] * dry;
		dst[2*i+1] = dsp_ssat16(mix>>12);
		
		/* handle muting */
		switch(audio_mute_state)
		{
			case 0:
				/* pass thru and wait for foreground to force a transition */
				break;
			
			case 1:
				/* transition to mute state */
				mix = (dst[2*i] * audio_mute_cnt);
				dst[2*i] = dsp_ssat16(mix>>9);
				mix = (dst[2*i+1] * audio_mute_cnt);
				dst[2*i+1] = dsp_ssat16(mix>>9);
				audio_mute_cnt--;
				if(audio_mute_cnt == 0)
					audio_mute_state = 2;
				break;
				
			case 2:
				/* mute and wait for foreground to force a transition */
				dst[2*i] = 0;
				dst[2*i+1] = 0;
				break;
			
			case 3:
				/* transition to unmute state */
				mix = (dst[2*i] * audio_mute_cnt);
				dst[2*i] = dsp_ssat16(mix>>9);
				mix = (dst[2*i+1] * audio_mute_cnt);
				dst[2*i+1] = dsp_ssat16(mix>>9);
				audio_mute_cnt++;
				if(audio_mute_cnt == 512)
				{
					audio_mute_state = 0;
					audio_mute_cnt = 0;
				}
				break;
				
			default:
				/* go to legal state */
				audio_mute_state = 0;
				break;
		}
	
		/* check output levels */
		level_calc(dst[2*i], &audio_sl[2]);
		level_calc(dst[2*i+1], &audio_sl[3]);
	}
	
	/* update end timer */
	audio_load[1] = esp_timer_get_time();
}

/*
 * initializer
 */
esp_err_t audio_init(void)
{
	/* init fx */
	fx_init();
	
	/* init state */
	audio_sl[0] = audio_sl[1] = audio_sl[2] = audio_sl[3] = 0;
	audio_load[0] = audio_load[1] = audio_load[2] = 0;
	audio_mute_state = 2;	// start up  muted
	audio_mute_cnt = 0;
	memset(prc, 0, sizeof(int16_t)*FRAMESZ);
	
	/* init I2S w/ audio processing callback */
    ESP_LOGI(TAG, "Initialize I2S");
	i2s_init(audio_proc_cb);
	
	return ESP_OK;
}

/*
 * internal soft mute
 */
void audio_mute(uint8_t enable)
{
    ESP_LOGI(TAG, "audio_mute: start - state = %d, enable = %d", audio_mute_state, enable);
	if((audio_mute_state == 0) && (enable == 1))
	{
		audio_mute_cnt = 512;
		audio_mute_state = 1;
		while(audio_mute_state != 2)
		{
			vTaskDelay(1);
		}
	}
	else if((audio_mute_state == 2) && (enable == 0))
	{
		audio_mute_cnt = 0;
		audio_mute_state = 3;
		while(audio_mute_state != 0)
		{
			vTaskDelay(1);
		}
	}
    ESP_LOGI(TAG, "audio_mute: done");
}

/*
 * get audio level for in/out right/left
 */
int16_t audio_get_level(uint8_t idx)
{
	idx &= 3;
	int16_t result = audio_sl[idx];
	audio_sl[idx] = 0;
	return result;
}
