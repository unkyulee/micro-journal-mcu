#include "KeyboardScreen.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"

//
#include "../display_EPD.h"
#include "../BleUI/BleUI.h"

//
#include <NimBLEDevice.h>
#include <BleKeyboard.h>

BleKeyboard bleKeyboard;
int keyboardConnectedPrev = -1;

static constexpr uint8_t HID_KEY_ESCAPE = 0x29;
static constexpr uint32_t ESC_LONG_PRESS_MS = 2000;
static volatile bool escPressed = false;
static volatile uint32_t escPressedAt = 0;
static bool bleKeyboardExitStarted = false;

static void KeyboardScreen_exitBleKeyboardMode()
{
    if (bleKeyboardExitStarted)
        return;

    bleKeyboardExitStarted = true;
    _log("ESC long press: disabling Bluetooth Keyboard mode\n");

    if (bleKeyboard.isConnected())
        bleKeyboard.releaseAll();

    JsonDocument &app = status();
    app["config"]["UsbKeyboard"] = false;
    config_save();

    delay(50);
    ESP.restart();
}

//
void KeyboardScreen_setup()
{
    //
    _log("Keyboard Screen Setup\n");

    //
    keyboardConnectedPrev = -1;
    escPressed = false;
    escPressedAt = 0;
    bleKeyboardExitStarted = false;

    // Clear Screen
    epd_poweron();
    epd_clear_quick(epd_full_screen(), 4, 50);
    epd_poweroff();

    // Setup Bluetooth Keyboard
    bleKeyboard.setName("Micro Journal 7");
    bleKeyboard.begin();
    _log("Bluetooth Keyboard Started\n");
}

//
void KeyboardScreen_render()
{
    if (escPressed && millis() - escPressedAt >= ESC_LONG_PRESS_MS)
    {
        KeyboardScreen_exitBleKeyboardMode();
        return;
    }

    // clear screen if status changed
    bool keyboardConnected = bleKeyboard.isConnected();
    if (keyboardConnected != keyboardConnectedPrev)
    {
        //
        keyboardConnectedPrev = keyboardConnected;

        // render screen
    }
    else
    {
        // nothing change no need to render
        return;
    }

    BleUI_begin();
    BleUI_header("Bluetooth Keyboard");

    if (keyboardConnected)
    {
        BleUI_status(BLEUI_DOT_FILLED, "Connected");
        BleUI_text(0, "Typing goes to your computer.");
        BleUI_text(1, "Power off to end the session.");
    }
    else
    {
        BleUI_status(BLEUI_DOT_HOLLOW, "Waiting for connection");
        BleUI_text(0, "Open Bluetooth on your computer and");
        BleUI_text(1, "pair with \"Micro Journal 7\".");
    }

    BleUI_footer("Hold Esc", "Exit");
    BleUI_end();
}

void KeyboardScreen_keyboard(uint8_t modifier, uint8_t reserved, uint8_t *keycodes)
{
    bool escapeInReport = false;
    KeyReport filteredReport = {};
    filteredReport.modifiers = modifier;
    filteredReport.reserved = reserved;

    int filteredIndex = 0;
    for (int i = 0; i < 6; i++)
    {
        if (keycodes[i] == HID_KEY_ESCAPE)
            escapeInReport = true;
        else if (keycodes[i] != 0 && filteredIndex < 6)
            filteredReport.keys[filteredIndex++] = keycodes[i];
    }

    // EPD receives raw HID reports rather than separate key events. Keep ESC
    // out of the forwarded report until its duration is known, but continue
    // forwarding every other held key and modifier while ESC is down.
    if (escapeInReport && !escPressed)
    {
        escPressedAt = millis();
        escPressed = true;
    }

    bool sendShortEscape = false;
    if (!escapeInReport && escPressed)
    {
        const uint32_t heldFor = millis() - escPressedAt;
        escPressed = false;

        if (heldFor >= ESC_LONG_PRESS_MS)
        {
            KeyboardScreen_exitBleKeyboardMode();
            return;
        }

        sendShortEscape = true;
    }

    if (!bleKeyboard.isConnected())
        return;

    // A short ESC needs distinct down/up notifications. Preserve the current
    // modifier and other held keys in both reports so the host state remains
    // synchronized during key combinations.
    if (sendShortEscape)
    {
        KeyReport escapeReport = filteredReport;
        for (int i = 0; i < 6; i++)
        {
            if (escapeReport.keys[i] == 0)
            {
                escapeReport.keys[i] = HID_KEY_ESCAPE;
                break;
            }
        }
        bleKeyboard.sendReport(&escapeReport);
        delay(8);
    }

    bleKeyboard.sendReport(&filteredReport);
    _debug("KeyboardScreen_keyboard::%02x %02x %02x %02x %02x %02x %02x %02x\n",
           filteredReport.modifiers,
           filteredReport.reserved,
           filteredReport.keys[0],
           filteredReport.keys[1],
           filteredReport.keys[2],
           filteredReport.keys[3],
           filteredReport.keys[4],
           filteredReport.keys[5]);
}
