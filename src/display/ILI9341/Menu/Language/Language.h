#ifndef Language_h
#define Language_h

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

//
void Language_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f);

//
void Language_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f);

//
void Language_keyboard(char key);

#endif
