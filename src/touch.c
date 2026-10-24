#include "touch.h"

/*
 * ---------------------------------------------------------------------------
 * Touch panel calibration
 * ---------------------------------------------------------------------------
 * Raw values measured on the two ends of each axis of the XPT2046 based panel.
 * They depend on the panel, so if the touch feels off on your hardware, press
 * the very corners of the screen, read the raw values (build with
 * TOUCH_DEBUG_SAMPLES defined) and enter them here.
 *
 * Note that "X" is the axis that ends up in the game's x coordinate: because
 * the panel is mounted rotated by 90 degrees (MADCTL = MV|MX|BGR) it is read
 * from the XPT2046 "Y position" channel (control byte 0x90). The "Y" axis
 * comes from the XPT2046 "X position" channel (control byte 0xD0).
 */
#define TOUCH_MIN_RAW_X 1500
#define TOUCH_MAX_RAW_X 31000
#define TOUCH_MIN_RAW_Y 3276
#define TOUCH_MAX_RAW_Y 30110

/* below this number of samples the burst is considered a glitch, not a touch */
#define TOUCH_MIN_SAMPLES 2

/*
 * A trimmed mean is used instead of a plain average: the lowest and the
 * highest samples of a burst are dropped before averaging. A resistive panel
 * regularly produces single wild readings (the finger is still settling, the
 * pressure is uneven, ...); one such sample drags a plain average by several
 * pixels, which is exactly the "touch is not precise" feeling.
 */
static uint32_t _trimmedMean(const uint16_t * raw, uint8_t nsamples, uint16_t * spread)
{
    uint16_t sorted[TOUCH_SAMPLES_MAX];
    for (uint8_t i = 0; i < nsamples; i++)
        sorted[i] = raw[i];
    /* insertion sort, nsamples is tiny */
    for (uint8_t i = 1; i < nsamples; i++)
    {
        uint16_t value = sorted[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && sorted[j] > value)
        {
            sorted[j+1] = sorted[j];
            j--;
        }
        sorted[j+1] = value;
    }
    /* drop n/4 from both ends (at least one when there are 3 or more) */
    uint8_t drop = nsamples/4;
    if (drop == 0 && nsamples > 2)
        drop = 1;
    uint32_t sum = 0;
    uint8_t kept = 0;
    for (uint8_t i = drop; i < nsamples-drop; i++)
    {
        sum += sorted[i];
        kept++;
    }
    if (kept == 0)
    {
        sum = sorted[0];
        kept = 1;
    }
    if (spread)
        *spread = sorted[nsamples-1-drop] - sorted[drop];
    return sum / kept;
}

/*
 * Maps a raw value onto 0..scale-1. Values outside of the calibrated range are
 * clamped (the old code mapped them to 0, which made the far edge of the screen
 * jump to the opposite edge instead of saturating).
 */
static uint16_t _mapAxis(uint32_t raw, uint32_t minRaw, uint32_t maxRaw, uint32_t scale)
{
    if (maxRaw <= minRaw)
        return 0;
    if (raw <= minRaw)
        return 0;
    if (raw >= maxRaw)
        return (uint16_t)(scale - 1);
    return (uint16_t)((raw - minRaw) * (scale - 1) / (maxRaw - minRaw));
}

uint8_t Touch_ComputeCoordinates(const uint16_t * raw_x, const uint16_t * raw_y,
                                 uint8_t nsamples, uint16_t * x, uint16_t * y)
{
    if (!raw_x || !raw_y || !x || !y)
        return 0;
    if (nsamples == 0 || nsamples > TOUCH_SAMPLES_MAX)
        return 0;
    if (nsamples < TOUCH_MIN_SAMPLES)
        return 0;

    uint32_t rawX = _trimmedMean(raw_x, nsamples, 0);
    uint32_t rawY = _trimmedMean(raw_y, nsamples, 0);

    *x = _mapAxis(rawX, TOUCH_MIN_RAW_X, TOUCH_MAX_RAW_X, TOUCH_SCREEN_X);
    *y = _mapAxis(rawY, TOUCH_MIN_RAW_Y, TOUCH_MAX_RAW_Y, TOUCH_SCREEN_Y);
    return 1;
}
