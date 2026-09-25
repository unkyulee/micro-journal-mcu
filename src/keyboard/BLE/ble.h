#ifndef Keyboard_Nimble_h
#define Keyboard_Nimble_h

#include "NimBLEDevice.h"

// Initialize BLE DEVICE
void ble_init(const char* name);

// Forget all bonded keyboards (unpair)
void ble_forget();

// BLE keyboard is enabled by the user
bool ble_enabled();

//
void ble_setup(const char* adName);
void ble_loop();

#endif