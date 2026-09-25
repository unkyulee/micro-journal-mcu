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
    int cursorY = 120;
    writeln(
        (GFXfont *)&systemFont,
        "WEB EDITOR",
        &cursorX, &cursorY,
        display_EPD_framebuffer());
    cursorY += 20;

    //
    String lines[8];
    int count = fileserver_status_lines(lines, 8);
    for (int i = 0; i < count; i++)
    {
        cursorX = 30;
        cursorY += 40;

        if (lines[i].isEmpty())
            continue;

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
