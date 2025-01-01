/*
 * esp32s2_audio main.h
 * 01-23-22 E. Brombaugh
 */

#ifndef __main__
#define __main__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

/* uncomment this for multicore audio */
#define MULTICORE

typedef float float32_t;

#endif
