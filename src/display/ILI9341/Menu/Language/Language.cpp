#include "Language.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "../../WordProcessor/WordProcessor.h"

// Simple input-language chooser for Rev.6.
//
//   (L) Latin Letters -> US keyboard layout, Latin font (WP_FONTS index 0)
//   (H) Hangul         -> KR keyboard layout, Korean font (WP_FONTS index 2)
//
// Selecting "KR" makes the ko locale emit Hangul jamo, which the Editor's
// HangulComposer assembles into syllables; font index 2 (korean20) renders
// them full width. This is the same wiring the (REV5) Layout menu uses, packed
// into a two-choice menu.

void Language_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // Clear the screen
    Menu_clear();
    ptft->fillScreen(TFT_BLACK);
}

void Language_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    JsonDocument &app = status();

    // currently selected font (0 = Latin, 2 = Korean) to highlight the choice
    int current_font = app["config"]["font"].as<int>();

    ptft->setTextColor(TFT_WHITE, TFT_BLACK);
    ptft->setCursor(0, 30, 2);
    ptft->print(" SELECT INPUT LANGUAGE");

    ptft->setTextColor(current_font == 2 ? TFT_WHITE : TFT_GREEN, TFT_BLACK);
    ptft->setCursor(0, 70, 2);
    ptft->print(" [L] Latin Letters");

    ptft->setTextColor(current_font == 2 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    ptft->setCursor(0, 95, 2);
    ptft->print(" [H] Hangul");

    ptft->setTextColor(TFT_WHITE, TFT_BLACK);
    ptft->setCursor(0, 140, 2);
    ptft->print(" [B] BACK");
}

void Language_keyboard(char key)
{
    JsonDocument &app = status();

    // normalize to lower case
    if (key >= 'A' && key <= 'Z')
        key = key - 'A' + 'a';

    // Latin letters
    if (key == 'l')
    {
        app["config"]["keyboard_layout"] = "US";
        app["config"]["font"] = 0;
        config_save();

        app["screen"] = WORDPROCESSOR;
        app["menu"]["state"] = MENU_HOME;
    }

    // Hangul
    else if (key == 'h')
    {
        app["config"]["keyboard_layout"] = "KR";
        app["config"]["font"] = 2;
        config_save();

        app["screen"] = WORDPROCESSOR;
        app["menu"]["state"] = MENU_HOME;
    }

    // back to home menu
    else if (key == 'b' || key == '\b' || key == 27)
    {
        app["menu"]["state"] = MENU_HOME;
    }
}
