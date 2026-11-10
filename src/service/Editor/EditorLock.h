#pragma once

#include <Arduino.h>

//
// The editor's lock.
//
// The keyboard runs on one core and the display on the other, and both reach
// into the editor: keys edit the buffer, the display core auto-repeats keys,
// saves the file when typing pauses, and reads the text to draw it. This
// mutex makes those take turns.
//
// It is recursive because the editor calls itself while holding it: a key
// can fill the buffer, which saves the file and swaps the window, all inside
// the same keystroke.
//
// Never take it from an interrupt.
//

#if defined(BOARD_ESP32_S3)

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class EditorMutex
{
public:
    EditorMutex() { _handle = xSemaphoreCreateRecursiveMutex(); }

    void lock()
    {
        if (_handle)
            xSemaphoreTakeRecursive(_handle, portMAX_DELAY);
    }

    void unlock()
    {
        if (_handle)
            xSemaphoreGiveRecursive(_handle);
    }

private:
    SemaphoreHandle_t _handle = NULL;
};

#elif defined(BOARD_PICO)

#include <pico/mutex.h>

class EditorMutex
{
public:
    EditorMutex() { recursive_mutex_init(&_mutex); }
    void lock() { recursive_mutex_enter_blocking(&_mutex); }
    void unlock() { recursive_mutex_exit(&_mutex); }

private:
    recursive_mutex_t _mutex;
};

#else

// single core build, nothing to guard against
class EditorMutex
{
public:
    void lock() {}
    void unlock() {}
};

#endif

// Holds the lock for as long as the object lives:
//   EditorLock lock(mutex);
class EditorLock
{
public:
    explicit EditorLock(EditorMutex &mutex) : _mutex(mutex) { _mutex.lock(); }
    ~EditorLock() { _mutex.unlock(); }

    EditorLock(const EditorLock &) = delete;
    EditorLock &operator=(const EditorLock &) = delete;

private:
    EditorMutex &_mutex;
};
