#pragma once

#include <Arduino.h>
#include <atomic>

#include "service/Editor/EditorLock.h"

//
// The state the two cores share while the device runs.
//
// This used to live in the status() JSON document together with the
// configuration: which screen is showing, which menu, what the background
// core was asked to do. Both cores looked those up by name on every pass of
// their loops while the other core was writing to the same document, and a
// JSON document is not safe to share like that.
//
// Here each of those values is a field of its own. Numbers and flags are
// atomic: one core can write while the other reads. Texts are guarded by a
// lock. Only what really is configuration - the "config" object, the saved
// WiFi networks - stays in the JSON document, which is loaded and saved as
// a whole.
//
// Use it like the JSON fields it replaces:
//
//   appState().screen = MENUSCREEN;
//   if (appState().screen == WORDPROCESSOR) ...
//   _log("screen %d\n", appState().screen.load());   // .load() for printf
//   appState().error = "File System Failed\n";
//   String text = appState().error.get();
//
// Only load and store are used on the atomics (no exchange or arithmetic):
// those are the operations every board supports without a helper library.
//

// A text that one core writes and the other reads.
class GuardedString
{
public:
    void set(const String &value)
    {
        EditorLock lock(_mutex);
        _value = value;
    }

    // a copy, so the caller can keep using it without the lock
    String get()
    {
        EditorLock lock(_mutex);
        return _value;
    }

    bool isEmpty()
    {
        EditorLock lock(_mutex);
        return _value.isEmpty();
    }

    GuardedString &operator=(const String &value)
    {
        set(value);
        return *this;
    }

    GuardedString &operator=(const char *value)
    {
        set(String(value ? value : ""));
        return *this;
    }

private:
    EditorMutex _mutex;
    String _value;
};

// Work the display or the keyboard asks the background core to do.
enum AppTask
{
    TASK_SYNC_START = 0, // run a network backup
    TASK_SEND_START,     // type the file into a computer
    TASK_BLE_SCAN,       // look for bluetooth keyboards
    TASK_COUNT
};

struct AppState
{
    //
    // screens
    //

    // which screen is showing, see display.h (WORDPROCESSOR, MENUSCREEN ...)
    std::atomic<int> screen{0};

    // the screen that was showing in the previous render round
    std::atomic<int> screenPrev{-1};

    // which page of the menu, see display.h (MENU_HOME, MENU_SYNC ...)
    std::atomic<int> menuState{0};

    // someone asks the current screen to redraw itself completely
    std::atomic<bool> clear{false};

    // what the error screen shows
    GuardedString error;

    //
    // requests to the background core
    //
    // One pending flag per task. Each task has exactly one place that picks
    // it up, so asking twice before it starts simply runs it once.
    //
    std::atomic<bool> taskPending[TASK_COUNT];

    void requestTask(AppTask task) { taskPending[task].store(true); }

    void cancelTask(AppTask task) { taskPending[task].store(false); }

    // true once for every request: the caller now owns the task
    bool takeTask(AppTask task)
    {
        if (!taskPending[task].load())
            return false;

        taskPending[task].store(false);
        return true;
    }

    //
    // network backup, see service/Sync
    //
    std::atomic<int> syncState{0};
    GuardedString syncMessage;
    GuardedString syncError;

    //
    // send the text as a keyboard, see service/Send
    //
    std::atomic<bool> sendStop{false};
    std::atomic<bool> sendFinished{false};

    //
    // wifi setup screen, see service/WifiEntry
    //
    std::atomic<int> wifiConfigStatus{0};
    std::atomic<int> wifiConfigIndex{0};

    //
    // bluetooth, see service/BLEServer and keyboard/BLE
    //
    std::atomic<int> bleState{0};
    std::atomic<bool> bleAutoPair{false};
    std::atomic<bool> bleConnected{false};
    GuardedString bleMessage;
    GuardedString bleError;

    //
    // keyboard
    //

    // a usb keyboard is plugged in
    std::atomic<bool> hidUsb{false};

    // a custom key map was loaded from the storage
    std::atomic<bool> keyboardCustom{false};

    // the knob is held down (Rev.4)
    std::atomic<bool> knobLongPressed{false};

    //
    // usb drive mode (Rev.4), see service/MassStorage
    //
    std::atomic<bool> massStorage{false};
    std::atomic<bool> massStorageStarted{false};

    // battery level in percent (Rev.4)
    std::atomic<float> battery{0};

    // colour pickers of the Rev.4 menu
    std::atomic<int> selectedBackgroundColorIndex{0};
    std::atomic<int> selectedFontColorColorIndex{0};

    //
    // Settings that are needed on every key press or every pass of the
    // main loop. They are copies of values in the "config" JSON object,
    // refreshed whenever the configuration is loaded or saved (see
    // config_cache_refresh), so the hot paths never search the document.
    //

    // "keyboard_layout": "US", "KR", "INT" ...
    GuardedString keyboardLayout;

    // a bluetooth keyboard is paired ("ble": { "address": ... })
    std::atomic<bool> bleConfigured{false};

    AppState()
    {
        for (int i = 0; i < TASK_COUNT; i++)
            taskPending[i].store(false);
    }
};

// the one instance
AppState &appState();
