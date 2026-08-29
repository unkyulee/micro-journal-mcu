#include "KeyboardScreen.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"
#include "keyboard/keyboard.h"

//
#include "service/Editor/Editor.h"
#include "service/Send/Send.h"

//
#include <NimBLEDevice.h>
#include <BleKeyboard.h>

BleKeyboard bleKeyboard;
int keyboardConnectedPrev = -1;

// Holding ESC exits the persistent BLE keyboard mode. The timer is checked by
// KeyboardScreen_render() so the device restarts without waiting for key-up.
static constexpr uint32_t ESC_LONG_PRESS_MS = 3000;
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

    delay(50); // allow the filesystem write and final BLE report to settle
    ESP.restart();
}

/*
Find the keycode from
https://github.com/arduino-libraries/Keyboard/blob/master/src/Keyboard.h

LAYER KEY: #define KEY_F24           0xFB
*/
#define TOTAL_KEYS 48
int _usb_keyboard_layers[3][TOTAL_KEYS] = {

    {// normal layers
     KEY_ESC, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '\b',
     '\t', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
     KEY_LEFT_SHIFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_RIGHT_SHIFT,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, KEY_F23, KEY_F24, ' ', ' ', KEY_LEFT_ARROW, KEY_DOWN_ARROW, KEY_UP_ARROW, KEY_RIGHT_ARROW, '\n'},

    {// lower
     KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', KEY_DELETE,
     '`', 'a', 's', 'd', 'f', 'g', 'h', 'j', '-', '=', '[', ']',
     KEY_LEFT_SHIFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_RIGHT_SHIFT,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, KEY_F23, KEY_F24, ' ', ' ', KEY_HOME, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_END, '\n'},

    {// raise
     KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
     '\\', 'a', 's', 'd', 'f', 'g', 'h', 'j', '-', '=', '[', ']',
     KEY_LEFT_SHIFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_PRTSC,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, KEY_F23, KEY_F24, ' ', ' ', KEY_HOME, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_END, '\n'},

};

// A release must use the exact key selected on press. Otherwise releasing a
// layer key first changes the lookup table and can leave the original BLE key
// stuck down (for example F1-down followed by 1-up).
static int activeBleKeys[TOTAL_KEYS] = {};
static int bleLayer = 0;
static bool sendLeftPressed = false;
static bool sendRightPressed = false;
static bool sendChordActive = false;

static int KeyboardScreen_resolveKey(int keypadKey, int mappedKey)
{
    // The keypad has already applied keyboard.json, shift and locale handling.
    // Keep that result for characters; use the BLE table for modifiers and
    // navigation keys, whose keypad representation is an internal control code
    // (or zero).
    if ((keypadKey >= 32 && keypadKey <= 126) ||
        keypadKey == '\b' || keypadKey == '\t' || keypadKey == '\n' || keypadKey == 127)
        return keypadKey;

    return mappedKey;
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
void KeyboardScreen_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // clear screen
    ptft->fillScreen(TFT_BLACK);

    // register send function
    send_register(_send_key);

    // reset flags
    keyboardConnectedPrev = -1;
    escPressed = false;
    escPressedAt = 0;
    bleKeyboardExitStarted = false;
    bleLayer = 0;
    memset(activeBleKeys, 0, sizeof(activeBleKeys));
    sendLeftPressed = false;
    sendRightPressed = false;
    sendChordActive = false;

    // load keyboard layout
    // Load Custom Keybaord Layout
    const char *keys[] = {"main", "lower", "raise"};
    keyboard_config_load(
        "/keyboard_usb.json",
        (int *)_usb_keyboard_layers,
        48,
        keys,
        3);

    // Setup Bluetooth Keyboard
    bleKeyboard.setName("Micro Journal 6");
    bleKeyboard.begin();
    _log("Bluetooth Keyboard Started\n");
}

//
void KeyboardScreen_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    if (escPressed && millis() - escPressedAt >= ESC_LONG_PRESS_MS)
    {
        KeyboardScreen_exitBleKeyboardMode();
        return;
    }

    bool keyboardConnected = bleKeyboard.isConnected();

    // Only redraw screen when connection status changes
    if (keyboardConnected != keyboardConnectedPrev)
    {
        ptft->fillScreen(TFT_BLACK);
        keyboardConnectedPrev = keyboardConnected;

        // Screen dimensions
        int screenW = ptft->width();
        int screenH = ptft->height();

        // Title
        ptft->setTextSize(2);
        ptft->setTextColor(TFT_WHITE, TFT_BLACK);
        ptft->setCursor(20, 10);
        ptft->println("BLUETOOTH KEYBOARD");

        // Status Box
        ptft->setTextSize(2);
        ptft->setCursor(45, 60);
        if (keyboardConnected)
        {
            ptft->setTextColor(TFT_GREEN, TFT_BLACK);
            ptft->print("Status: Connected");
        }
        else
        {
            ptft->setTextColor(TFT_YELLOW, TFT_BLACK);
            ptft->print("Status: Waiting...");
        }

        // Instruction Box
        ptft->drawRoundRect(20, 120, screenW - 40, 100, 8, TFT_WHITE);
        ptft->setTextSize(1);
        ptft->setTextColor(TFT_CYAN, TFT_BLACK);
        ptft->setCursor(40, 140);
        ptft->println("Turn off device to end session.");
        ptft->setCursor(40, 160);
        ptft->println("Press top-left and top-right keys");
        ptft->setCursor(40, 180);
        ptft->println("simultaneously to SEND.");
        ptft->setCursor(40, 200);
        ptft->println("Hold ESC for 2 seconds to exit.");
    }
}

// keyboard message will come from Rev.6 via this function.
void KeyboardScreen_keyboard(int key, bool pressed, int index)
{
    if (index < 0 || index >= TOTAL_KEYS)
    {
        _log("KeyboardScreen_keyboard: invalid key index %d\n", index);
        return;
    }

    // Track the SEND chord before ESC handling; index 0 is also ESC, and the
    // former early return made the advertised corner-key chord unreachable.
    if (bleKeyboard.isConnected() && (index == 0 || index == 11))
    {
        if (index == 0)
            sendLeftPressed = pressed;
        else
            sendRightPressed = pressed;

        if (!sendChordActive && sendLeftPressed && sendRightPressed)
        {
            sendChordActive = true;
            escPressed = false;
            memset(activeBleKeys, 0, sizeof(activeBleKeys));
            bleKeyboard.releaseAll();
            JsonDocument &app = status();
            app["task"] = "send_start";
            app["send_stop"] = false;
            app["send_finished"] = false;
            return;
        }

        if (sendChordActive)
        {
            if (!sendLeftPressed && !sendRightPressed)
                sendChordActive = false;
            return;
        }
    }

    // Handle ESC before checking the BLE connection. This makes it possible to
    // leave keyboard mode even while the ESP32 is still waiting to be paired.
    const bool isEscape = key == MENU || key == 27 || key == KEY_ESC;
    if (isEscape)
    {
        if (pressed)
        {
            if (!escPressed)
            {
                escPressedAt = millis();
                escPressed = true;
            }
        }
        else if (escPressed)
        {
            const uint32_t heldFor = millis() - escPressedAt;
            escPressed = false;

            if (heldFor >= ESC_LONG_PRESS_MS)
            {
                KeyboardScreen_exitBleKeyboardMode();
            }
            else if (bleKeyboard.isConnected())
            {
                // Delay the normal ESC report until key-up so a long press does
                // not also send ESC to the connected computer.
                bleKeyboard.press(KEY_ESC);
                bleKeyboard.release(KEY_ESC);
            }
        }

        return;
    }

    // no need any action when bluetooth keyboard is not connected
    if (!bleKeyboard.isConnected())
        return;

    // any key pressed is going to stop sending text
    JsonDocument &app = status();
    app["send_stop"] = true;

    // LOWER is pressed
    if (_usb_keyboard_layers[0][index] == KEY_F24)
    {
        if (pressed)
            bleLayer = 1;
        else
            bleLayer = 0;

        // layer key does not count a key press
        // ignore the layer key press
        return;
    }

    // RAISE is clicked
    if (_usb_keyboard_layers[0][index] == KEY_F23)
    {
        if (pressed)
            bleLayer = 2;
        else
            bleLayer = 0;

        // layer key does not count a key press
        // ignore the layer key press
        return;
    }

    //
    if (pressed)
    {
        key = KeyboardScreen_resolveKey(key, _usb_keyboard_layers[bleLayer][index]);
        activeBleKeys[index] = key;
    }
    else
    {
        // Do not resolve the release using the current layer. It may already
        // have changed since this physical key was pressed.
        int pressedKey = activeBleKeys[index];
        activeBleKeys[index] = 0;
        key = pressedKey != 0
                  ? pressedKey
                  : KeyboardScreen_resolveKey(key, _usb_keyboard_layers[bleLayer][index]);
    }
    _debug("[KeyboardScreen_keyboard] layer %d index %d key %d pressed %d\n", bleLayer, index, key, pressed);

    // ignore dead keys
    if (key == 0)
        return;

    // Check for Macro Keys
    if (key >= 2000 && key <= 2100)
    {
        // run macro when key is released
        if (!pressed)
        {
            int macroIndex = key - 2000;
            String key = format("MACRO_%d", macroIndex);
            _log("Macro Key Clicked: %d\n", macroIndex);
            if (app[key].is<String>())
            {
                String macroString = app[key].as<String>();
                _debug(macroString.c_str());
                KeyboardScreen_play(macroString);
            }
        }

        return;
    }

    //
    /////////////
    if (pressed)
        bleKeyboard.press(key);
    else
        bleKeyboard.release(key);
}

void KeyboardScreen_play(String macro)
{
    int start = 0;
    while (start < macro.length())
    {
        int end = macro.indexOf(',', start);
        if (end == -1)
            end = macro.length();

        String token = macro.substring(start, end);
        token.trim(); // remove spaces

        if (token.startsWith("!PRESS_"))
        {
            String keyName = token.substring(7); // after "!PRESS_"
            int keyCode = keyboard_convert_HID(keyName);
            if (keyCode != 0)
                bleKeyboard.press(keyCode);
        }
        else if (token.startsWith("!RELEASE_"))
        {
            String keyName = token.substring(9); // after "!RELEASE_"
            int keyCode = keyboard_convert_HID(keyName);
            if (keyCode != 0)
                bleKeyboard.release(keyCode);
        }
        else
        {
            int keyCode = keyboard_convert_HID(token);
            if (keyCode != 0)
            {
                bleKeyboard.press(keyCode);
                delay(30); // small delay for reliability
                bleKeyboard.release(keyCode);
            }
        }

        start = end + 1;
        delay(30); // delay between each token
    }
}
