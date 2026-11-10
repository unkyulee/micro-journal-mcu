#pragma once

//
// What the keys do in the menu, shared by the displays that use the
// letter-key menu (Rev.5, Rev.6, Rev.7, Rev.8).
//
// Each display used to carry its own copy of these handlers next to its
// drawing code, differing only in which entries a board has. The rules live
// here now - which key opens which screen, when a screen may be left - and a
// display's menu files only draw. Differences between boards are decided in
// MenuController.cpp by the board's build flags.
//
// The Rev.4 menu is a different design (a list driven by the knob) and keeps
// its own handlers.
//

// main menu: open a sub menu, pick a file, go back to writing
void menu_home_keyboard(int key);

// network backup: leave when it is done, failed, or still needs setting up
void menu_sync_keyboard(int key);

// clear file: Y empties the file, anything else goes back
void menu_clear_keyboard(int key);

// drive mode: ESC ends the session, any key leaves an error
void menu_web_keyboard(int key);
