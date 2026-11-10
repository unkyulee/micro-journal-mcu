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
void Sync_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
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
void Sync_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    //
    JsonDocument &app = status();

    // header
    ptft->setCursor(0, 30, 2);
    ptft->setTextColor(TFT_WHITE, TFT_BLACK);
    ptft->println(" NETWORK BACKUP ");
    ptft->println("");

    // SYNC STATE
    int sync_state = appState().syncState.load();

    //
    if (sync_state == SYNC_START)
    {
        //
        ptft->println("Preparing Network Backup ...");

        //
        sync_start_request();
    }

    else if (sync_state == SYNC_STARTED || sync_state == SYNC_PROGRESS)
    {
        String message = appState().syncMessage.get();
        if (message != "")
        {
            //
            ptft->println(message);
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
            ptft->setTextColor(TFT_WHITE, TFT_RED);
            ptft->println(error);
            ptft->setTextColor(TFT_WHITE, TFT_BLACK);
        }
    }

    else if (sync_state == SYNC_COMPLETED)
    {
        //
        ptft->println("NETWORK BACKUP Completed. Press any key to exit");
    }

    // WiFi or the sync url still has to be set up
    else if (sync_state == SYNC_NOT_CONFIGURED)
    {
        String lines[12];
        int count = sync_setup_lines(lines, 12);
        for (int i = 0; i < count; i++)
            ptft->println(" " + lines[i]);
    }
}

//
void Sync_keyboard(char key)
{
    // the rules are shared by all displays, see service/Menu/MenuController
    menu_sync_keyboard(key);
}
