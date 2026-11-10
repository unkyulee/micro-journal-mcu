#include "WordProcessor.h"
#include "app/app.h"
#include "keyboard/keyboard.h"
#include "display/display.h"
#include "display/ILI9341/display_ILI9341.h"

//
#include "service/Editor/Editor.h"
#include "service/Editor/EditorTimers.h"

//
int STATUSBAR_Y = 224;
int screen_width = 320;
int screen_height = 240;
bool clear_background = true;

// Generated Korean font - see script/generate_u8g2_korean_font.py. Same
// u8g2 binary format as the Rev.8 RLCD build, from D2Coding 1.3.2 (Naver,
// SIL OFL - github.com/naver/d2codingfont), a vector coding font that
// rasterizes cleanly at 20px: dual-width 10/20px Latin/Hangul.
// Generated with --line-height 25 to match lineHeight below.
extern const uint8_t u8g2_font_d2coding_20_t_korean[];

// Selectable word processor fonts. lineHeight is the u8g2 paragraph
// advance (what print("\n") moves by, = ascent_para - descent_para), not
// the glyph height. Declared glyph widths are corrected at setup by
// measuring the actual font. The Korean font renders Hangul full-width,
// hence the double-width charColumns rule.
static EditorFont WP_FONTS[] = {
    {"profont22", u8g2_font_profont22_mf, 12, 18, nullptr},
    {"profont29", u8g2_font_profont29_mf, 16, 25, nullptr},
    {"korean20", u8g2_font_d2coding_20_t_korean, 10, 25, editorfont_hangul_columns},
};
static const int WP_FONT_COUNT = sizeof(WP_FONTS) / sizeof(WP_FONTS[0]);

// active font, selected in WP_setup from config "font"
static EditorFont *wp_font = &WP_FONTS[0];

// layout derived from the active font metrics in WP_setup
static int editY = 206;   // baseline of the edit line
static int cursorY = 210; // top of the cursor band under the edit line
const int cursorHeight = 2;
const int cursorGap = 4; // space between edit line baseline and cursor band

//
void WP_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // setup default color
    JsonDocument &app = status();
    if (!app["config"]["foreground_color"].is<int>())
    {
        app["config"]["foreground_color"] = TFT_WHITE;
    }

    // pick the font from config ("font" is an index into WP_FONTS)
    int fontIndex = app["config"]["font"].as<int>();
    if (fontIndex < 0 || fontIndex >= WP_FONT_COUNT)
        fontIndex = 0;
    wp_font = &WP_FONTS[fontIndex];

    // measure the real glyph advance instead of trusting the declared value.
    // Measured as a difference: getUTF8Width() substitutes the LAST glyph's
    // tight bitmap width for its advance, so a single "M" under-reports on
    // fonts with trimmed glyph boxes - the difference cancels that and
    // leaves exactly one true advance.
    pu8f->setFont((const uint8_t *)wp_font->fontData);
    int16_t measured = pu8f->getUTF8Width("MM") - pu8f->getUTF8Width("M");
    if (measured > 0)
        wp_font->glyphWidth = measured;

    // derive the layout from the font metrics:
    // history lines start at 1.5 line heights (see WP_render_text) and the
    // edit line sits exactly one line height below the last history line,
    // as low as the cursor band above the status bar allows
    int textTop = wp_font->lineHeight * 3 / 2;
    int maxEditY = STATUSBAR_Y - 4 - cursorHeight - cursorGap;
    int rows = (maxEditY - textTop) / wp_font->lineHeight;
    editY = textTop + rows * wp_font->lineHeight;
    cursorY = editY + cursorGap;
    int cols = screen_width / wp_font->glyphWidth;

    //
    _log("WP layout font: %s %dx%d cols: %d rows: %d editY: %d\n",
         wp_font->id, wp_font->glyphWidth, wp_font->lineHeight, cols, rows, editY);

    // editor instantiate
    Editor::getInstance().init(cols, rows, wp_font);

    // load file from the editor
    int file_index = app["config"]["file_index"].as<int>();
    Editor::getInstance().loadFile(format("/%d.txt", file_index));

    // start from clear background
    clear_background = true;

    // sleep timer reset
    editor_activity();
}

//
void WP_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // Take this round's copy of the editor. Everything drawn below reads
    // Editor::view(), so the text can't change half way through a redraw
    // while keys keep arriving on the other core.
    Editor::getInstance().snapshot();

    // the editor swapped to a different window of the file - force a full redraw
    if (Editor::view().pageChanged)
    {
        Editor::view().pageChanged = false;
        clear_background = true;
    }

    // timers
    editor_autosave(3000);
    editor_sleep_check(10 * 60 * 1000);

    // CLEAR BACKGROUND
    WP_render_clear(ptft, pu8f);

    // RENDER TEXT
    WP_render_text(ptft, pu8f);

    // RENDER STATUS BAR
    WP_render_status(ptft, pu8f);

    // BLINK CURSOR
    WP_render_cursor(ptft, pu8f);

    //
    if (clear_background)
        clear_background = false;

    // Editor House Keeping Task
    Editor::getInstance().loop();
}

//
// Clear Screen
// Do it as less as possible so that there is the least amount of the flicker
//
void WP_render_clear(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    // clear background
    if (clear_background)
    {
        // when clearing background
        // sprite will be activated to reduce the flicker
        return;
    }

    //
    JsonDocument &app = status();

    // the composing Hangul character was replaced in place - the new glyph
    // doesn't necessarily cover the old one's pixels, so repaint fully
    if (Editor::view().charReplaced)
    {
        Editor::view().charReplaced = false;
        clear_background = true;
    }

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    //
    static int cursorLine_prev = 0;
    static int cursorPos_prev = 0;
    int cursorLine = Editor::view().cursorLine;
    int cursorPos = Editor::view().cursorPos;
    int cursorLinePos = Editor::view().cursorLinePos;
    int cursorLineLength = Editor::view().lineLengths[cursorLine];

    //
    static int bufferSize_prev = 0;
    int bufferSize = Editor::view().getBufferSize();

    // When new line clear everything
    if (cursorLine_prev != cursorLine)
    {
        //
        clear_background = true;

        //
        cursorLine_prev = cursorLine;
    }

    // When Backspace, trailing characters should be deleted
    // if it is backspace or del key
    if (cursorPos_prev >= cursorPos && bufferSize_prev != bufferSize)
        clear_background = true;

    if (cursorPos_prev != cursorPos)
    {
        // if it is typing at the end don't flicker
        if (cursorPos != Editor::view().getBufferSize())
        {
            // if the edit line is empty then don't flicker
            //
            if (cursorLinePos + 1 != cursorLineLength)
                clear_background = true;
        }

        //
        cursorPos_prev = cursorPos;
    }

    if (bufferSize != bufferSize_prev)
    {
        bufferSize_prev = bufferSize;
    }
}

//
void WP_render_line(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f, int line_num)
{
    char *line = Editor::view().linePositions[line_num];
    int length = Editor::view().lineLengths[line_num];

    // render - the buffer holds UTF-8 and the u8g2 print pipeline decodes
    // multi-byte characters itself, so bytes stream straight through
    for (int i = 0; i < length; i++)
    {
        char value = *(line + i);
        if (value == '\n')
            continue;

        pu8f->print(value);
    }
}

//
void WP_render_text(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // SET FONT
    pu8f->setFont((const uint8_t *)wp_font->fontData);
    pu8f->setForegroundColor(foreground_color);
    pu8f->setFontMode(1);

    // Cursor Information
    static int cursorLine_prev = 0;
    static int cursorLinePos_prev = 0;
    int cursorLine = Editor::view().cursorLine;
    int cursorLinePos = Editor::view().cursorLinePos;
    int totalLine = Editor::view().totalLine;

    //
    // Middle part of the text will be rendered
    // Only when refresh background is called
    //
    // initiate sprite
    TFT_eSprite sprite = display_ILI9341_sprite();
    if (clear_background)
    {
        sprite.createSprite(screen_width, screen_height);
        sprite.fillSprite(background_color);

        //
        pu8f->begin(sprite);
        pu8f->setFont((const uint8_t *)wp_font->fontData);
        pu8f->setForegroundColor(foreground_color);
        pu8f->setFontMode(1);

        //
        pu8f->setCursor(0, wp_font->lineHeight + wp_font->lineHeight / 2);

        // start line
        int rows = Editor::view().rows;

        //
        for (int i = cursorLine - rows; i < cursorLine; i++)
        {
            if (i >= 0)
                WP_render_line(ptft, pu8f, i);

            //
            pu8f->print("\n");
        }
    }

    //
    // Bottom line will the the edit area
    //
    pu8f->setCursor(0, editY);
    WP_render_line(ptft, pu8f, cursorLine);

    // Clean up sprite
    if (clear_background)
    {
        // push sprite
        sprite.pushSprite(0, 0);
        sprite.deleteSprite();
        pu8f->begin(*ptft);
    }
}

// Status Bar
// - file index
// - current file size
// - keyboard layout
// - saved status
void WP_render_status(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // RENDER STATUS BAR
    ptft->setTextColor(foreground_color, background_color);
    ptft->setTextSize(1);

    // FILE INDEX
    ptft->setCursor(0, STATUSBAR_Y, 2);
    ptft->setTextColor(background_color, foreground_color);
    ptft->printf(" %d ", app["config"]["file_index"].as<int>());

    // WORD COUNT
    ptft->setTextColor(foreground_color, background_color);
    ptft->setCursor(30, STATUSBAR_Y, 2);

    int wordCount = Editor::view().wordCountFile + Editor::view().wordCountBuffer;
    String wordCountFormatted = formatNumber(wordCount);
    ptft->printf("%s words", wordCountFormatted.c_str());

    String layout = app["config"]["keyboard_layout"].as<String>();
    if (layout == "null" || layout.isEmpty())
        layout = "US";
    ptft->setCursor(230, STATUSBAR_Y, 2);
    ptft->print(layout);

#ifdef REV5
    if (app["config"]["ble"]["address"].is<const char *>())
    {
        // CHECK BLE Keyboard Connected
        ptft->setCursor(200, STATUSBAR_Y + 6, 1);
        if (appState().bleConnected.load())
        {
            ptft->setTextColor(TFT_GREEN, background_color);
            ptft->print("BLE");
        }
        else
        {
            ptft->setTextColor(TFT_RED, background_color);
            ptft->print("BLE");
        }
    }
#endif

    // SAVE STATUS
    if (Editor::view().saved)
    {
        ptft->fillCircle(310, STATUSBAR_Y + 8, 5, TFT_GREEN);
    }
    else
    {
        ptft->fillCircle(310, STATUSBAR_Y + 8, 5, TFT_RED);
    }
    ptft->drawCircle(310, STATUSBAR_Y + 8, 5, TFT_BLACK);
}

//
// Render Cursor
void WP_render_cursor(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    JsonDocument &app = status();

    // retrieve color information
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // Cursor information
    static int cursorLineCols_prev = 0;
    int cursorLinePos = Editor::view().cursorLinePos;
    int cursorLineCols = Editor::view().cursorLineCols;
    int cursorPos = Editor::view().cursorPos;

    // Calculate Cursor X position
    // reached the line where cursor is
    // cursor offset is measured in display columns so it stays correct
    // once double-width glyphs exist
    int cursorX = 0;
    if (Editor::view().buffer[cursorPos - 1] != '\n' && cursorLinePos != 0)
    {
        cursorX = cursorLineCols * wp_font->glyphWidth;
    }

    // Blink the cursor every 500 ms
    static bool blink = false;
    static unsigned int last = millis();
    if (millis() - last > 500)
    {
        last = millis();
        blink = !blink;
    }

    // Delete previous cursor line
    if (cursorLineCols != cursorLineCols_prev)
    {
        //
        ptft->fillRect(cursorLineCols_prev * wp_font->glyphWidth, cursorY, wp_font->glyphWidth, cursorHeight, background_color);

        //
        cursorLineCols_prev = cursorLineCols;
    }

    // Cursor Blink will be always at the bottom of the screen
    if (blink)
        ptft->fillRect(cursorX, cursorY, wp_font->glyphWidth, cursorHeight, foreground_color);
    else
        ptft->fillRect(cursorX, cursorY, wp_font->glyphWidth, cursorHeight, background_color);
}

//
void WP_keyboard(int key, bool pressed, int index)
{
    // Every key stroke resets sleep timer
    editor_activity();

    //
    JsonDocument &app = status();

    // Check if menu key is pressed
    if (key == MENU || key == 27) // or ESC key
    {
        if (!pressed)
        {
            // Save before transitioning to the menu
            Editor::getInstance().saveFile();

            //
            appState().screen = MENUSCREEN;

            //
            _debug("WP_keyboard::Moving to Menu Screen\n");
        }
    }

    // Check if File Change request is pressed
    else if (key >= 1000 && key <= 1010)
    {
        if (!pressed)
        {
            int fileIndex = key - 1000;
            _log("File Change Requested: %d\n", fileIndex);

            //
            Editor::getInstance().saveFile();

            // save config
            app["config"]["file_index"] = fileIndex;
            config_save();

            // load new file
            Editor::getInstance().loadFile(format("/%d.txt", fileIndex));
        }
    }

    else
    {
        // send the keys to the editor
        Editor::getInstance().keyboard(key, pressed);
    }
}
