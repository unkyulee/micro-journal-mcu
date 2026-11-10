#include "Sync.h"
#include "../Menu.h"
#include "app/app.h"
#include "service/Menu/MenuController.h"
#include "display/display.h"
//
#include "service/WifiEntry/WifiEntry.h"
#include "service/Sync/Sync.h"
#include "service/Editor/Editor.h"

//
void Sync_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
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
void Sync_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    //
    JsonDocument &app = status();

    // header
    u8->setCursor(0, 50);
    u8->println(" NETWORK BACKUP ");
    u8->println("");

    // SYNC STATE
    int sync_state = appState().syncState.load();

    //
    if (sync_state == SYNC_START)
    {
        //
        u8->println("Preparing Network Backup ...");

        //
        sync_start_request();
    }

    else if (sync_state == SYNC_STARTED || sync_state == SYNC_PROGRESS)
    {
        String message = appState().syncMessage.get();
        if (message != "")
        {
            //
            u8->println(message);
        }
    }

    //
    // error
    //
    else if (sync_state == SYNC_ERROR)
    {
        String error = appState().syncError.get();
        if (error != "")
        {
            u8->println(error);
        }
    }

    else if (sync_state == SYNC_COMPLETED)
    {
        //
        u8->println("NETWORK BACKUP Completed. Press any key to exit");
    }

    // WiFi or the sync url still has to be set up
    else if (sync_state == SYNC_NOT_CONFIGURED)
    {
        String lines[12];
        int count = sync_setup_lines(lines, 12);
        for (int i = 0; i < count; i++)
            u8->println(" " + lines[i]);
    }
}

//
void Sync_keyboard(char key)
{
    // the rules are shared by all displays, see service/Menu/MenuController
    menu_sync_keyboard(key);
}
