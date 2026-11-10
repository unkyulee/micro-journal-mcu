#pragma once

bool firmware_check();
bool filesystem_check();

// Called by the error screen with the key that was pressed. When the screen
// is asking whether to format unreadable storage and the key is Y, the
// storage is formatted. The caller restarts the device afterwards either way.
void filesystem_format_confirm(char key);