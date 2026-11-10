#include "SendScreen.h"
#include "app/app.h"
#include "display/display.h"

//
#include "service/Send/Send.h"
#include "../BleUI/BleUI.h"

//
#include <NimBLEDevice.h>
#include <BleKeyboard.h>

// BLE keyboard instance is owned by the keyboard screen
extern BleKeyboard bleKeyboard;

#define SEND_DEVICE_NAME "Micro Journal"

// SEND screen states
#define SEND_WAITING 0 // waiting for the computer to connect
#define SEND_READY 1   // connected, waiting for a key press to send
#define SEND_SENDING 2 // sending text

static int sendStatePrev = -1;
static int progressPrev = -1;
static bool sendRequested = false;
static String sendResult = "";
static bool needsDisplay = true;

// function reference used for SEND
static void SendScreen_sendKey(int key)
{
    char c = (char)key;
    if (c == '\n' || isPrintable(c) || c == '\t') // Avoid sending non-printable control characters
    {
        bleKeyboard.press(c);
        delay(5);
        bleKeyboard.release(c);
    }

    // BLE requires delay per key press to be stable
    delay(10);
}

static int SendScreen_state()
{
    if (!bleKeyboard.isConnected())
        return SEND_WAITING;

    if (sendRequested)
        return SEND_SENDING;

    return SEND_READY;
}

// Bluetooth stays registered as a keyboard, restart to leave cleanly
static void SendScreen_exit()
{
    _log("Leaving SEND screen\n");

    if (bleKeyboard.isConnected())
        bleKeyboard.releaseAll();

    delay(50);
    ESP.restart();
}

//
void SendScreen_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // clear screen
    display->clearDisplay();

    // register send function
    send_register(SendScreen_sendKey);

    // reset flags
    sendStatePrev = -1;
    sendRequested = false;
    sendResult = "";

    // Setup Bluetooth Keyboard
    bleKeyboard.setName(SEND_DEVICE_NAME);
    bleKeyboard.begin();
    _log("SEND: Bluetooth Keyboard Started\n");
}

//
void SendScreen_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // computer disconnected while sending
    if (sendRequested && !bleKeyboard.isConnected())
        appState().sendStop = true;

    // sending finished or stopped
    if (sendRequested && appState().sendFinished.load())
    {
        sendRequested = false;
        sendResult = appState().sendStop.load() ? "Send stopped." : "Send completed.";
        sendStatePrev = -1;
    }

    int state = SendScreen_state();

    // while sending only the progress bar changes
    if (state == sendStatePrev)
    {
        needsDisplay = false;
        if (state == SEND_SENDING && send_progress() != progressPrev)
        {
            progressPrev = send_progress();
            BleUI_progress(display, u8, progressPrev);
            needsDisplay = true;
        }
        return;
    }
    sendStatePrev = state;
    needsDisplay = true;

    BleUI_clear(display);
    BleUI_header(display, u8, "Send", format("File %d", app["config"]["file_index"].as<int>()).c_str());

    if (state == SEND_WAITING)
    {
        BleUI_status(display, u8, BLEUI_DOT_HOLLOW, "Waiting for connection");
        BleUI_text(u8, 0, "Open Bluetooth on your computer and");
        BleUI_text(u8, 1, "pair with \"" SEND_DEVICE_NAME "\".");
        BleUI_footer(display, u8, "Esc", "Exit");
    }
    else if (state == SEND_READY)
    {
        BleUI_status(display, u8, BLEUI_DOT_FILLED, "Connected");
        BleUI_text(u8, 0, sendResult.isEmpty() ? "Ready to send." : sendResult.c_str());
        BleUI_text(u8, 1, "It types into the active window.");
        BleUI_footer(display, u8, "Any key", "Send", "Esc", "Exit");
    }
    else
    {
        BleUI_status(display, u8, BLEUI_DOT_FILLED, "Sending");
        BleUI_text(u8, 0, "Typing into the active window.");
        progressPrev = send_progress();
        BleUI_progress(display, u8, progressPrev);
        BleUI_footer(display, u8, "Any key", "Stop");
    }
}

bool SendScreen_needsDisplay()
{
    return needsDisplay;
}

//
void SendScreen_keyboard(int key)
{
    JsonDocument &app = status();

    // any key during the process stops sending
    if (sendRequested)
    {
        appState().sendStop = true;
        return;
    }

    // ESC leaves the SEND screen
    if (key == MENU || key == 27)
    {
        SendScreen_exit();
        return;
    }

    // any other key starts sending once connected
    if (bleKeyboard.isConnected())
    {
        bleKeyboard.releaseAll();
        send_progress_reset();
        sendRequested = true;
        sendResult = "";
        appState().requestTask(TASK_SEND_START);
        appState().sendStop = false;
        appState().sendFinished = false;
    }
}
