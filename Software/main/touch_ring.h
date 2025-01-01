/*
 * touch_ring.c - routines for handling touch ring around display
 * 11-18-24 E. Brombaugh
 */
 
#ifndef __touch_ring__
#define __touch_ring__

#define TR_MAXBTNS 10

void touch_ring_init(void);
uint32_t touch_ring_get(float *ret_angle, uint32_t *force);
esp_err_t touch_ring_soft_button_init(uint8_t n, float *min, float *max);
uint8_t touch_ring_soft_button_get(uint8_t *state, uint8_t *re, uint8_t *fe, float * val);

#endif