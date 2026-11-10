#include "Home.h"
#include "../Menu.h"
#include "app/app.h"
#include "service/Menu/MenuController.h"
#include "display/display.h"
#include "keyboard/keyboard.h"
#ifdef REV5
#include "keyboard/BLE/ble.h"
#endif

//
#include "service/Editor/Editor.h"

//
void Home_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    //
    Menu_clear();

    //
    JsonDocument &app = status();
    appState().menuState = MENU_HOME;
}

//
void Home_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    JsonDocument &app = status();

    //
    String keyboard_layout = app["config"]["keyboard_layout"].as<String>();
    if (keyboard_layout == "null" || keyboard_layout.isEmpty())
        keyboard_layout = "US";
    if (appState().keyboardCustom.load())
        keyboard_layout += " (Custom)";

    // Text to be displayed
    ptft->setCursor(0, 20, 2);
    ptft->setTextSize(1);

    //
    ptft->setTextColor(TFT_WHITE, TFT_BLACK);
    ptft->println();

    //
    // always listed: the backup screen explains what to set up
    ptft->println(" [N] NETWORK BACKUP");

    ptft->println(" [W] WIFI SETUP");
    ptft->println(" [E] DRIVE MODE");
    ptft->println(" [S] SEND");

#ifdef REV5
    if (ble_enabled())
        ptft->println(" [P] PAIR BLE KBD - ON");
    else
        ptft->println(" [P] PAIR BLE KBD - OFF");
#endif

#ifdef REV6
    ptft->println(" [K] BLE KEYBOARD");
#endif

    ptft->println(" [L] LANGUAGE - " + keyboard_layout);

    ptft->println(" [G] BACKGROUND COLOR");
    ptft->println(" [C] FONT COLOR");
    ptft->println(" [T] WORD RAIN");

    ptft->println();
    ptft->println(" [B] BACK ");
    ptft->println();
    ptft->println();

    // File Selection
    int pos_x = 190;
    ptft->setCursor(pos_x, 25, 2);
    ptft->print("CHOOSE A FILE");

    int file_index = app["config"]["file_index"].as<int>();
    for (int i = 0; i < 10; i++)
    {
        if (file_index == i)
        {
            ptft->setTextColor(TFT_GREEN, TFT_BLACK);
        }
        else
        {
            ptft->setTextColor(TFT_WHITE, TFT_BLACK);
        }

        // word count
        int wordCount = app["config"][format("wordcount_file_%d", i)].as<int>() + app["config"][format("wordcount_buffer_%d", i)].as<int>();

        ptft->setCursor(pos_x, 45 + i * 16, 2);
        ptft->printf(" [%d]: %zu", i, wordCount);
    }
    //
    ptft->setCursor(pos_x, 210, 2);
    ptft->printf(" [D] Clear File [%d] ", file_index);
}

//
void Home_keyboard(char key)
{
    // the rules are shared by all displays, see service/Menu/MenuController
    menu_home_keyboard(key);
}
