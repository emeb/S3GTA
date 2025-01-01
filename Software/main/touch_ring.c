/*
 * touch_ring.c - routines for handling touch ring around display
 * 11-18-24 E. Brombaugh
 */
 
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/touch_pad.h"
#include "math.h"
#include "touch_ring.h"

#define TOUCH_BUTTON_NUM    4
#define TOUCH_CHANGE_CONFIG 0

static const char *TAG = "touch_ring";

static const touch_pad_t button[TOUCH_BUTTON_NUM] = {
    TOUCH_PAD_NUM1,
    TOUCH_PAD_NUM2,
    TOUCH_PAD_NUM3,
    TOUCH_PAD_NUM4,
};

static const uint32_t min[4] = {
	37700,
	33300,
	39600,
	41500,
};
static const uint32_t max[4] = {
	120000,
	110000,
	115000,
	112000,
};

/* state of detector */
uint32_t detect = 0, force = 0;
float angle = 0.0F;

/* definable buttons */
float tr_btn_min[TR_MAXBTNS], tr_btn_max[TR_MAXBTNS], tr_btn_val[TR_MAXBTNS];
uint8_t num_btns, tr_btn_state[TR_MAXBTNS], tr_btn_pstate[TR_MAXBTNS],
	tr_btn_re[TR_MAXBTNS], tr_btn_fe[TR_MAXBTNS];

/*
  Read values sensed at all available touch pads.
 Print out values in a loop on a serial monitor.
 */
static void touch_ring_read_task(void *pvParameter)
{
    uint32_t touch_value, sum;
	float tcal[4], x, y;
	uint8_t i;

    /* Wait for touch sensor init done */
    vTaskDelay(pdMS_TO_TICKS(100));

	/* continuously scan pads and convert to angle + touch detect */
    while(1)
	{
		/* loop over all pads */
		sum = 0;
        for(int i = 0; i < TOUCH_BUTTON_NUM; i++)
		{
			/* read raw data */
            touch_pad_read_raw_data(button[i], &touch_value);
            //printf("T%d: [%4"PRIu32"] ", button[i], touch_value);
			
			/* clamp */
			touch_value = touch_value < min[i] ? min[i] : touch_value;
			touch_value = touch_value > max[i] ? max[i] : touch_value;
			
			/* add up for touch detect */
			sum += touch_value - min[i];
			
			/* calibrate */
			tcal[i] = (float)(touch_value - min[i])/(float)(max[i]-min[i]);
			//printf("%8.4f ", tcal[i]);
        }
		
		/* convert 4 pads cal data to X & Y coords */
		x = tcal[1]-tcal[3];
		y = tcal[2]-tcal[0];
		//printf("%8.4f ", x);
		//printf("%8.4f ", y);
		
		/* convert X & Y to angle in radians */
		angle = atan2f(x, y);
		//printf("%8.4f ", angle);
		
		/* detect absolute touch */
		force = sum;
		detect = force > 32767;
        //printf("sum: %4"PRIu32" ", sum);
        //printf("detect: %4"PRIu32" ", detect);
		
        //printf("\n");
		
		/* process soft buttons if defined */
		for(i=0;i<num_btns;i++)
		{
			if((angle>=tr_btn_min[i]) && (angle < tr_btn_max[i]))
			{
				/* angle is in this button's region */
				if(!tr_btn_state[i])
				{
					/* button was off */
					if(detect)
					{
						tr_btn_state[i] = 1;	// enable button
						tr_btn_re[i] = 1;	// assert rising edge
						tr_btn_fe[i] = 0;	// clear falling edge if any left
					}
				}
				else
				{
					/* button was on */
					if(!detect)
					{
						tr_btn_state[i] = 0;	// disable button
						tr_btn_re[i] = 0;	// clear rising edge if any left
						tr_btn_fe[i] = 1;	// assert falling edge
					}
				}
			}
			else
			{
				/* angle is out of this button's region */
				if(tr_btn_state[i])
				{
					/* button was on */
					tr_btn_state[i] = 0;	// disable button
					tr_btn_re[i] = 0;	// clear rising edge if any left
					tr_btn_fe[i] = 1;	// assert falling edge
				}
			}
		}

		/* wait and repeat */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/*
 * init the touch ring
 */
void touch_ring_init(void)
{
    /* Initialize touch pad peripheral. */
    touch_pad_init();
	
	/* set up four pads */
    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        touch_pad_config(button[i]);
    }
	
    /* Denoise setting at TouchSensor 0. */
    touch_pad_denoise_t denoise = {
        /* The bits to be cancelled are determined according to the noise level. */
        .grade = TOUCH_PAD_DENOISE_BIT4,
        .cap_level = TOUCH_PAD_DENOISE_CAP_L4,
    };
    touch_pad_denoise_set_config(&denoise);
    touch_pad_denoise_enable();
    ESP_LOGI(TAG, "Denoise function init");

    /* Enable touch sensor clock. Work mode is "timer trigger". */
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();
	
	/* init button processing */
	num_btns = 0;

    /* Start task to read values by pads. */
    xTaskCreate(&touch_ring_read_task, "touch_pad_read_task", 4096, NULL, 5, NULL);
}

/*
 * report raw state
 */
uint32_t touch_ring_get(float *ret_angle, uint32_t *ret_force)
{
	*ret_force = force;
	*ret_angle = angle;
	return detect;
}

/*
 * init soft buttons
 */
esp_err_t touch_ring_soft_button_init(uint8_t n, float *min, float *max)
{
	uint8_t i;
	
	/* check for valid number of buttons */
	if(n>TR_MAXBTNS)
		return ESP_FAIL;
	
	num_btns = n;
	
	for(i=0;i<n;i++)
	{
		tr_btn_min[i] = *min++;
		tr_btn_max[i] = *max++;
		tr_btn_val[i] = 0.0F;
		tr_btn_state[i] = 0;
		tr_btn_pstate[i] = 0;
		tr_btn_re[i] = 0;
		tr_btn_fe[i] = 0;
	}
	
	return ESP_OK;
}

/*
 * report soft button state
 */
uint8_t touch_ring_soft_button_get(uint8_t *state, uint8_t *re, uint8_t *fe, float * val)
{
	uint8_t i, result = 0;
	
	/* default values */
	*state = TR_MAXBTNS;
	*re = TR_MAXBTNS;
	*fe = TR_MAXBTNS;
	*val = 0.0F;
	
	/* check for activity */
	for(i=0;i<num_btns;i++)
	{
		//printf("%1d ", tr_btn_state[i]);
		
		/* only detect first active button */
		if(tr_btn_state[i] && (*state == TR_MAXBTNS))
		{
			*state = i;
			*val = (angle-tr_btn_min[i])/(tr_btn_max[i]-tr_btn_min[i]);
			result = 1;
		}
		
		/* only detect first rising edge */
		if(tr_btn_re[i] && (*re == TR_MAXBTNS))
		{
			*re = i;
			tr_btn_re[i] = 0;
			result = 1;
		};
		
		/* only detect first rising edge */
		if(tr_btn_fe[i] && (*fe == TR_MAXBTNS))
		{
			*fe = i;
			tr_btn_fe[i] = 0;
			result = 1;
		}
	}

	return result;
}
