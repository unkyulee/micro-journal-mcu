#include "PairBLE.h"

//
#include "../Menu.h"
#include "app/app.h"
#include "keyboard/keyboard.h"
#include "display/display.h"

//
#include "service/BLEServer/BLEServer.h"
#include "keyboard/BLE/ble.h"

// Keep scanning and pair automatically with the first keyboard found in
// pairing mode. Rev.5 has no keys to pick from a list.
static void PairBLE_search()
{
    JsonDocument &app = status();
    app["ble_auto_pair"] = true;
    BLEServer_setup("Micro Journal 5");
}

//
void PairBLE_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    //
    Menu_clear();

    //
    JsonDocument &app = status();

    // search only when the user enabled BLE keyboard and nothing is paired
    if (ble_enabled() && !app["config"]["ble"]["name"].is<const char *>())
        PairBLE_search();
}

//
void PairBLE_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // Text to be displayed
    ptft->setCursor(0, 30, 2);
    ptft->setTextSize(1);

    //
    ptft->setTextColor(TFT_WHITE, TFT_BLACK);
    ptft->println("BLE Keyboard ");
    ptft->println("");

    //
    JsonDocument &app = status();
    bool paired = app["config"]["ble"]["name"].is<const char *>();

    if (!ble_enabled())
    {
        ptft->println("BLE keyboard is disabled.");
        if (paired)
            ptft->printf("Keyboard: %s\n", app["config"]["ble"]["name"].as<const char *>());

        ptft->println("");
        ptft->println("[M] ENABLE BLE KEYBOARD");
    }
    else if (paired)
    {
        ptft->printf("Keyboard: %s\n", app["config"]["ble"]["name"].as<const char *>());

        if (app["ble_connected"].as<bool>())
        {
            ptft->println("Status: Connected");
        }
        else
        {
            ptft->println("Status: Connecting ...");
            ptft->println("Press a key on the keyboard to wake it up");
        }

        ptft->println("");
        ptft->println("[M] DISABLE BLE KEYBOARD");
    }
    else
    {
        ptft->println("Put the keyboard in pairing mode.");
        ptft->println("It will be paired automatically.");
        ptft->println("");
        ptft->println("Searching ...");
        ptft->println("");
        ptft->println("[M] DISABLE BLE KEYBOARD");
    }

    if (paired)
        ptft->println("[R] REMOVE BLE PAIR");

    // BACK
    ptft->println();
    ptft->println("[B] BACK ");
}

//
void PairBLE_keyboard(char key)
{
    //
    JsonDocument &app = status();

    //
    BLEServer_keyboard(key);

    //
    Menu_clear();

    bool paired = app["config"]["ble"]["name"].is<const char *>();

    // ENABLE / DISABLE BLE KEYBOARD
    if (key == 'm' || key == 'M' || key == MENU)
    {
        if (ble_enabled())
        {
            app["config"]["ble_enabled"] = false;
            config_save();

            // restart to shut down the BLE radio
            ESP.restart();
        }
        else
        {
            app["config"]["ble_enabled"] = true;
            config_save();

            // a paired keyboard is reconnected by ble_loop()
            if (!paired)
                PairBLE_search();
        }
    }

    // REMOVE BLE PAIR
    else if ((key == 'r' || key == 'R') && paired)
    {
        // forget the stored bond so a new keyboard can be paired
        ble_forget();
        app["config"].remove("ble");

        // save config
        config_save();

        // restart
        ESP.restart();
    }
}
