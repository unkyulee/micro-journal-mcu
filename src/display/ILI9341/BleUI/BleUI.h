#pragma once

#include <TFT_eSPI.h>

// Shared layout for the Bluetooth screens (SEND / BLE KEYBOARD)
//
//  Title                         detail
//  ─────────────────────────────────────
//  ● Status
//    body line 0
//    body line 1
//    [=========-------]          42%
//  ─────────────────────────────────────
//  [Key] action   [Key] action

// palette (RGB565)
#define BLEUI_BG 0x0000        // #000000
#define BLEUI_TEXT 0xFFFF      // #FFFFFF
#define BLEUI_MUTED 0x9CF3     // #9E9E9E
#define BLEUI_LINE 0x2965      // #2C2C2C
#define BLEUI_CHIP 0x3186      // #303030
#define BLEUI_WAITING 0xF4E1   // #F59E0B amber
#define BLEUI_CONNECTED 0x262B // #22C55E green
#define BLEUI_ACTIVE 0x3C1E    // #3B82F6 blue

void BleUI_clear(TFT_eSPI *tft);
void BleUI_header(TFT_eSPI *tft, const char *title, const char *detail = "");
void BleUI_status(TFT_eSPI *tft, uint16_t color, const char *status);
void BleUI_text(TFT_eSPI *tft, int line, const char *text);
void BleUI_progress(TFT_eSPI *tft, int percent);
void BleUI_footer(TFT_eSPI *tft, const char *key1, const char *action1, const char *key2 = nullptr, const char *action2 = nullptr);

// restore the default font for the other screens
void BleUI_end(TFT_eSPI *tft);
