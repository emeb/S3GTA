# S3GTA
A Eurorack effects module based on the ESP32S3

<img src="doc/s3gta_frong.jpg" width="300" />

## Abstract
This project builds on my previous ESP32S2 design:

https://github.com/emeb/ESP32S2_Audio

taking it further with a circular LCD, touch sensing and more CV inputs. It's
based on an ESP32S3 with 4MB flash and 2MB PSRAM in the package, as well as
dual CPU cores to potentially provide more DSP bandwidth.

## Hardware
The hardware consists of two PCBs - a front-panel board that provides the main
Eurorack user interface including a 240x240 circular LCD with a circular touch
sensor surrounding it and the MCU & audio codec, coupled with a back board that
includes the Eurorack power as well as audio and CV interfaces for the front-panel.

The front-panel PCB contains the following components:
* ESP32-S3FH4R2 MCU with 4MB flash and 2MB PSRAM integrated in the package
* WM8731 I2S audio codec
* 4 pad proportional circular touch sensor
* 4 CVs, stereo in/out and sync + button interfaces to back board.
* optional USB and Qwicc connectors

The back board has
* Standard Eurorack 16-pin shrouded power connector
* Four +/-5V CV inputs and associated analog signal conditioning
* Four offset pots for the CVs
* Two +/-7V Audio inputs and associated analog signal conditioning 
* Two +/-7V Audio outputs and associated analog drivers
* Tap tempo button
* Sync input jack

## Firmware
A demonstration firmware is included that exercises all of the capabilities of
the system, including the LCD, UI button, CV inputs and stereo audio I/O. It is
a basic multi-effects unit that supports a complement of audio DSP algorithms
that are easily extended by adding standardized modules to a data structure.
As provided here just three algorithms are available:
* Simple pass-thru with no processing
* Simple gain control
* Lowpass, Highpass and Bandpass filters
* Basic "clean delay" with crossfaded deglitching during delay changes.

Other algorithms have been tested including phasers, flangers, frequency shifters,
resampling delays and reverbs, but these are not publicly released at this time.

## Findings
The ESP32Ss is not bad for doing audio and the I2S driver in ESP-IDF V5.2.3 does
include good support for realtime audio processing. There are a few hiccups in
the way it works that are commented in the source code, but overall it does a
good job of supporting audio effects processing.

I found that overall the DSP on the ESP32S3 was usable, although not as
efficient as on ARM processors. Some algorithms showed significantly higher CPU
loading on the ESP32S3 than on ARM Cortex M7, even accounting for clock speed
differences. Studying the disassembly of the object code suggests that the full
DSP capability of the ESP32S3 Xtensa CPU ISA isn't being brought to bear by the
GCC compiler - for example the available multiply-accumulate instructions were
not used in cases where they would have reduced overall instruction counts.


