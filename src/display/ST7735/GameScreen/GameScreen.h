#pragma once

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

void GameScreen_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f);
void GameScreen_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f);
void GameScreen_keyboard(int key, bool pressed);
