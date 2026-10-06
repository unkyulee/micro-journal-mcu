#include "WebEditor.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include <display/EPD/display_EPD.h>

//
#include "service/FileServer/FileServer.h"

//
void WebEditor_setup()
{
    _log("WebEditor_setup\n");

    //
    Menu_clear();

    // bring up wifi and the web server in the background
    fileserver_start_request();
}

//
void WebEditor_render()
{
    // header
    int cursorX = 10;
    int cursorY = 100;
    writeln(
        (GFXfont *)&systemFont,
        "DRIVE MODE",
        &cursorX, &cursorY,
        display_EPD_framebuffer());
    cursorY += 10;

    // the font is 33 pixels tall, 44 keeps the lines clearly apart
    // and the longest message (10 lines) still ends above the bottom edge
    String lines[10];
    int count = fileserver_status_lines(lines, 10);
    for (int i = 0; i < count; i++)
    {
        cursorX = 30;

        // an empty line is a half height gap
        if (lines[i].isEmpty())
        {
            cursorY += 22;
            continue;
        }

        cursorY += 44;

        writeln(
            (GFXfont *)&systemFont,
            lines[i].c_str(),
            &cursorX, &cursorY,
            display_EPD_framebuffer());
    }
}

//
void WebEditor_keyboard(char key)
{
    int state = fileserver_state();

    // ESC finishes the session, any key leaves the error screen
    if (state == FILESERVER_ERROR || key == 27 || key == MENU)
    {
        fileserver_stop_request();
    }
}
