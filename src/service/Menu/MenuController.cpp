#include "MenuController.h"
#include "app/app.h"
#include "display/display.h"

//
#include "service/Editor/Editor.h"
#include "service/Sync/Sync.h"
#include "service/FileServer/FileServer.h"

//
// Which entries a board's main menu has
//

// Bluetooth keyboard pairing and the front panel buttons: Rev.5 only
#if defined(REV5)
#define MENU_HAS_PAIRING 1
#else
#define MENU_HAS_PAIRING 0
#endif

// act as a Bluetooth keyboard for a computer: every board but Rev.5
#if defined(REV5)
#define MENU_HAS_BLE_KEYBOARD 0
#else
#define MENU_HAS_BLE_KEYBOARD 1
#endif

// colour screens choose a background and a font colour
#if defined(ILI9341_DRIVER) || defined(ST7789_DRIVER)
#define MENU_HAS_COLORS 1
#else
#define MENU_HAS_COLORS 0
#endif

// the typing game is not built for the e-ink board
#if defined(REV7)
#define MENU_HAS_GAME 0
#else
#define MENU_HAS_GAME 1
#endif

// Rev.6 picks between input languages, the others pick a keyboard layout
#if defined(REV6)
#define MENU_LANGUAGE_SCREEN MENU_LANGUAGE
#else
#define MENU_LANGUAGE_SCREEN MENU_LAYOUT
#endif

//
void menu_home_keyboard(int key)
{
    JsonDocument &app = status();

    // letters work in either case
    if (key >= 'A' && key <= 'Z')
        key = key - 'A' + 'a';

    // choose a file
    if (key >= '0' && key <= '9')
    {
        // save config
        int file_index = key - '0';
        app["config"]["file_index"] = file_index;
        config_save();

        // load editor
        Editor::getInstance().loadFile(format("/%d.txt", file_index));

        // go back to the word processor
        appState().screen = WORDPROCESSOR;
        return;
    }

    switch (key)
    {
    // network backup
    // always available: the backup screen explains what to set up
    case 'n':
        appState().menuState = MENU_SYNC;
        break;

    // clear the current file
    case 'd':
        appState().menuState = MENU_CLEAR;
        break;

    // back to the word processor
    case 'b':
    case '\b':
        appState().screen = WORDPROCESSOR;
        break;

    // wifi setup
    case 'w':
        appState().menuState = MENU_WIFI;
        break;

    // drive mode: edit files from a browser over wifi
    case 'e':
        appState().menuState = MENU_WEB;
        break;

    // act as a keyboard and type the text into a computer
    case 's':
        appState().screen = SENDSCREEN;
        break;

    // keyboard layout or input language
    case 'l':
        appState().menuState = MENU_LANGUAGE_SCREEN;
        break;

#if MENU_HAS_PAIRING
    // front panel buttons
    case 'f':
        appState().menuState = MENU_BUTTONS;
        break;

    // pair a bluetooth keyboard
    case 'p':
    case MENU:
        appState().menuState = MENU_BLUETOOTH;
        break;
#endif

#if MENU_HAS_BLE_KEYBOARD
    // bluetooth keyboard mode
    case 'k':
        appState().screen = KEYBOARDSCREEN;
        break;
#endif

#if MENU_HAS_GAME
    // typing game
    case 't':
        appState().screen = GAMESCREEN;
        break;
#endif

#if MENU_HAS_COLORS
    // background color
    case 'g':
        appState().menuState = MENU_BACKGROUND;
        break;

    // font color
    case 'c':
        appState().menuState = MENU_FONTCOLOR;
        break;
#endif

    default:
        break;
    }
}

//
void menu_sync_keyboard(int key)
{
    JsonDocument &app = status();

    int sync_state = appState().syncState.load();

    // finished one way or the other: back to writing
    if (sync_state == SYNC_COMPLETED || sync_state == SYNC_ERROR)
    {
        appState().screen = WORDPROCESSOR;
    }

    // back to the menu, where WiFi setup and Drive Mode are
    else if (sync_state == SYNC_NOT_CONFIGURED)
    {
        appState().menuState = MENU_HOME;
    }

    // while the backup is running keys do nothing
}

//
void menu_clear_keyboard(int key)
{
    JsonDocument &app = status();

    // delete confirmed
    if (key == 'Y' || key == 'y')
    {
        // empty the file
        Editor::getInstance().clearFile();

        // go back to the word processor
        appState().screen = WORDPROCESSOR;
    }

    // go back to home menu
    appState().menuState = MENU_HOME;
}

//
void menu_web_keyboard(int key)
{
    // ESC finishes the session, any key leaves the error screen
    if (fileserver_state() == FILESERVER_ERROR || key == 27 || key == MENU)
    {
        fileserver_stop_request();
    }
}
