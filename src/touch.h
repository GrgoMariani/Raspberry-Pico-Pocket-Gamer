#ifndef TOUCH_H_
#define TOUCH_H_

#include <stdint.h>

/*
 * Screen (and therefore touch) resolution. The touch panel is mapped onto the
 * same coordinate system the games draw in: x = 0..TOUCH_SCREEN_X-1 from left
 * to right, y = 0..TOUCH_SCREEN_Y-1 from top to bottom.
 */
#define TOUCH_SCREEN_X 320
#define TOUCH_SCREEN_Y 240

/* number of raw samples the driver collects per touch */
#define TOUCH_SAMPLES_MAX 8

/*
 * Platform independent part of the touchscreen handling: turns a burst of raw
 * XPT2046 samples into one stable screen coordinate. Kept separate from the
 * SPI driver so that it can be unit tested on a PC.
 */

/*
 * raw_x/raw_y: nsamples raw conversions (12 bit value left justified in the
 *              16 bit word read back from the controller).
 * Returns 1 and stores the screen coordinates in x and y when the burst was
 * usable, 0 when there were too few samples or the samples disagreed too much
 * (a finger sliding off the edge, a very light touch, ...).
 */
uint8_t Touch_ComputeCoordinates(const uint16_t * raw_x, const uint16_t * raw_y,
                                 uint8_t nsamples, uint16_t * x, uint16_t * y);

#endif//TOUCH_H_
