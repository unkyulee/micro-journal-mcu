#include "ble.h"
#include "app/app.h"
#include "keyboard/keyboard.h"
#include "keyboard/Locale/locale.h"
#include "display/display.h"

#include "service/BLEServer/BLEServer.h"

// notify callback shouldn't be blocked
// any file operation triggers will crash the system
// so copy the message from the callback and process it in the main process
// Define the HID report structure
typedef struct
{
    uint8_t report[7];
} HidReport_t;

// Queue to store HID reports
static QueueHandle_t hidQueue = nullptr;

// Reconnect state. The flags are written by the NimBLE host task callbacks
// and read by ble_loop(), which does all the blocking work and file I/O.
static volatile bool ble_connecting = false; // async connect is pending
static volatile bool ble_link_up = false;    // link is up, HID not attached yet
static bool ble_attached = false;            // subscribed to key reports
static unsigned long ble_retry_at = 0;

static const char *ble_name = "";

static bool ble_connect();
static bool ble_attach();

// BLE keyboard is off unless the user enables it in the menu, so the radio
// doesn't drain the battery. Configs from before this setting existed had
// BLE on whenever a keyboard was paired.
bool ble_enabled()
{
    JsonDocument &app = status();
    if (app["config"]["ble_enabled"].is<bool>())
        return app["config"]["ble_enabled"].as<bool>();

    return app["config"]["ble"]["address"].is<const char *>();
}

//
void ble_setup(const char *adName)
{
    //
    JsonDocument &app = status();
    ble_name = adName;

    // When enabled and ble.address exists then ble_loop() keeps a connection
    // pending to the keyboard. A bonded keyboard reconnects with the stored
    // keys as soon as it advertises (usually after any key press), no
    // pairing mode needed.
    if (ble_enabled() && app["config"]["ble"]["address"].is<const char *>())
    {
        // BLE Initialize
        ble_init(adName);
        _log("[ble_setup] BLE keyboard configured: %s\n", app["config"]["ble"]["address"].as<const char *>());
    }

    // Create HID report queue (10 elements of HidReport_t)
    hidQueue = xQueueCreate(10, sizeof(HidReport_t));
    assert(hidQueue != NULL);
}

//
void ble_loop()
{
    //
    JsonDocument &app = status();

    if (ble_enabled() && app["config"]["ble"]["address"].is<const char *>())
    {
        // BLE may have been enabled from the menu after boot
        ble_init(ble_name);

        if (ble_link_up)
        {
            // keyboard just connected: encrypt, discover and subscribe
            if (!ble_attached && !ble_attach())
            {
                ble_link_up = false;
                ble_retry_at = millis() + 1000;
            }
        }
        else
        {
            if (ble_attached)
            {
                ble_attached = false;
                app["ble_connected"] = false;
                app["clear"] = true; // refresh the pairing screen status
                _log("[ble_loop] BLE keyboard disconnected\n");
            }

            // keep a connection attempt pending so the keyboard is picked up
            // whenever it starts advertising
            if (!ble_connecting && (long)(millis() - ble_retry_at) >= 0)
            {
                if (!ble_connect())
                    ble_retry_at = millis() + 1000;
            }
        }
    }

    // Process HID queue safely
    HidReport_t report;
    while (hidQueue != nullptr && xQueueReceive(hidQueue, &report, 0) == pdTRUE)
    {
        static uint8_t prev[7] = {0};

        // Key Pressed
        for (int i = 1; i < 7; i++)
        {
            if (report.report[i] != 0)
            {
                bool newKey = true;
                for (int j = 1; j < 7; j++)
                {
                    if (prev[j] == report.report[i])
                    {
                        newKey = false;
                        break;
                    }
                }
                if (newKey)
                {
                    keyboard_HID2Ascii(report.report[i], report.report[0], true);
                }
            }
        }

        // Key Released
        for (int i = 1; i < 7; i++)
        {
            if (prev[i] != 0)
            {
                bool released = true;
                for (int j = 1; j < 7; j++)
                {
                    if (prev[i] == report.report[j])
                    {
                        released = false;
                        break;
                    }
                }
                if (released)
                {
                    keyboard_HID2Ascii(prev[i], prev[0], false);
                }
            }
        }

        memcpy(prev, report.report, 7);
    }
}

// Initialize BLE Device
bool ble_init_done = false;
void ble_init(const char *name)
{
    if (!ble_init_done)
    {
        _log("[ble_init] Device Init: %s\n", name);

        // Start the BLEDevice
        NimBLEDevice::init(name);
        NimBLEDevice::setSecurityAuth(true, true, true); // bonding + MITM + SC

        //
        ble_init_done = true;
    }
}

// Custom callback class for BLE client
class clientCallback : public NimBLEClientCallbacks
{
    void onConnect(NimBLEClient *pClient)
    {
        _log("[BLEClientCallbacks] onConnect\n");

        NimBLEConnInfo connInfo = pClient->getConnInfo();

        //
        _log("  Peer Address: %s\n", pClient->getPeerAddress().toString().c_str());
        _log("  Conn Handle: %d\n", connInfo.getConnHandle());
        _log("  MTU: %u\n", pClient->getMTU());
        _log("  Encrypted: %s\n", connInfo.isEncrypted() ? "Yes" : "No");
        _log("  Bonded: %s\n", connInfo.isBonded() ? "Yes" : "No");

        // ble_loop() attaches to the HID service outside of the host task
        ble_connecting = false;
        ble_link_up = true;
    }

    void onConnectFail(NimBLEClient *pClient, int reason)
    {
        // a timeout here is normal: the keyboard was not advertising
        _debug("[BLEClientCallbacks] onConnectFail %d\n", reason);
        ble_connecting = false;
        ble_link_up = false;
    }

    void onDisconnect(NimBLEClient *pClient, int reason)
    {
        _log("[BLEClientCallbacks] onDisconnect %d\n", reason);
        ble_connecting = false;
        ble_link_up = false;
    }

    /**
     * @brief Called when server requests to update the connection parameters.
     * @param [in] pClient A pointer to the calling client object.
     * @param [in] params A pointer to the struct containing the connection parameters requested.
     * @return True to accept the parameters.
     */
    bool onConnParamsUpdateRequest(NimBLEClient *pClient, const ble_gap_upd_params *params)
    {
        _log("[BLEClientCallbacks] onConnParamsUpdateRequest\n");
        _log("  Interval Min: %u (%.2f ms)\n", params->itvl_min, params->itvl_min * 1.25);
        _log("  Interval Max: %u (%.2f ms)\n", params->itvl_max, params->itvl_max * 1.25);
        _log("  Latency: %u events\n", params->latency);
        _log("  Supervision Timeout: %u (%.1f ms)\n",
             params->supervision_timeout,
             params->supervision_timeout * 10.0);

        return true; // Accept the parameters
    }

    /**
     * @brief Called when server requests a passkey for pairing.
     * @param [in] connInfo A reference to a NimBLEConnInfo instance containing the peer info.
     */
    void onPassKeyEntry(NimBLEConnInfo &connInfo)
    {
        Serial.println("Keyboard requesting passkey...");
        // Inject the same passkey you configured earlier
        NimBLEDevice::injectPassKey(connInfo, 123456);
    }

    /**
     * @brief Called when the pairing procedure is complete.
     * @param [in] connInfo A reference to a NimBLEConnInfo instance containing the peer info.\n
     * This can be used to check the status of the connection encryption/pairing.
     */
    void onAuthenticationComplete(NimBLEConnInfo &connInfo)
    {
        _log("[onAuthenticationComplete]\n");
        _log("  Peer Address: %s\n", connInfo.getAddress().toString().c_str());
        _log("  Encrypted: %s\n", connInfo.isEncrypted() ? "Yes" : "No");
        _log("  Bonded: %s\n", connInfo.isBonded() ? "Yes" : "No");
        _log("  Authenticated (MITM): %s\n", connInfo.isAuthenticated() ? "Yes" : "No");
        //_log("  Security Level: %d\n", connInfo.getSecurityLevel());
        //_log("  Key Size: %d\n", connInfo.getKeySize());
        //_log("  Role: %s\n", connInfo.getRole() == BLE_HS_CONN_ROLE_MASTER ? "Central" : "Peripheral");

        // the identity address is saved by ble_attach(); file I/O is not
        // safe in this host task callback
    }

    /**
     * @brief Called when using numeric comparision for pairing.
     * @param [in] connInfo A reference to a NimBLEConnInfo instance containing the peer info.
     * @param [in] pin The pin to compare with the server.
     */
    void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin)
    {
        _log("[BLEClientCallbacks] onConfirmPasskey\n");
        Serial.printf("Confirm passkey: %06u\n", pin);
        // Accept automatically
        NimBLEDevice::injectConfirmPasskey(connInfo, true);
    }

    /**
     * @brief Called when the peer identity address is resolved.
     * @param [in] connInfo A reference to a NimBLEConnInfo instance with information
     */
    void onIdentity(NimBLEConnInfo &connInfo)
    {
        _log("[BLEClientCallbacks] onIdentity\n");
    }

    /**
     * @brief Called when the connection MTU changes.
     * @param [in] pClient A pointer to the client that the MTU change is associated with.
     * @param [in] MTU The new MTU value.
     * about the peer connection parameters.
     */
    void onMTUChange(NimBLEClient *pClient, uint16_t MTU)
    {
        _log("[BLEClientCallbacks] onMTUChange\n");
        _log("  Peer: %s\n", pClient->getPeerAddress().toString().c_str());
        _log("  New MTU: %u bytes\n", MTU);
    }

    /**
     * @brief Called when the PHY update procedure is complete.
     * @param [in] pClient A pointer to the client whose PHY was updated.
     * about the peer connection parameters.
     * @param [in] txPhy The transmit PHY.
     * @param [in] rxPhy The receive PHY.
     * Possible values:
     * * BLE_GAP_LE_PHY_1M
     * * BLE_GAP_LE_PHY_2M
     * * BLE_GAP_LE_PHY_CODED
     */
    void onPhyUpdate(NimBLEClient *pClient, uint8_t txPhy, uint8_t rxPhy)
    {
        _log("[BLEClientCallbacks] onPhyUpdate\n");
    }
};

static clientCallback clientCB;

// Callback function for notifications
void notifyCallback(
    NimBLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
    // when key press message comes from the BLE keyboard
    // send it to the main process Q
    HidReport_t report;
    memcpy(report.report, pData, length > 7 ? 7 : length);
    xQueueSendFromISR(hidQueue, &report, nullptr);
}

// Connect to BLE device
NimBLEUUID serviceUUID = NimBLEUUID("1812");
NimBLEUUID charUUID = NimBLEUUID("2A4D");
NimBLEClient *client;
NimBLERemoteService *service;
NimBLERemoteCharacteristic *characteristic;

// Address to reconnect to. A bond stores the keyboard's identity address,
// which stays valid even when the keyboard advertises with a changing
// private address (the stored IRK lets the controller resolve it).
static NimBLEAddress ble_target_address()
{
    JsonDocument &app = status();
    const char *address = app["config"]["ble"]["address"].as<const char *>();
    const int type = app["config"]["ble"]["type"].as<int>();
    NimBLEAddress target = NimBLEAddress(std::string(address), type);

    // older firmware could save a temporary private address instead of the
    // identity address; fall back to the most recent bond in that case
    int bonds = NimBLEDevice::getNumBonds();
    if (target.isRpa() && !NimBLEDevice::isBonded(target) && bonds > 0)
        target = NimBLEDevice::getBondedAddress(bonds - 1);

    return target;
}

// Start an asynchronous connection attempt. It stays pending in the
// controller until the keyboard advertises or the timeout expires, so a
// sleeping keyboard is picked up as soon as a key is pressed on it.
static bool ble_connect()
{
    if (!client)
    {
        client = NimBLEDevice::createClient();
        if (!client)
        {
            _log("[ble_connect] Failed to create client\n");
            return false;
        }
        client->setClientCallbacks(&clientCB, false);
        client->setConnectionParams(12, 12, 0, 51);
        client->setConnectTimeout(30 * 1000);
    }

    NimBLEAddress target = ble_target_address();
    _debug("[ble_connect] waiting for %s bonded: %d\n",
           target.toString().c_str(), NimBLEDevice::isBonded(target));

    ble_connecting = true;
    if (!client->connect(target, true, true))
    {
        _debug("[ble_connect] Failed to start connection: %d\n", client->getLastError());
        ble_connecting = false;
        return false;
    }

    return true;
}

// Secure the link and subscribe to the keyboard's HID reports
static bool ble_attach()
{
    _log("[ble_attach] Connected to %s\n", client->getPeerAddress().toString().c_str());

    // Encrypt the link. A bonded keyboard is re-encrypted with the stored
    // keys; a new keyboard (in pairing mode) is paired and bonded here.
    if (!client->secureConnection())
        _log("[ble_attach] Failed to secure connection: %d\n", client->getLastError());

    if (!client->discoverAttributes())
    {
        _log("[ble_attach] Failed to discover attributes\n");
        client->disconnect();
        return false;
    }

    service = client->getService(serviceUUID);
    if (service == nullptr)
    {
        _log("[ble_attach] Cannot find service %s\n", serviceUUID.toString().c_str());
        client->disconnect();
        return false;
    }

    characteristic = service->getCharacteristic(charUUID);
    if (characteristic == nullptr)
    {
        _log("[ble_attach] Failed to get characteristic %s\n", charUUID.toString().c_str());
        client->disconnect();
        return false;
    }

    if (characteristic->canRead())
    {
        std::string val = characteristic->readValue();
        _log("[ble_attach] Read value size: %d\n", (int)val.size());
    }

    if (characteristic->canNotify())
    {
        if (!characteristic->subscribe(true, notifyCallback))
        {
            _log("[ble_attach] failed to subscribe\n");
            client->disconnect();
            return false;
        }
    }

    JsonDocument &app = status();

    // remember the identity address so the next reconnect targets the bond
    NimBLEConnInfo connInfo = client->getConnInfo();
    if (connInfo.isBonded())
    {
        NimBLEAddress id = connInfo.getIdAddress();
        String address = id.toString().c_str();
        if (address != app["config"]["ble"]["address"].as<String>() ||
            id.getType() != app["config"]["ble"]["type"].as<int>())
        {
            _log("[ble_attach] Saving keyboard identity address %s type %d\n", address.c_str(), id.getType());
            app["config"]["ble"]["address"] = address;
            app["config"]["ble"]["type"] = id.getType();
            config_save();
        }
    }

    ble_attached = true;
    app["ble_connected"] = true;
    app["clear"] = true; // refresh the pairing screen status
    _log("[ble_attach] BLE keyboard ready\n");

    return true;
}

// Forget every bonded keyboard, used when unpairing
void ble_forget()
{
    // bonds live in NVS, so the stack must be up to delete them even when
    // BLE keyboard is disabled
    ble_init(ble_name);
    NimBLEDevice::deleteAllBonds();
}
