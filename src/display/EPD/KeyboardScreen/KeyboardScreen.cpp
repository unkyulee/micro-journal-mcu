#include "KeyboardScreen.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"

//
#include "../display_EPD.h"
#include "service/Send/Send.h"

//
#include <NimBLEDevice.h>
#include <BleKeyboard.h>

BleKeyboard bleKeyboard;
int keyboardConnectedPrev = -1;

static constexpr uint8_t HID_KEY_ESCAPE = 0x29;
static constexpr uint8_t HID_KEY_PAUSE = 0x48;
static constexpr uint32_t ESC_LONG_PRESS_MS = 2000;
static volatile bool escPressed = false;
static volatile uint32_t escPressedAt = 0;
static bool bleKeyboardExitStarted = false;
static bool pausePressed = false;

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

// function reference used for SEND
void _send_key(int key)
{
    _debug("_send_key: %d\n", key);

    // If needed, convert line endings
    char c = (char)key;
    if (c == '\n')
    {
        bleKeyboard.press('\n');
        delay(5);
        bleKeyboard.release('\n');
    }
    else if (isPrintable(c) || c == '\t') // Avoid sending non-printable control characters
    {
        bleKeyboard.press(c);
        delay(5);
        bleKeyboard.release(c);
    }

    // BLE requires delay per key press to be stable
    delay(10);
}

//
void KeyboardScreen_setup()
{
    //
    _log("Keyboard Screen Setup\n");

    // register send function
    send_register(_send_key);

    //
    keyboardConnectedPrev = -1;
    escPressed = false;
    escPressedAt = 0;
    bleKeyboardExitStarted = false;
    pausePressed = false;

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

    // Start Rendering Screen
    epd_poweron();
    epd_clear_quick(epd_full_screen(), 4, 50);

    // Draw text
    int32_t x = 18;
    int32_t y = 50;
    write_string(display_EPD_font(), "Bluetooth Keyboard Mode", &x, &y, NULL);

    //
    x = 18;
    y += 30;
    if (keyboardConnected)
    {
        write_string((GFXfont *)&systemFont, "Keyboard Connected", &x, &y, NULL);
    }
    else
    {
        write_string((GFXfont *)&systemFont, "Waiting for connection ...", &x, &y, NULL);
    }

    x = 18;
    y += 30;
    write_string((GFXfont *)&systemFont, "Press Left Knob  will send the text", &x, &y, NULL);

    x = 18;
    y += 15;
    write_string((GFXfont *)&systemFont, "Turn off the device to end the writing session", &x, &y, NULL);

    x = 18;
    y += 15;
    write_string((GFXfont *)&systemFont, "Hold ESC for 2 seconds to exit", &x, &y, NULL);

    //
    epd_poweroff();
}

void KeyboardScreen_keyboard(uint8_t modifier, uint8_t reserved, uint8_t *keycodes)
{
    bool escapeInReport = false;
    bool pauseInReport = false;
    KeyReport filteredReport = {};
    filteredReport.modifiers = modifier;
    filteredReport.reserved = reserved;

    int filteredIndex = 0;
    for (int i = 0; i < 6; i++)
    {
        if (keycodes[i] == HID_KEY_ESCAPE)
            escapeInReport = true;
        else if (keycodes[i] == HID_KEY_PAUSE)
            pauseInReport = true;
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

    // PAUSE is the SEND command. Trigger only on its key-down edge and remove
    // it from the BLE report without suppressing other simultaneously held keys.
    const bool startSend = pauseInReport && !pausePressed;
    pausePressed = pauseInReport;

    if (!bleKeyboard.isConnected())
        return;

    // any key pressed is going to stop sending text
    JsonDocument &app = status();
    // A PAUSE release produces an otherwise empty report. Do not let that
    // immediately cancel the SEND task that its preceding press just started.
    if (filteredIndex > 0 || modifier != 0 || sendShortEscape)
        app["send_stop"] = true;

    if (startSend)
    {
        app["task"] = "send_start";
        app["send_stop"] = false;
        app["send_finished"] = false;
    }

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
