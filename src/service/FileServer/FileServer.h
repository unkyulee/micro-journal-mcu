#pragma once

#include <Arduino.h>

// Web based file explorer and editor
// Brings up WiFi on demand, serves a single page app that lists, edits,
// downloads and deletes the files in the internal storage.
//
// app["fileserver"] carries the status for the display:
//   message     - progress / error text
//   url         - address to open in the browser
//   mdns        - true when http://microjournal.local is also available
//   ap_ssid     - set when the device runs its own hotspot
//   ap_password - hotspot password
//   login       - true when the page asks for the web password

#define FILESERVER_ERROR -1
#define FILESERVER_IDLE 0
#define FILESERVER_CONNECTING 1
#define FILESERVER_RUNNING 2

// login name used when config.json has web.password set
#define FILESERVER_USER "journal"

// request background service to pick up the request
void fileserver_start_request();
void fileserver_stop_request();

// background task, runs on the secondary core
void fileserver_loop();

//
int fileserver_state();

// text for the menu screen, the same on every display
// fills up to max lines and returns how many were written
int fileserver_status_lines(String *lines, int max);
