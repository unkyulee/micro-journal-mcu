#include "BLEServer.h"
#include "app/app.h"
#include "display/display.h"
#include "keyboard/BLE/ble.h"

// services
#include "service/Buffer/BufferService.h"

// HID service UUID
static BLEUUID hidUUID((uint16_t)0x1812);

/////////////////////////////////////////////////////
// BACKGROUND SERVICE
/////////////////////////////////////////////////////
void BLEServer_init()
{
    // load app status
    JsonDocument &app = status();

    // reset task queue
    appState().bleState = 0;
    appState().bleError = "";
    appState().bleMessage = "";

    _log("[BLEServer_init]\n");
}

void BLEServer_loop()
{
    static unsigned int last = 0;
    if (millis() - last > 1000)
    {
        last = millis();

        //
        JsonDocument &app = status();

        // scan for the BLE devices
        if (appState().takeTask(TASK_BLE_SCAN))
        {
            //
            _log("[BLEServer_loop] Picked up BLE Device Scan Request\n");

            //
            BLEServer_scan();

            // pair automatically with a keyboard in pairing mode
            if (appState().bleAutoPair.load())
            {
                if (!BLEServer_auto_pair())
                {
                    // keep searching until a keyboard shows up or the
                    // pairing screen is closed
                    appState().requestTask(TASK_BLE_SCAN);
                }
                return;
            }

            // Check if any keyboard is found
            JsonArray ble_devices = app["ble_devices"].as<JsonArray>();
            if (ble_devices.size() == 0)
            {
                _log("No BLE Devices Found\n");
                appState().bleState = BLE_CONFIG_NO_DEVICES;
            }
        }

        // reconnecting to a paired keyboard is handled by ble_loop()
    }
}

// Save the strongest keyboard found in pairing mode as the paired keyboard.
// ble_loop() picks up the new configuration and connects and bonds with it.
bool BLEServer_auto_pair()
{
    JsonDocument &app = status();
    JsonArray devices = app["ble_devices"].as<JsonArray>();

    int selected = -1;
    for (int i = 0; i < devices.size(); i++)
    {
        if (!devices[i]["pairing"].as<bool>())
            continue;

        if (selected < 0 || devices[i]["rssi"].as<int>() > devices[selected]["rssi"].as<int>())
            selected = i;
    }

    if (selected < 0)
        return false;

    _log("[BLEServer_auto_pair] Pairing with %s %s\n",
         devices[selected]["name"].as<const char *>(),
         devices[selected]["address"].as<const char *>());

    // only one keyboard is supported; drop old bonds so the new keyboard
    // is the one ble_loop() reconnects to
    ble_forget();

    app["config"]["ble"]["address"] = devices[selected]["address"];
    app["config"]["ble"]["name"] = devices[selected]["name"];
    app["config"]["ble"]["type"] = devices[selected]["type"];
    config_save();

    appState().bleAutoPair = false;
    appState().clear = true;

    return true;
}

void BLEServer_scan()
{
    _log("BLE Devices Scan Starting\n");

    //
    JsonDocument &app = status();
    //
    appState().bleError = "";
    appState().bleMessage = "Scanning BLE Devices";
    app["ble_devices"] = app["ble_devices"].to<JsonArray>();

    // the auto pair screen shows a fixed message, so don't redraw it on every
    // repeated scan
    bool refresh = !appState().bleAutoPair.load();
    if (refresh)
        appState().clear = true;

    // Scan BLE devices
    NimBLEScan *pScan = NimBLEDevice::getScan();
    NimBLEScanResults results = pScan->getResults(5 * 1000);
    JsonArray ble_devices = app["ble_devices"].as<JsonArray>();
    ble_devices.clear();

    int count = 0;
    for (int i = 0; i < results.getCount(); i++)
    {
        const NimBLEAdvertisedDevice *device = results.getDevice(i);

        if (device->isAdvertisingService(hidUUID))
        {
            // retrieve information
            // Add to the config
            app["ble_devices"][count]["address"] = device->getAddress().toString().c_str();
            app["ble_devices"][count]["name"] = device->getName().c_str();
            app["ble_devices"][count]["type"] = (int)device->getAddressType();
            app["ble_devices"][count]["rssi"] = device->getRSSI();

            // A keyboard in pairing mode advertises as discoverable (limited
            // or general). A bonded keyboard reconnecting to another host does
            // not, and a mouse is never a candidate.
            bool discoverable = device->getAdvFlags() & (BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_DISC_GEN);
            bool mouse = device->haveAppearance() && device->getAppearance() == 0x03C2;
            app["ble_devices"][count]["pairing"] = discoverable && !mouse && device->isConnectable();

            //
            _log("BLE device found index: %d name: %s address: %s type: %d rssi: %d pairing: %d\n",
                 i,
                 app["ble_devices"][count]["name"].as<const char *>(),
                 app["ble_devices"][count]["address"].as<const char *>(),
                 app["ble_devices"][count]["type"].as<int>(),
                 app["ble_devices"][count]["rssi"].as<int>(),
                 app["ble_devices"][count]["pairing"].as<bool>());

            //
            count++;
        }
    }

    // refresh the menu screen
    _log("BLE Devices Scan Ended\n");
    if (refresh)
        appState().clear = true;
}

// Entry Screen Initializing
void BLEServer_setup(const char *name)
{
    //
    buffer_clear();

    // init BLE device
    ble_init(name);

    //
    JsonDocument &app = status();

    // initialize the config
    if (!app["ble"].is<JsonObject>())
    {
        JsonObject ble = app["ble"].to<JsonObject>();
        app["ble"] = ble;
    }

    if (!app["ble"]["devices"].is<JsonArray>())
    {
        JsonArray devices = app["ble"]["devices"].to<JsonArray>();
        app["ble"]["devices"] = devices;
    }

    // Request for BLE Devices Scan
    appState().requestTask(TASK_BLE_SCAN);
    appState().bleState = BLE_CONFIG_LIST;
}

//
void BLEServer_keyboard(char key)
{
    // non printable keys are not going to be going through the buffer
    if (key == 0)
        return;

    //
    JsonDocument &app = status();

    //
    _debug("BLEServer_keyboard key: %d\n", key);

    // back to home
    if (key == 'B' || key == 'b')
    {
        // stop searching for a keyboard to pair with
        appState().bleAutoPair = false;
        appState().menuState = MENU_HOME;
        return;
    }
}
