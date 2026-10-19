#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

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

// status dot style
#define BLEUI_DOT_HOLLOW 0 // waiting
#define BLEUI_DOT_FILLED 1 // connected / active

void BleUI_clear(ST7305_4p2_BW_DisplayDriver *display);
void BleUI_header(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, const char *title, const char *detail = "");
void BleUI_status(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int dot, const char *status);
void BleUI_text(U8G2_FOR_ST73XX *u8, int line, const char *text);
void BleUI_progress(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int percent);
void BleUI_footer(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, const char *key1, const char *action1, const char *key2 = nullptr, const char *action2 = nullptr);
