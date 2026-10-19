#pragma once

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

void SendScreen_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f);
void SendScreen_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f);
void SendScreen_keyboard(int key);
