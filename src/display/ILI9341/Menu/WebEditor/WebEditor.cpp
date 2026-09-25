#include "WebEditor.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
//
#include "service/FileServer/FileServer.h"

//
void WebEditor_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    _log("WebEditor_setup\n");

    //
    Menu_clear();

    // bring up wifi and the web server in the background
    fileserver_start_request();
}

//
void WebEditor_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // header
    ptft->setCursor(0, 30, 2);
    ptft->setTextSize(1);
    ptft->setTextColor(TFT_WHITE, TFT_BLACK);
    ptft->println(" WEB EDITOR ");
    ptft->println("");

    //
    String lines[8];
    int count = fileserver_status_lines(lines, 8);
    for (int i = 0; i < count; i++)
    {
        // the address stands out so it is easy to type in the browser
        if (lines[i].startsWith("http"))
            ptft->setTextColor(TFT_GREEN, TFT_BLACK);
        else if (fileserver_state() == FILESERVER_ERROR && i == 0)
            ptft->setTextColor(TFT_WHITE, TFT_RED);
        else
            ptft->setTextColor(TFT_WHITE, TFT_BLACK);

        ptft->print(" ");
        ptft->println(lines[i]);
    }
    ptft->setTextColor(TFT_WHITE, TFT_BLACK);
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
