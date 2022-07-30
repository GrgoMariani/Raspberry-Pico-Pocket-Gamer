#ifndef ILI9341_TOUCH_H_
#define ILI9341_TOUCH_H_

#include <stdint.h>

void ILI9341_T_Init();

bool ILI9341_T_TouchGetCoordinates(uint16_t* x, uint16_t* y);


#endif
