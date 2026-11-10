#include "Sync.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "../Wifi/Wifi.h"
#include <display/EPD/display_EPD.h>

//
#include "service/WifiEntry/WifiEntry.h"
#include "service/Sync/Sync.h"
#include "service/Editor/Editor.h"

//
void Sync_setup()
{
    _log("Sync_setup\n");

    //
    Menu_clear();

    // load wifi
    wifi_config_load();

    // init sync
    sync_init();
}

//
void Sync_render()
{
    //
    JsonDocument &app = status();

    // SYNC IN PROGRESS
    int cursorX = 10;
    int cursorY = 120;
    writeln(
        (GFXfont *)&systemFont,
        format("NETWORK BACKUP for %s", Editor::getInstance().fileName.c_str()).c_str(),
        &cursorX, &cursorY,
        display_EPD_framebuffer());

    // SYNC STATE
    int sync_state = app["sync_state"].as<int>();

    //
    if (sync_state == SYNC_START)
    {
        //
        cursorX = 30;
        cursorY += 50;
        writeln(
            (GFXfont *)&systemFont,
            "Preparing Network Backup ...",
            &cursorX, &cursorY,
            display_EPD_framebuffer());

        // Start Sync Process
        // 1) Connect to Wifi
        sync_start_request();
    }

    else if (sync_state == SYNC_STARTED || sync_state == SYNC_PROGRESS)
    {
        String message = app["sync_message"].as<String>();

        if (message != "")
        {
            cursorX = 30;
            cursorY += 50;
            writeln(
                (GFXfont *)&systemFont,
                message.c_str(),
                &cursorX, &cursorY,
                display_EPD_framebuffer());
        }
    }

    //
    else if (sync_state == SYNC_ERROR)
    {
        String error = app["sync_error"].as<String>();
        if (error != "")
        {
            cursorX = 30;
            cursorY += 50;
            writeln(
                (GFXfont *)&systemFont,
                error.c_str(),
                &cursorX, &cursorY,
                display_EPD_framebuffer());
        }
    }

    else if (sync_state == SYNC_COMPLETED)
    {
        //
        cursorX = 30;
        cursorY += 50;

        writeln(
            (GFXfont *)&systemFont,
            "NETWORK BACKUP Completed. Press any key to exit.",
            &cursorX, &cursorY,
            display_EPD_framebuffer());
    }

    // WiFi or the sync url still has to be set up
    else if (sync_state == SYNC_NOT_CONFIGURED)
    {
        String lines[12];
        int count = sync_setup_lines(lines, 12);

        cursorY += 15;
        for (int i = 0; i < count; i++)
        {
            cursorX = 30;
            cursorY += 35;

            // an empty line only moves down
            if (lines[i].isEmpty())
                continue;

            writeln(
                (GFXfont *)&systemFont,
                lines[i].c_str(),
                &cursorX, &cursorY,
                display_EPD_framebuffer());
        }
    }

}

//
void Sync_keyboard(char key)
{
    //
    JsonDocument &app = status();

    // SYNC STATE
    int sync_state = app["sync_state"].as<int>();
    if (sync_state == SYNC_COMPLETED || sync_state == SYNC_ERROR)
    {
       app["screen"] = WORDPROCESSOR;
    }

    // back to the menu, where WiFi setup and Drive Mode are
    else if (sync_state == SYNC_NOT_CONFIGURED)
    {
        app["menu"]["state"] = MENU_HOME;
    }
}
