#include "Home.h"
#include "../Menu.h"
#include "app/app.h"
#include "service/Menu/MenuController.h"
#include "display/display.h"
#include "keyboard/keyboard.h"

//
#include "service/Editor/Editor.h"

//
void Home_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    //
    Menu_clear();

    //
    JsonDocument &app = status();
    appState().menuState = MENU_HOME;
}

//
void Home_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    //
    String keyboard_layout = app["config"]["keyboard_layout"].as<String>();
    if (keyboard_layout == "null" || keyboard_layout.isEmpty())
        keyboard_layout = "US";

    // Text to be displayed
    u8->setCursor(0, 40);
    u8->println();
    //

    //
    // always listed: the backup screen explains what to set up
    u8->println(" [N] NETWORK BACKUP");

    u8->println(" [W] WIFI SETUP");
    u8->println(" [E] DRIVE MODE");
    u8->println(" [S] SEND");
    u8->println(" [K] BLE KEYBOARD");
    u8->println(" [L] LANGUAGE - " + keyboard_layout);
    // narrow left column - show custom keymap on its own line
    if (appState().keyboardCustom.load())
        u8->println("     (Custom)");
    u8->println(" [T] WORD RAIN");

    u8->println();
    u8->println(" [B] BACK ");

    // File Selection
    int pos_x = 190;
    u8->setCursor(pos_x, 45);
    u8->print("CHOOSE A FILE");

    int file_index = app["config"]["file_index"].as<int>();
    for (int i = 0; i < 10; i++)
    {
        // word count
        int wordCount = app["config"][format("wordcount_file_%d", i)].as<int>() + app["config"][format("wordcount_buffer_%d", i)].as<int>();

        u8->setCursor(pos_x, 75 + i * 20);
        u8->printf(" [%d]: %zu", i, wordCount);
    }
    //
    u8->setCursor(pos_x, 290);
    u8->printf(" [D] Clear File # %d", file_index);
}

//
void Home_keyboard(char key)
{
    // the rules are shared by all displays, see service/Menu/MenuController
    menu_home_keyboard(key);
}
