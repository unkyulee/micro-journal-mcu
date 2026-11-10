#pragma once

// configuration
void config_setup();
bool config_load();
void config_save();

// Copy the settings that are read on every key press or every pass of the
// main loop into appState(). Runs after every load and save of the
// configuration, so code that changes such a setting only has to do what it
// always did: change it and call config_save().
void config_cache_refresh();