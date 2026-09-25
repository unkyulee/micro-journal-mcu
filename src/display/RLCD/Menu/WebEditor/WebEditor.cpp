#include "WebEditor.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
//
#include "service/FileServer/FileServer.h"

//
void WebEditor_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    _log("WebEditor_setup\n");

    //
    Menu_clear();

    // bring up wifi and the web server in the background
    fileserver_start_request();
}

//
void WebEditor_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // header
    u8->setCursor(0, 50);
    u8->println(" WEB EDITOR ");
    u8->println("");

    //
    String lines[8];
    int count = fileserver_status_lines(lines, 8);
    for (int i = 0; i < count; i++)
    {
        u8->print(" ");
        u8->println(lines[i]);
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
