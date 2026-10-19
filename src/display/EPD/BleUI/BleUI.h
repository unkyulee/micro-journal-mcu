#pragma once

#include <Arduino.h>

// Shared layout for the Bluetooth screens (SEND / BLE KEYBOARD)
// Draw into the frame buffer between BleUI_begin() and BleUI_end().
//
//  Title                         detail
//  ─────────────────────────────────────
//  ● Status
//    body line 0
//    body line 1
//  ─────────────────────────────────────
//  [Key] action   [Key] action

// status dot style
#define BLEUI_DOT_HOLLOW 0 // waiting
#define BLEUI_DOT_FILLED 1 // connected / active

void BleUI_begin();
void BleUI_header(const char *title, const char *detail = "");
void BleUI_status(int dot, const char *status);
void BleUI_text(int line, const char *text);
void BleUI_footer(const char *key1, const char *action1, const char *key2 = nullptr, const char *action2 = nullptr);
void BleUI_end();
