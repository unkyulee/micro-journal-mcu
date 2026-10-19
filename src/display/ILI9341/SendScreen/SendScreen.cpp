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

#ifdef REV5
#define SEND_DEVICE_NAME "Micro Journal 5"
#else
#define SEND_DEVICE_NAME "Micro Journal 6"
#endif

// SEND screen states
#define SEND_WAITING 0 // waiting for the computer to connect
#define SEND_READY 1   // connected, waiting for a key press to send
#define SEND_SENDING 2 // sending text

static int sendStatePrev = -1;
static int progressPrev = -1;
static bool sendRequested = false;
static String sendResult = "";

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
void SendScreen_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // clear screen
    BleUI_clear(ptft);

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
void SendScreen_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    JsonDocument &app = status();

    // computer disconnected while sending
    if (sendRequested && !bleKeyboard.isConnected())
        app["send_stop"] = true;

    // sending finished or stopped
    if (sendRequested && app["send_finished"].as<bool>())
    {
        sendRequested = false;
        sendResult = app["send_stop"].as<bool>() ? "Send stopped." : "Send completed.";
        sendStatePrev = -1;
    }

    int state = SendScreen_state();

    // while sending only the progress bar changes
    if (state == sendStatePrev)
    {
        if (state == SEND_SENDING && send_progress() != progressPrev)
        {
            progressPrev = send_progress();
            BleUI_progress(ptft, progressPrev);
            BleUI_end(ptft);
        }
        return;
    }
    sendStatePrev = state;

    BleUI_clear(ptft);
    BleUI_header(ptft, "Send", format("File %d", app["config"]["file_index"].as<int>()).c_str());

    if (state == SEND_WAITING)
    {
        BleUI_status(ptft, BLEUI_WAITING, "Waiting for connection");
        BleUI_text(ptft, 0, "Open Bluetooth on your computer and");
        BleUI_text(ptft, 1, "pair with \"" SEND_DEVICE_NAME "\".");
        BleUI_footer(ptft, "Esc", "Exit");
    }
    else if (state == SEND_READY)
    {
        BleUI_status(ptft, BLEUI_CONNECTED, "Connected");
        BleUI_text(ptft, 0, sendResult.isEmpty() ? "Ready to send." : sendResult.c_str());
        BleUI_text(ptft, 1, "It types into the active window.");
        BleUI_footer(ptft, "Any key", "Send", "Esc", "Exit");
    }
    else
    {
        BleUI_status(ptft, BLEUI_ACTIVE, "Sending");
        BleUI_text(ptft, 0, "Typing into the active window.");
        progressPrev = send_progress();
        BleUI_progress(ptft, progressPrev);
        BleUI_footer(ptft, "Any key", "Stop");
    }

    BleUI_end(ptft);
}

//
void SendScreen_keyboard(int key)
{
    JsonDocument &app = status();

    // any key during the process stops sending
    if (sendRequested)
    {
        app["send_stop"] = true;
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
        app["task"] = "send_start";
        app["send_stop"] = false;
        app["send_finished"] = false;
    }
}
