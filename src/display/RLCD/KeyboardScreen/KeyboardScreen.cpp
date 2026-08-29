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

// Whether the last KeyboardScreen_render() actually changed anything visible - the
// caller uses this to decide whether the (expensive, full 30KB SPI) panel
// refresh is worth doing this tick.
static bool needsDisplay = true;

#if defined(KEYPAD_68)
#define TOTAL_KEYS 72
#define KNOB_INDEX 69
int _usb_keyboard_layers[2][72] = {

    {// normal layers
     KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', KEY_DELETE,
     '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', KEY_PAGE_UP,
     KEY_CAPS_LOCK, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\', KEY_PAGE_DOWN,
     KEY_LEFT_SHIFT, '`', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_LEFT_SHIFT, KEY_UP_ARROW, KEY_END,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, ' ', KEY_RIGHT_ALT, KEY_F24, KEY_HOME, KEY_LEFT_ARROW, KEY_DOWN_ARROW, KEY_RIGHT_ARROW,
     0},

    {// extra layer
     KEY_ESC, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, '\b', KEY_DELETE,
     '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', KEY_PRTSC,
     KEY_CAPS_LOCK, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\', KEY_PAGE_DOWN,
     KEY_LEFT_SHIFT, '`', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_LEFT_SHIFT, KEY_UP_ARROW, KEY_END,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, ' ', KEY_RIGHT_ALT, KEY_F24, KEY_HOME, KEY_LEFT_ARROW, KEY_DOWN_ARROW, KEY_RIGHT_ARROW,
     0},

};
#endif

// Remember the BLE key chosen on key-down. Layer state can change before the
// physical key is released, so resolving both events independently can leave a
// key stuck on the host.
static int activeBleKeys[TOTAL_KEYS] = {};
static int bleLayer = 0;
static bool sendLeftPressed = false;
static bool sendRightPressed = false;
static bool sendChordActive = false;

static int KeyboardScreen_resolveKey(int keypadKey, int mappedKey)
{
    // Characters arriving here have already passed through keyboard.json,
    // shift and locale conversion. The BLE table remains authoritative for
    // modifiers/navigation keys represented internally as control codes/zero.
    if ((keypadKey >= 32 && keypadKey <= 126) ||
        keypadKey == '\b' || keypadKey == '\t' || keypadKey == '\n' || keypadKey == 127)
        return keypadKey;

    return mappedKey;
}

#if defined(KEYPAD_48)
/*
Find the keycode from
https://github.com/arduino-libraries/Keyboard/blob/master/src/Keyboard.h

LAYER KEY: #define KEY_F24           0xFB
*/
#define TOTAL_KEYS 49
#define KNOB_INDEX 48
int _usb_keyboard_layers[3][49] = {

    {// normal layers
     KEY_ESC, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '\b',
     '\t', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
     KEY_LEFT_SHIFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_RIGHT_SHIFT,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, KEY_F23, KEY_F24, ' ', ' ', KEY_LEFT_ARROW, KEY_DOWN_ARROW, KEY_UP_ARROW, KEY_RIGHT_ARROW, '\n',
    0},

    {// lower
     KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', KEY_DELETE,
     '`', 'a', 's', 'd', 'f', 'g', 'h', 'j', '-', '=', '[', ']',
     KEY_LEFT_SHIFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_RIGHT_SHIFT,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, KEY_F23, KEY_F24, ' ', ' ', KEY_HOME, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_END, '\n',
    0},

    {// raise
     KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
     '\\', 'a', 's', 'd', 'f', 'g', 'h', 'j', '-', '=', '[', ']',
     KEY_LEFT_SHIFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_RIGHT_SHIFT,
     KEY_LEFT_CTRL, KEY_LEFT_GUI, KEY_LEFT_ALT, KEY_F23, KEY_F24, ' ', ' ', KEY_HOME, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_END, '\n',
    0},

};
#endif

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
void KeyboardScreen_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // clear screen
    display->clearDisplay();

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
#if defined(KEYPAD_68)
    const char *keys[] = {"main", "alt"};
    keyboard_config_load(
        "/keyboard_usb.json",
        (int *)_usb_keyboard_layers,
        TOTAL_KEYS,
        keys,
        2);
#elif defined(KEYPAD_48)
    const char *keys[] = {"main", "lower", "raise"};
    keyboard_config_load(
        "/keyboard_usb.json",
        (int *)_usb_keyboard_layers,
        TOTAL_KEYS,
        keys,
        3);
#endif

    

    // Setup Bluetooth Keyboard
    bleKeyboard.setName("Micro Journal");
    bleKeyboard.begin();
    _log("Bluetooth Keyboard Started\n");
}

//
void KeyboardScreen_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
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
        display->clearDisplay();
        keyboardConnectedPrev = keyboardConnected;

        // Screen dimensions
        int screenW = 400;
        int screenH = 300;

        //
        u8->setFont(u8g2_font_courB14_tf);

        // Title
        u8->setCursor(10, 30);
        u8->print("BLUETOOTH KEYBOARD");

        // Status Box
        u8->setCursor(10, 80);
        if (keyboardConnected)
        {
            u8->print("Status: Connected");
        }
        else
        {
            u8->print("Status: Waiting...");
        }

        // Instructions
        u8->setCursor(10, 145);
        u8->print("Long press ESC to end session.");

        u8->setCursor(10, 175);
        u8->print("Press top-left and top-right keys");

        u8->setCursor(10, 205);
        u8->print("simultaneously to SEND.");

        u8->setCursor(10, 235);
        u8->print("Hold ESC for 2 seconds to exit.");

        // decide whether the panel actually needs to be pushed over SPI this tick
        needsDisplay = true;
    }
    else
        needsDisplay = false;
}

bool KeyboardScreen_needsDisplay()
{
    return needsDisplay;
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
    // former early return made the corner-key chord unreachable.
#if defined(KEYPAD_68)
    constexpr int SEND_RIGHT_INDEX = 14;
#else
    constexpr int SEND_RIGHT_INDEX = 11;
#endif
    if (bleKeyboard.isConnected() && (index == 0 || index == SEND_RIGHT_INDEX))
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

    // Handle ESC before checking the BLE connection so keyboard mode can also
    // be disabled while the ESP32 is waiting to be paired.
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
    //
    _log("KeyboardScreen_play: %s\n", macro.c_str());

    // Split the macro string by commas and process each token
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
