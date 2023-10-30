/**
 * Copyright (c) 2020, Stephen Murphy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 *  this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LIGHT_WS2812_H_
#define LIGHT_WS2812_H_

#include <avr/io.h>
#include <avr/interrupt.h>
#include "ws2812_config.h"

/*
 *  Structure of the LED array
 *
 * cRGB:     RGB  for WS2812S/B/C/D, SK6812, SK6812Mini, SK6812WWA, APA104, APA106
 * cRGBW:    RGBW for SK6812RGBW
 */

typedef struct cRGB {
    uint8_t g;
    uint8_t r;
    uint8_t b;
} ws2812_RGB_t;

typedef struct {
    uint8_t g;
    uint8_t r;
    uint8_t b;
    uint8_t w;
} ws2812_RGBW_t;

/* User Interface
 *
 * Input:
 *         ledarray:           An array of GRB data describing the LED colors
 *         number_of_leds:     The number of LEDs to write
 *         pinmask (optional): Bitmask describing the output bin. e.g. _BV(PB0)
 *
 * The functions will perform the following actions:
 *         - Set the data-out pin as output
 *         - Send out the LED data
 *         - Wait 50µs to reset the LEDs
 */



void ws2812_setleds     (ws2812_RGB_t  *ledarray, uint16_t number_of_leds);
void ws2812_setleds_pin (ws2812_RGB_t  *ledarray, uint16_t number_of_leds,uint8_t pinmask);
void ws2812_setleds_rgbw(ws2812_RGBW_t *ledarray, uint16_t number_of_leds);

/*
 * Old interface / Internal functions
 *
 * The functions take a byte-array and send to the data output as WS2812 bitstream.
 * The length is the number of bytes to send - three per LED.
 */

void ws2812_sendarray     (uint8_t *array,uint16_t length);
void ws2812_sendarray_mask(uint8_t *array,uint16_t length, uint8_t pinmask);


// ******************************************************************************************************
// patches here courtesy of wurthless elektroniks, for use with ATTinys with very little RAM space
// ******************************************************************************************************

// Macro to pack 4bpp data
#define M_Pack_4bpp(r,g,b) (((g & 0xF) << 8) | ((r & 0xF) << 4) | (b & 0xF))

/**
 * Decode 4bpp data and send it to LEDs.
 * There is no brightness shift, so a value of 0x0F will be sent as 0x0F, not as 0xF0.
 *
 * @param data 16-bit values packed as ---gggg rrrrbbbb
 * @param leds Number of LEDs (NOT data length!)
 */ 
void ws2812_setleds_4bpp(uint16_t* data, uint16_t leds);

#endif /* LIGHT_WS2812_H_ */

#ifdef __cplusplus
}
#endif