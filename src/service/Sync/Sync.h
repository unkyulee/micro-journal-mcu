#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// 0 - starting
// 1 - waiting
// 2 - sending
#define SYNC_ERROR -1
#define SYNC_START 0
#define SYNC_STARTED 1
#define SYNC_PROGRESS 2
#define SYNC_COMPLETED 3

// WiFi or the sync url is missing, the screen shows sync_setup_lines()
#define SYNC_NOT_CONFIGURED 4

// Resets the sync state. Ends in SYNC_NOT_CONFIGURED instead of SYNC_START
// when something still has to be set up. Call wifi_config_load() first.
void sync_init();

// What the user has to set up before a network backup can run, one screen
// line per entry. Returns the number of lines, 0 when everything is ready.
int sync_setup_lines(String *lines, int max);
void sync_start_request();


// 
void sync_loop();


// SYNC PROCESS
void sync_start();
bool sync_connect_wifi(JsonDocument &app, const char *ssid, const char *password);
void sync_stop();
void sync_send();
