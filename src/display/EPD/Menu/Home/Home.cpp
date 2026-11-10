#include "Home.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "keyboard/keyboard.h"
#include <display/EPD/display_EPD.h>
#include "service/Tools/Tools.h"

#include "service/Editor/Editor.h"

//
void Home_setup()
{
    // every key stroke re-process the menu
    Menu_clear();
}

//
void Home_render()
{
    JsonDocument &app = status();

    //
    String keyboard_layout = app["config"]["keyboard_layout"].as<String>();
    if (keyboard_layout == "null" || keyboard_layout.isEmpty())
        keyboard_layout = "US";
    if (app["keyboard_custom"].as<bool>())
        keyboard_layout += " (Custom)";

    int cursorX = 10;
    int cursorY = 65;

    // always listed: the backup screen explains what to set up
    cursorX = 10;
    cursorY += 35;
    writeln((GFXfont *)&systemFont, " [N] NETWORK BACKUP", &cursorX, &cursorY, display_EPD_framebuffer());

    // WIFI
    cursorX = 10;
    cursorY += 35;
    writeln((GFXfont *)&systemFont, " [W] WIFI SETUP", &cursorX, &cursorY, display_EPD_framebuffer());

    // Drive Mode (web editor over WiFi)
    cursorX = 10;
    cursorY += 35;
    writeln((GFXfont *)&systemFont, " [E] DRIVE MODE", &cursorX, &cursorY, display_EPD_framebuffer());

    // Send text over Bluetooth
    cursorX = 10;
    cursorY += 35;
    writeln((GFXfont *)&systemFont, " [S] SEND", &cursorX, &cursorY, display_EPD_framebuffer());

    // Bluetooth Keyboard
    cursorX = 10;
    cursorY += 35;
    writeln((GFXfont *)&systemFont, " [K] BLE KEYBOARD", &cursorX, &cursorY, display_EPD_framebuffer());

    // Keyboard Layout
    cursorX = 10;
    cursorY += 35;
    writeln((GFXfont *)&systemFont, (" [L] LANGUAGE - " + keyboard_layout).c_str(), &cursorX, &cursorY, display_EPD_framebuffer());

    // BACK
    cursorX = 10;
    cursorY += 50;
    writeln((GFXfont *)&systemFont, " [B] BACK", &cursorX, &cursorY, display_EPD_framebuffer());

    // File Selection
    cursorX = 600;
    cursorY = 50;
    writeln((GFXfont *)&systemFont, "CHOOSE A FILE", &cursorX, &cursorY, display_EPD_framebuffer());
    cursorY += 30;

    //
    int file_index = app["config"]["file_index"].as<int>();
    for (int i = 0; i < 10; i++)
    {
        cursorX = 600;
        cursorY += 30;

        //
        int wordCount = app["config"][format("wordcount_file_%d", i)].as<int>() + app["config"][format("wordcount_buffer_%d", i)].as<int>();

        //
        writeln(
            (GFXfont *)&systemFont,
            format(" [%d]: %zu", i, wordCount).c_str(),
            &cursorX, &cursorY,
            display_EPD_framebuffer());

        // draw a circle next to the selected file
        if (file_index == i)
        {
            epd_fill_circle(590, cursorY - 8, 8, 0, display_EPD_framebuffer());
        }
    }
    //
    cursorX = 500;
    cursorY += 60;
    writeln(
        (GFXfont *)&systemFont,
        format(" [D] Clear File %d ", file_index).c_str(),
        &cursorX, &cursorY,
        display_EPD_framebuffer());
}

//
void Home_keyboard(char key)
{
    //
    JsonDocument &app = status();

    // network backup
    if (key == 'N' || key == 'n')
    {
        // always listed: the backup screen explains what to set up
        // to sync
        app["menu"]["state"] = MENU_SYNC;
    }

    // delete file
    else if (key == 'D' || key == 'd')
    {
        // to clear file
        app["menu"]["state"] = MENU_CLEAR;
    }

    // word processor
    else if (key == '\b' || key == 'B' || key == 'b')
    {
        // go back to the word processor
        app["screen"] = WORDPROCESSOR;
    }

    // wifi setup
    else if (key == 'w' || key == 'W')
    {
        // move to keyboard layout
        app["menu"]["state"] = MENU_WIFI;
    }

    // keyboardn layout setup
    else if (key == 'l' || key == 'L')
    {
        // move to keyboard layout
        app["menu"]["state"] = MENU_LAYOUT;
    }

    // send text over bluetooth
    else if (key == 's' || key == 'S')
    {
        app["screen"] = SENDSCREEN;
    }

    // web editor
    else if (key == 'e' || key == 'E')
    {
        // edit files from a browser over wifi
        app["menu"]["state"] = MENU_WEB;
    }

    // Bluetooth Keyboard
    else if (key == 'k' || key == 'K')
    {
        // move to keyboard screen
        app["screen"] = KEYBOARDSCREEN;
    }

    // chose file
    if (key > 47 && key < 58)
    {
        // save config
        int file_index = key - 48;
        app["config"]["file_index"] = file_index;
        config_save();

        // load editor
        Editor::getInstance().loadFile(format("/%d.txt", file_index));

        //
        // go back to the word processor
        app["screen"] = WORDPROCESSOR;
    }
}