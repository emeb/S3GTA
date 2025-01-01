/*
 * eb_adc.c - my high-level ADC driver. Mimics background ADC operation.
 * 01-16-22 E. Brombaugh
 */
 
#include <stdio.h>
#include "main.h"
//#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "hal/i2s_hal.h"
#include "hal/i2s_types.h"
#include <soc/sens_reg.h>
#include <soc/sens_struct.h>
#include <soc/spi_reg.h>
#include <soc/spi_struct.h>
#include "eb_adc.h"

static const char* TAG = "eb_adc";
static uint8_t adc_idx, adc_hyst_state, adc_param_idx;
//static int16_t adc_hyst_val;
int32_t adc_iir[ADC_NUMVALS];
volatile int16_t adc_val[ADC_NUMVALS], adc_param[ADC_NUMPARAMS];

#define GET_UNIT(x)			((x>>3) & 0x1)
#define TIMES				8

/*
 * channel mapping:
 * POT 1 - CV4 - GPIO8 - CHL7
 * POT 2 - CV3 - GPIO7 - CHL6
 * POT 3 - CV1 - GPIO5 - CHL4
 * POT 4 - CV2 - GPIO6 - CHL5
 */
const uint8_t adc_chls[ADC_NUMVALS] =
{
	ADC_CHANNEL_7,
	ADC_CHANNEL_6,
	ADC_CHANNEL_4,
	ADC_CHANNEL_5,	
};

/*
 * low-level: set channel, wait for ready & start conversion
 * cuts time from 45us to 15us
 */
void IRAM_ATTR my_adc1_get_raw_begin(uint8_t chl)
{
	SENS.sar_meas1_ctrl2.sar1_en_pad = (1 << chl);
	/* note that this reg is undocumented! */
	while (HAL_FORCE_READ_U32_REG_FIELD(SENS.sar_slave_addr1, meas_status) != 0) {}
	SENS.sar_meas1_ctrl2.meas1_start_sar = 0;
	SENS.sar_meas1_ctrl2.meas1_start_sar = 1;
}

/*
 * low-level: wait for eoc & fetch result
 */
int16_t IRAM_ATTR my_adc1_get_raw_end(void)
{
    while (SENS.sar_meas1_ctrl2.meas1_done_sar == 0);
    return 4095-HAL_FORCE_READ_U32_REG_FIELD(SENS.sar_meas1_ctrl2, meas1_data_sar);
}

/*
 * IIR filter for 12-bit ADC values 
 */
#define IIR_COEF 4
inline int16_t adc_IIR_filter(int32_t *filt_state, int16_t in)
{
	*filt_state += ((in<<IIR_COEF) - *filt_state )>>IIR_COEF;
	return *filt_state >> (IIR_COEF);
}

/*
 * ADC timer callback
 */
void IRAM_ATTR adc_timer_callback(void *arg)
{
	/* set ISR active flag */
	//gpio_set_level(DIAG_1, 1);
	
	/* don't wait for conversion - get previous result */
	adc_val[adc_idx] = adc_IIR_filter(&adc_iir[adc_idx], my_adc1_get_raw_end());

#if 0
	/* handle hysteresis */
	if(adc_idx<(ADC_NUMVALS-1))
	{
		/* update parameter */
		switch(adc_hyst_state)
		{
			case 0:	/* reset */
				adc_hyst_val = adc_val[1];
				adc_hyst_state = 1;
				break;
			
			case 1: /* locked & waiting for unlock */
				if(abs(adc_val[1]-adc_hyst_val) > 200)
					adc_hyst_state = 2;
				break;
			
			case 2:	/* tracking */
				adc_param[adc_param_idx] = adc_val[1];
				break;
		}
	}
#else
	/* just pass thru to params */
	adc_param[adc_idx] = adc_val[adc_idx];
#endif

	/* advance channel */
	adc_idx = (adc_idx + 1)%ADC_NUMVALS;
	
	/* start next conversion */
	my_adc1_get_raw_begin(adc_chls[adc_idx]);
	
	/* clear ISR active flag */
	//gpio_set_level(DIAG_1, 0);
}

esp_err_t eb_adc_init(void)
{
	uint8_t i;
	esp_timer_handle_t timer_handle;
	
	/* setup state */
	adc_idx = 0;
	adc_hyst_state = 0;
	adc_param_idx = 0;
	
	/* single conversions w/ timer */
#if 0
	/* OLD API is deprecated */
    ESP_LOGI(TAG, "PIO mode w/ deprecated driver");
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(CV0, ADC_ATTEN_DB_6);
	adc1_config_channel_atten(CV1, ADC_ATTEN_DB_6);
	adc1_get_raw(CV0);
#else
	/* New one-shot API */
    ESP_LOGI(TAG, "PIO mode w/ new one-shot driver");
	/* init ADC1 */
	adc_oneshot_unit_handle_t adc1_handle;
	adc_oneshot_unit_init_cfg_t init_config1 = {
		.unit_id = ADC_UNIT_1,
		.ulp_mode = ADC_ULP_MODE_DISABLE,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
	
	/* set up channels: GPIO 5-8 are ADC1 chls 4-7 */
	adc_oneshot_chan_cfg_t config = {
		.bitwidth = SOC_ADC_RTC_MAX_BITWIDTH,	// defaults to 12 bit on S3
		.atten = ADC_ATTEN_DB_6,
	};
	for(i=0;i<ADC_NUMVALS;i++)
		ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, adc_chls[i], &config));

	/* read a channel to start ADC */
	int dummy;
	ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, adc_chls[0], &dummy));
#endif
	
	/* Timer for controlling ADC sampling intervals */
	esp_timer_create_args_t timer_args = {
		.callback = adc_timer_callback,
		.arg = NULL,
		.name = "adc_timer",
		.skip_unhandled_events = true
	};
	esp_timer_create(&timer_args, &timer_handle);
	esp_timer_start_periodic(timer_handle, 1000);
	
	return ESP_OK;
}

/*
 * set adc[1] parameter destination
 */
void eb_adc_setactparam(uint8_t idx)
{
	if(idx>=ADC_NUMPARAMS)
		return;
	
	if(idx != adc_param_idx)
	{
		/* this has to be atomic */
		taskDISABLE_INTERRUPTS();
		adc_param_idx = idx;
		adc_hyst_state = 0;
		taskENABLE_INTERRUPTS();
	}
}

/*
 * set adc[1] parameter value
 */
void eb_adc_setparamval(uint8_t idx, int16_t val)
{
	if(idx>=ADC_NUMPARAMS)
		return;
	
	adc_param[idx] = val;
}

/*
 * force adc[1] active parameter hysteresis to track live value
 */
void eb_adc_forceactparam(void)
{
	adc_hyst_state = 2;
}
