#include "EditorTimers.h"
#include "Editor.h"
#include "app/app.h"
#include "display/display.h"

#include <atomic>

// when the text last changed, as seen by the render loop
static unsigned long autosave_last = 0;
static int autosave_buffer_size = -1;

// when the writer last did something
// written by the keyboard core, read by the display core
static std::atomic<uint32_t> activity_last(0);

//
void editor_timers_reset()
{
    autosave_last = millis();
    autosave_buffer_size = -1;
    activity_last = millis();
}

//
void editor_activity()
{
    activity_last = millis();
}

//
void editor_autosave(unsigned long idleMs)
{
    // the render loop's copy of the editor, see Editor::snapshot()
    Editor &view = Editor::view();

    // every change to the text restarts the timer
    int bufferSize = view.getBufferSize();
    if (bufferSize != autosave_buffer_size)
    {
        autosave_buffer_size = bufferSize;
        autosave_last = millis();
    }

    // quiet for long enough
    if (millis() - autosave_last > idleMs)
    {
        autosave_last = millis();

        if (!view.saved)
            Editor::getInstance().saveFile();
    }
}

//
void editor_sleep_check(unsigned long idleMs)
{
    // text that still has to be saved means the writer was just typing
    if (!Editor::view().saved)
        activity_last = millis();

    if (millis() - activity_last.load() > idleMs)
    {
        // start over, so waking up doesn't fall asleep again right away
        activity_last = millis();

        JsonDocument &app = status();
        appState().screen = SLEEPSCREEN;
    }
}
