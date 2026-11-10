#pragma once

#include <Arduino.h>

//
// Idle timers of the word processor, shared by every display.
//
// Each display's word processor used to carry its own copy of the autosave
// timer and the sleep timer. They only differ in how long they wait, so the
// logic lives here and a display just says how long:
//
//   editor_autosave(3000);      // in the render loop
//   editor_sleep_check(600000); // in the render loop, where there is a sleep screen
//   editor_activity();          // on every key press
//

// Start both timers afresh. Call when the word processor screen opens.
void editor_timers_reset();

// The writer did something. Restarts the sleep timer.
// Safe to call from the keyboard core.
void editor_activity();

// Save the file once the text has not changed for idleMs.
// Call on every render, after Editor::snapshot().
void editor_autosave(unsigned long idleMs);

// Switch to the sleep screen after idleMs without activity. Unsaved text
// counts as activity. Call on every render, after Editor::snapshot().
void editor_sleep_check(unsigned long idleMs);
