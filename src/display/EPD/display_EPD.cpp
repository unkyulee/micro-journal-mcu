#include "display_EPD.h"
#include "../display.h"
#include "app/app.h"

//
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// screens
#include "ErrorScreen/ErrorScreen.h"
#include "WakeUp/WakeUp.h"
#include "WordProcessor/WordProcessor.h"
#include "Sleep/Sleep.h"
#include "Menu/Menu.h"
#include "Update/Update.h"
#include "KeyboardScreen/KeyboardScreen.h"
#include "SendScreen/SendScreen.h"

// Key strokes for screens other than the word processor are queued here and
// handled inside display_EPD_loop. The display usually runs on the second
// core, and handling a key on the keyboard core while a screen renders lets
// both cores change the status() document at the same time, which crashes.
struct QueuedKey
{
    int key;
    int screen;
};
static QueueHandle_t key_queue = NULL;
static SemaphoreHandle_t display_lock = NULL;
static void display_EPD_render();

// Display Frame Buffer Setup
uint8_t *framebuffer = NULL;
uint8_t *display_EPD_framebuffer()
{
    return framebuffer;
}

int display_EPD_core()
{
    JsonDocument &app = status();
    int screen = app["screen"].as<int>();

    // if app is not ready then run in the main core
    if (!app_ready())
        return 0;

    // IN MENU run it in the main core, so that sync process runs independently
    if (screen == MENUSCREEN)
    {
        int menu = app["menu"]["state"].as<int>();
        if (menu == MENU_SYNC || menu == MENU_WEB)
            return 0;
    }

    // SEND blocks the background core while typing, keep the screen on the main core
    if (screen == SENDSCREEN)
        return 0;

    // by default run at the second core separated from keyboard loop
    return 1;
}

void display_draw_buffer()
{
    epd_poweron();
    epd_draw_grayscale_image(epd_full_screen(), display_EPD_framebuffer());
    memset(display_EPD_framebuffer(), 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    epd_poweroff_all();
}

//
void display_EPD_setup()
{
    // Initialize Framebuffer
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer)
    {
        Serial.println("alloc memory failed !!!");
        while (1)
            ;
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    // key queue between the keyboard core and the display core
    key_queue = xQueueCreate(32, sizeof(QueuedKey));

    // only one core may run the display loop at a time
    display_lock = xSemaphoreCreateMutex();

    // Initialize EPD Screen
    epd_init();
    _log("E-ink display initialized\n");
}

//
void display_EPD_loop()
{
    // process display
    static unsigned int last = 0;
    if (millis() - last <= 150)
        return;

    // The display can move between cores when the screen changes. A screen
    // setup running on both cores at once starts Bluetooth twice, so the
    // other core skips this round while one is still inside.
    if (display_lock == NULL || xSemaphoreTake(display_lock, 0) != pdTRUE)
        return;

    last = millis();

    // handle key strokes on the same core that renders
    int core = display_EPD_core();
    display_EPD_process_keys();

    // a key moved the display to the other core, let that core set it up
    if (display_EPD_core() == core)
        display_EPD_render();

    xSemaphoreGive(display_lock);
}

static void display_EPD_render()
{
    JsonDocument &app = status();
    static int screen_prev = -1;
    int screen = app["screen"].as<int>();

    // Error Screen
    if (screen == ERRORSCREEN)
    {
        // setup only once
        if (screen != screen_prev)
            ErrorScreen_setup();
        else
            // loop
            ErrorScreen_render();
    }

    // WAKE UP SCREEN
    // does nothing and skips immediately to WORDPROCESSOR
    else if (screen == WAKEUPSCREEN)
    {
        // setup only once
        if (screen != screen_prev)
            WakeUp_setup();
        else
            // loop
            WakeUp_render();
    }

    // WORD PROCESSOR
    else if (screen == WORDPROCESSOR)
    {
        // setup only once
        if (screen != screen_prev)
            WP_setup();
        else
            // loop
            WP_render();
    }

    // SLEEP SCREEN
    else if (screen == SLEEPSCREEN)
    {
        // setup only once
        if (screen != screen_prev)
            Sleep_setup();
        else
            // loop
            Sleep_render();
    }

    // MENU SCREEN
    else if (screen == MENUSCREEN)
    {
        // setup only once
        if (screen != screen_prev)
            Menu_setup();
        else
            // loop
            Menu_render();
    }

    // UPDATE
    else if (screen == UPDATESCREEN)
    {
        // setup only once
        if (screen != screen_prev)
            Update_setup();
        else
            // loop
            Update_render();
    }

    // BLE KEYBOARD SCREEN
    else if (screen == KEYBOARDSCREEN)
    {
        // setup only once
        if (screen != screen_prev)
            KeyboardScreen_setup();
        else
            // loop
            KeyboardScreen_render();
    }

    // SEND SCREEN
    else if (screen == SENDSCREEN)
    {
        // setup only once
        if (screen != screen_prev)
            SendScreen_setup();
        else
            // loop
            SendScreen_render();
    }

    //
    app["screen_prev"] = screen;
    screen_prev = screen;
}

// key is int: printable keys above 127 (UTF-8 codepoints) and virtual
// keys like the 1000+ file-change requests must not be narrowed to char
void display_EPD_keyboard(int key, bool pressed, int index)
{
    JsonDocument &app = status();
    int screen = app["screen"].as<int>();

    // the word processor takes key strokes directly to keep typing responsive
    if (screen == WORDPROCESSOR)
    {
        // send the key stroke to word processor
        WP_keyboard(key, pressed, index);
        return;
    }

    // other screens only react to key release, hand it to the display loop
    if (pressed || key_queue == NULL)
        return;

    QueuedKey queued = {key, screen};
    if (xQueueSend(key_queue, &queued, 0) != pdTRUE)
        _log("display_EPD_keyboard:: key queue full, dropping key %d\n", key);
}

// called from display_EPD_loop so key handling never overlaps a render
void display_EPD_process_keys()
{
    if (key_queue == NULL)
        return;

    JsonDocument &app = status();
    QueuedKey queued;
    while (xQueueReceive(key_queue, &queued, 0) == pdTRUE)
    {
        int screen = app["screen"].as<int>();
        int menu = app["menu"]["state"].as<int>();

        // the key was meant for a screen that is no longer shown
        if (queued.screen != screen)
            continue;

        display_EPD_handle_key(queued.key, screen);

        // let the new screen or menu run its setup before taking more keys
        if (app["screen"].as<int>() != screen || app["menu"]["state"].as<int>() != menu)
            break;
    }
}

void display_EPD_handle_key(int key, int screen)
{
    bool pressed = false;

    if (screen == ERRORSCREEN)
    {
        if (!pressed)
            ErrorScreen_keyboard(key);
    }

    else if (screen == WAKEUPSCREEN)
    {
        if (!pressed)
            WakeUp_keyboard(key);
    }

    else if (screen == SLEEPSCREEN)
    {
        if (!pressed)
            Sleep_keyboard(key);
    }

    else if (screen == MENUSCREEN)
    {
        if (!pressed)
            Menu_keyboard(key);
    }

    else if (screen == UPDATESCREEN)
    {
        if (!pressed)
            Update_keyboard(key);
    }

    else if (screen == SENDSCREEN)
    {
        if (!pressed)
            SendScreen_keyboard(key);
    }
}

// GET USBHOST Keyboard Report
// Forward it to Bluetooth Screen
void display_EPD_keyboard_report(uint8_t modifier, uint8_t reserved, uint8_t *keycodes)
{
    JsonDocument &app = status();
    int screen = app["screen"].as<int>();

    if (screen == KEYBOARDSCREEN)
    {
        // send the key stroke to word processor
        KeyboardScreen_keyboard(modifier, reserved, keycodes);
    }
}

GFXfont *display_EPD_font()
{
    return (GFXfont *)&monospace;
}

// assign safe rect that constraints inside the range
Rect_t display_rect(int x, int y, int width, int height)
{
    Rect_t area = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
    };

    // x can't exceed the EPD_WIDTH
    if (area.x + area.width > EPD_WIDTH)
        area.width = EPD_WIDTH - area.x;

    // y can't exceed the EPD_HEIGHT
    if (area.y + area.height > EPD_HEIGHT)
        area.height = EPD_HEIGHT - area.y;

    // width can't be negative
    if (area.width < 0)
        area.width = 0;

    // height can't be negative
    if (area.height < 0)
        area.height = 0;

    //
    // _debug("display_rect:: x: %d, y: %d, width: %d, height: %d\n", x, y, width, height);

    return area;
}

// DISPLAY CONFIGURATION
int _x_init = 10;
int _y_init = 10;
int _line_height = 50;
int _x_pos = 0;
int _y_pos = 0;
int _font_width = 30;

void display_initialize(int x_init, int y_init, int line_height, int font_width)
{
    //
    _x_init = x_init;
    _y_init = y_init;
    _line_height = line_height;
    _font_width = font_width;

    //
    _x_pos = x_init;
    _y_pos = y_init + _line_height;
}

void display_newline()
{
    _x_pos = _x_init;
    _y_pos += _line_height;
}

void display_setline(int row)
{
    _x_pos = _x_init;
    _y_pos = _y_init + _line_height * (row);
}

int display_x()
{
    return _x_pos;
}

int display_y()
{
    return _y_pos;
}

int display_lineheight()
{
    return _line_height;
}

int display_fontwidth()
{
    return _font_width;
}