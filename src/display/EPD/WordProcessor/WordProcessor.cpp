#include "WordProcessor.h"
#include "../display_EPD.h"

//
#include "app/app.h"
#include "service/Editor/Editor.h"
#include "service/Editor/EditorTimers.h"
#include "keyboard/keyboard.h"
#include "display/display.h"

// Generated Korean font - see script/generate_gfxfont_korean_font.py.
// GFXfont/4-bit-grayscale format (this backend's own, different from the
// u8g2 fonts used by RLCD/ILI9341/ST7735). Source is the same
// GalmuriMono11 (SIL OFL) used for Rev.8, rasterized larger (36px) for
// this panel's bigger 960x540 canvas; only included here, so its ~1.3MB
// of glyph data isn't duplicated into every other EPD screen's translation
// unit (unlike monospace.h/system.h, which are small enough not to matter).
#include "../fonts/galmuri_36_korean.h"

//
//
#define MARGIN_X 20
#define MARGIN_Y 40

// Selectable word processor fonts. lineHeight is the hand-picked leading
// (larger than the font's own advance_y for readability on the big panel).
// Declared glyph widths are corrected at setup by reading the glyph table.
// The Korean font renders Hangul full-width, hence the double-width
// charColumns rule.
static EditorFont WP_FONTS[] = {
    {"monospace", &monospace, 20, 60, nullptr},
    {"korean36", &galmuri36Korean, 18, 40, editorfont_hangul_columns},
};
static const int WP_FONT_COUNT = sizeof(WP_FONTS) / sizeof(WP_FONTS[0]);

// active font, selected in WP_setup from config "font"
static EditorFont *wp_font = &WP_FONTS[0];

// status bar
const int status_height = 30;
const int statusY = EPD_HEIGHT - status_height - 5;

// measure the real glyph advance from the GFXfont glyph table
static int16_t WP_glyph_advance(const GFXfont *font, uint32_t codepoint)
{
    for (uint32_t i = 0; i < font->interval_count; i++)
    {
        const UnicodeInterval *interval = &font->intervals[i];
        if (codepoint >= interval->first && codepoint <= interval->last)
            return font->glyph[interval->offset + (codepoint - interval->first)].advance_x;
    }
    return 0;
}

// clear screen
bool clear_full = false;
bool clear_request = true;
bool cleared = true;

// How hard the flashing clear (clear_full) works. One cycle is 4 black and
// 4 white passes over the whole panel, about 0.7 seconds.
//   F5: the writer asked for a clean screen, so the thorough clear.
//   Entering the word processor (boot, back from menu or sleep): a lighter
//   one. Raise CLEAR_CYCLES_ENTER to 2 if ghosts of the menu stay visible.
#define CLEAR_CYCLES_REFRESH 2
#define CLEAR_CYCLES_ENTER 1
static int clear_full_cycles = CLEAR_CYCLES_REFRESH;

//
int startLine = -1;

// Cursor state. Kept here so anything that wipes the screen can tell the
// cursor code its mark is gone - otherwise it would spend a clear on erasing
// a cursor that is no longer there, and not draw one until the next move.
static bool cursorVisible = false;
static int renderedCursorX = -1;
static void WP_cursor_wiped()
{
    cursorVisible = false;
    renderedCursorX = -1;
}

// Status bar state, shared between the partial updates and the full draw
static bool status_saved_prev = false;
static size_t status_filesize_prev = 0;
static bool status_debouncing = false;
static int status_last = 0;
static void WP_status_to_framebuffer();

// What each text row of the panel currently shows, as a fingerprint of the
// line drawn there. Typing in the middle of the text can re-wrap the lines
// below the cursor; those are only brought up to date when the cursor leaves
// the line, and comparing against this tells whether any row is really out
// of date - most edits don't move anything, and then no refresh is needed.
struct RowShown
{
    uint32_t hash;
    int length;
};
#define WP_MAX_ROWS 32
static RowShown rows_shown[WP_MAX_ROWS];

// fingerprint of the line that belongs on a visible row right now
static RowShown WP_row_now(int row)
{
    Editor &editor = Editor::view();
    RowShown now = {2166136261u, 0};

    // a row past the end of the text is blank, the same as an empty line
    int line = startLine + row;
    if (startLine < 0 || line < 0 || line > editor.totalLine || editor.linePositions[line] == NULL)
        return now;

    const char *text = editor.linePositions[line];
    now.length = editor.lineLengths[line];
    for (int i = 0; i < now.length; i++)
        now.hash = (now.hash ^ (uint8_t)text[i]) * 16777619u; // FNV-1a

    return now;
}

// the row was just redrawn and matches the text again
static void WP_row_mark_shown(int row)
{
    if (row >= 0 && row < WP_MAX_ROWS)
        rows_shown[row] = WP_row_now(row);
}

// the whole text area matches the text
static void WP_rows_mark_shown()
{
    for (int row = 0; row < WP_MAX_ROWS; row++)
        rows_shown[row] = WP_row_now(row);
}

// first row whose content on the panel differs from the text, -1 if none
static int WP_rows_first_stale()
{
    int rows = Editor::view().rows;
    for (int row = 0; row <= rows && row < WP_MAX_ROWS; row++)
    {
        RowShown now = WP_row_now(row);
        if (now.hash != rows_shown[row].hash || now.length != rows_shown[row].length)
            return row;
    }
    return -1;
}

// The composing Hangul character is tracked separately from the cursor.
// A composition can finish and immediately start again on the next jamo,
// so composer.isComposing() alone is not enough to detect a commit: a move
// of the composing character to another buffer position is also a commit.
static bool composing_prev = false;
static int composing_line_prev = -1;
static int composing_char_start_prev = -1;

//
void WP_setup()
{
    _debug("==========================================\n");
    _log("Word Processor GUI Setup\n");

    //
    JsonDocument &app = status();

    // pick the font from config ("font" is an index into WP_FONTS)
    int fontIndex = app["config"]["font"].as<int>();
    if (fontIndex < 0 || fontIndex >= WP_FONT_COUNT)
        fontIndex = 0;
    wp_font = &WP_FONTS[fontIndex];

    // measure the real glyph advance instead of trusting the declared value
    int16_t measured = WP_glyph_advance((const GFXfont *)wp_font->fontData, 'M');
    if (measured > 0)
        wp_font->glyphWidth = measured;

    // derive the layout from the font metrics:
    // text runs from the left margin to the panel edge, and from the top
    // margin down to the status bar
    int cols = (EPD_WIDTH - MARGIN_X) / wp_font->glyphWidth;
    int rows = (statusY - MARGIN_Y) / wp_font->lineHeight;

    //
    _log("WP layout font: %s %dx%d cols: %d rows: %d\n",
         wp_font->id, wp_font->glyphWidth, wp_font->lineHeight, cols, rows);

    // editor instantiate
    // Editor Init - setup screen size
    Editor::getInstance().init(cols, rows, wp_font);

    // display init
    display_initialize(MARGIN_X, MARGIN_Y, wp_font->lineHeight, wp_font->glyphWidth);

    // load file from the editor
    int file_index = app["config"]["file_index"].as<int>();

    //
    Editor::getInstance().loadFile(format("/%d.txt", file_index));

    //
    _log("Word Processor Initialized %d.txt\n", file_index);

    // clear background
    clear_full = true;
    clear_full_cycles = CLEAR_CYCLES_ENTER;

    // autosave and sleep timers start over
    editor_timers_reset();

    // reset startLine
    startLine = -1;

    composing_prev = false;
    composing_line_prev = -1;
    composing_char_start_prev = -1;
}

//
void WP_render()
{
    // Take this round's copy of the editor. Everything drawn below reads
    // Editor::view(), so the text can't change half way through a redraw
    // while keys keep arriving on the other core.
    Editor::getInstance().snapshot();

    // the editor swapped to a different window of the file (paging, or the
    // buffer filling up while typing) - force a full redraw
    if (Editor::view().pageChanged)
    {
        Editor::view().pageChanged = false;

        // the flashing clear below wipes the screen anyway, clearing it
        // twice only costs time (this is the case every time the word
        // processor opens: the file load swaps the window as well)
        if (!clear_full)
        {
            epd_poweron();
            epd_clear_quick(epd_full_screen(), 4, 50);
            epd_poweroff_all();
        }

        startLine = -1;
        clear_request = true; // status bar should be refreshed too
    }

    // perform full screen clear routine
    if (clear_full)
    {
        //
        epd_poweron();
        epd_clear_area_cycles(epd_full_screen(), clear_full_cycles, 50);
        epd_poweroff_all();

        //
        clear_full = false;
        clear_full_cycles = CLEAR_CYCLES_REFRESH;
        clear_request = true;
    }

    // Clear Background
    if (clear_request)
    {
        //
        clear_request = false;

        // mark that the screen is cleared
        // so that following render functions will redraw
        cleared = true;

        //
        _debug("WP_render::full refresh requested\n");
    }

    //
    editor_autosave(4000);
    editor_sleep_check(10 * 60 * 1000);

    // Bottom Status
    WP_render_status();

    // BLINK CURSOR
    WP_render_cursor();

    // RENDER TEXT
    WP_render_text();

    // clear background flag off
    cleared = false;

    // Editor House Keeping Tasks
    Editor::getInstance().loop();
}

// DRAW LINE OF TEXT
void WP_render_text_line(int i, int cursorY, uint8_t *framebuffer)
{
    char *line = Editor::view().linePositions[i];
    int length = Editor::view().lineLengths[i];

    //
    display_setline(i - startLine);

    // render
    if (line != NULL && length > 0)
    {
        // the buffer holds UTF-8 and writeln() decodes it directly
        char row[256];

        // Copy the line content to row, but not more than 255 bytes
        int copyLength = (length < 255) ? length : 255;
        strncpy(row, line, copyLength);

        // Null-terminate the string
        row[copyLength] = '\0';

        int cursorX = display_x();
        int cursorY = display_y();

        //
        if (framebuffer == NULL)
            epd_poweron();
        //
        writeln((GFXfont *)wp_font->fontData, row, &cursorX, &cursorY, framebuffer);

        //
        if (framebuffer == NULL)
            epd_poweroff_all();
    }
    else
    {
        _debug("WP_render_text_line::line null or empty line position %d\n", i);
    }
}

void WP_clear_row(int row)
{
    epd_poweron();

    // delete a line and redraw the line
    Rect_t area =
        display_rect(
            0,
            display_lineheight() * row,
            EPD_WIDTH,
            display_lineheight());

    epd_clear_quick(area, 4, 50);

    epd_poweroff_all();
}

// Clear and redraw one visible editor line. This is used when a Hangul
// composition is committed so any ghosting accumulated by the composing
// cell is cleaned up at the line level.
static void WP_refresh_text_line(int line)
{
    int row = line - startLine;
    if (row < 0 || row > Editor::view().rows)
        return;

    WP_clear_row(row);
    display_setline(row);
    WP_render_text_line(line, display_y(), NULL);
    WP_row_mark_shown(row);
}

// Clear and redraw the text area from one row down to the status bar, in a
// single pass. Used instead of a whole screen refresh when only the lines
// from some row on are out of date: the rows above and the status bar are
// left alone.
static void WP_refresh_from_row(int row)
{
    Editor &editor = Editor::view();
    if (row < 0)
        row = 0;

    int top = display_lineheight() * row;
    if (top >= statusY)
        return;

    epd_poweron();
    epd_clear_quick(display_rect(0, top, EPD_WIDTH, statusY - top), 4, 50);
    epd_poweroff_all();

    // The frame buffer is white apart from these lines, and white rows are
    // skipped by the panel driver, so pushing the whole buffer only spends
    // time on the rows that were just cleared.
    for (int r = row; r <= editor.rows; r++)
    {
        int line = startLine + r;
        if (line > editor.totalLine)
            break;

        WP_render_text_line(line, display_y(), display_EPD_framebuffer());
    }
    display_draw_buffer();

    for (int r = row; r < WP_MAX_ROWS; r++)
        WP_row_mark_shown(r);
}

// Refresh exactly the display cell occupied by the active composing Hangul
// character. Hangul and compatibility jamo use two editor columns, so the
// rectangle follows charColumns() rather than assuming a fixed byte/width.
static void WP_refresh_composing_cell(int line, int charStart)
{
    Editor &editor = Editor::view();
    int row = line - startLine;
    if (row < 0 || row > editor.rows || line < 0 || line > editor.totalLine)
        return;

    char *linePtr = editor.linePositions[line];
    if (linePtr == NULL)
        return;

    int lineStart = (int)(linePtr - editor.buffer);
    int lineEnd = lineStart + editor.lineLengths[line];
    if (charStart < lineStart || charStart >= lineEnd)
        return;

    int prefixCols = 0;
    for (int pos = lineStart; pos < charStart;)
    {
        int charLen = 0;
        uint32_t codepoint = utf8_decode(editor.buffer + pos, &charLen);
        if (charLen <= 0)
            return;
        prefixCols += editor.charColumns(codepoint);
        pos += charLen;
    }

    int charLen = 0;
    uint32_t codepoint = utf8_decode(editor.buffer + charStart, &charLen);
    if (charLen <= 0 || charStart + charLen > lineEnd || charLen > 4)
        return;

    int x = MARGIN_X + display_fontwidth() * prefixCols;
    Rect_t area = display_rect(
        x,
        display_lineheight() * row,
        display_fontwidth() * editor.charColumns(codepoint),
        display_lineheight());

    char glyph[5];
    memcpy(glyph, editor.buffer + charStart, charLen);
    glyph[charLen] = '\0';

    display_setline(row);
    int cursorX = x;
    int cursorY = display_y();

    epd_poweron();
    epd_clear_quick(area, 4, 50);
    writeln((GFXfont *)wp_font->fontData, glyph, &cursorX, &cursorY, NULL);
    epd_poweroff_all();
}

//
bool editing = false;
void WP_render_text()
{
    //
    JsonDocument &app = status();

    // Cursor Information
    static int cursorPos_prev = 0;
    static int cursorLine_prev = 0;
    static int cursorLinePos_prev = 0;
    static int bufferSize_prev = 0;
    int cursorPos = Editor::view().cursorPos;
    int cursorLine = Editor::view().cursorLine;
    int cursorLinePos = Editor::view().cursorLinePos;
    int bufferSize = Editor::view().getBufferSize();

    Editor &editor = Editor::view();
    bool composing = editor.composer.isComposing();
    int composingCharStart = composing ? editor.prevCharStart(cursorPos) : -1;
    bool compositionCommitted =
        composing_prev &&
        (!composing || cursorLine != composing_line_prev ||
         composingCharStart != composing_char_start_prev);

    //
    int totalLine = Editor::view().totalLine;
    int rows = Editor::view().rows;

    // when first turned on
    // start editing from last 2nd Line
    if (startLine == -1)
    {
        //
        startLine = max(cursorLine - rows + 1, 0);
    }

    //
    // when reaching the end of the screen reset the startLine
    if (cursorLine - startLine > rows)
    {
        // clear background
        epd_poweron();
        epd_clear_quick(epd_full_screen(), 4, 50);
        epd_poweroff_all();

        // Everything is redrawn in this same round. The status bar was
        // wiped with the rest of the screen and WP_render_status() already
        // ran, so it is added to the frame buffer here. (Requesting another
        // clear round for it, as before, drew the whole page a second time.)
        cleared = true;
        WP_status_to_framebuffer();
        WP_cursor_wiped();

        //
        startLine = max(cursorLine - 1, 0);
        _debug("WP_render_text::New Page cursorLine %d startLine %d rows %d totalLine %d\n",
               cursorLine,
               startLine,
               rows,
               totalLine);
    }

    // when cursor reaches the top and it's time to show the previous page
    if (cursorLine < startLine)
    {
        //
        _debug("WP_render_text::need to show previous page cursorLine %d prev %d startLine %d totalLine %d\n",
               cursorLine,
               cursorLine_prev,
               startLine,
               totalLine);

        // startLine should go one page before
        // keep the last line
        startLine = max(startLine - rows - 1, 0);

        _debug("WP_render_text::startLine updated %d \n",
               startLine);

        // clear background
        epd_poweron();
        epd_clear_quick(epd_full_screen(), 4, 50);
        epd_poweroff_all();

        // the status bar was wiped too, redraw it with the text
        cleared = true;
        WP_status_to_framebuffer();
        WP_cursor_wiped();
    }

    //
    // Middle part of the text will be rendered
    // Only when refresh background is called
    //
    if (cleared)
    {
        // Draw from the first line
        display_setline(0);

        //
        for (int i = startLine; i <= totalLine; i++)
        {
            // stop when exceeding row
            if (i - startLine > rows)
            {
                _debug("WP_render_text::reached the end of the text startLine %d i %d row %d\n", startLine, i, rows);
                break;
            }

            if (i >= 0)
                WP_render_text_line(i, display_y(), display_EPD_framebuffer());

            // increase the line
            display_newline();
        }

        //
        // render frame to the display
        display_draw_buffer();

        // every row is up to date again
        editing = false;
        WP_rows_mark_shown();
    }

    // The active composition ended, or feed() committed it and immediately
    // began the next Hangul cell. Clean up the completed line as one unit.
    else if (compositionCommitted)
    {
        editor.charReplaced = false;
        editor.backSpacePressed = false;

        WP_refresh_text_line(composing_line_prev);

        // Enter/wrapping can commit on the previous line and insert on a
        // new one in the same editor update. Make that new text visible too.
        if (cursorLine != composing_line_prev && bufferSize != bufferSize_prev)
            WP_refresh_text_line(cursorLine);
    }

    // While the same Hangul character is being assembled (ㄱ -> 가 -> 간),
    // restrict the EPD partial update to that character's display cell.
    else if (composing &&
             (editor.charReplaced || editor.backSpacePressed ||
              bufferSize != bufferSize_prev || cursorPos != cursorPos_prev))
    {
        editor.charReplaced = false;
        editor.backSpacePressed = false;
        WP_refresh_composing_cell(cursorLine, composingCharStart);
        WP_row_mark_shown(cursorLine - startLine);

        // composing in the middle of the text can re-wrap the lines below
        if (cursorPos != bufferSize)
            editing = true;
    }

    // handle backspace (whole-character deletion), and the rare composing
    // replace that crossed a line boundary - the row is cleared and
    // redrawn whole
    else if (Editor::view().backSpacePressed || Editor::view().charReplaced)
    {
        //
        Editor::view().backSpacePressed = false;
        Editor::view().charReplaced = false;

        // clear the currentLine and the previousLine
        _debug("WP_render_text::Handle Backspace\n");

        // delete a line and redraw the line
        WP_clear_row(max(cursorLine - startLine, 0));

        // if line is change then also clear the previous line
        if (cursorLine != cursorLine_prev)
        {
            WP_clear_row(max(cursorLine_prev - startLine, 0));
        }

        // and redraw the line
        WP_render_text_line(cursorLine, display_y(), NULL);
        WP_row_mark_shown(cursorLine - startLine);

        // Deleting in the middle of the text can re-wrap the lines below,
        // and joining two lines moves everything under them up. Those rows
        // are checked and brought up to date when the cursor changes line.
        if (cursorPos != bufferSize)
            editing = true;
    }

    // Partial Refresh Logic
    else if (cleared == false && bufferSize != bufferSize_prev)
    {
        // new line
        // when new line is detected than redraw the previous line
        // in case when there is a word wrap then what was written at the previous line is moved to the current line
        if (cursorLine > cursorLine_prev)
        {
            // clear the currentLine and the previousLine
            _debug("WP_render_text::new line is detected. clear the previous line to complete word wrap.\n");

            WP_clear_row(max(cursorLine_prev - startLine, 0));

            // and redraw the line
            WP_render_text_line(cursorLine_prev, display_y(), NULL);
            WP_row_mark_shown(cursorLine_prev - startLine);

            // render the entire line
            cursorLinePos_prev = 0;
        }

        ////////////////////////////////////////////////
        // if currently writing in the middle for editing then erase the current line
        // and subsequent lines until the end of the screen
        if (cursorPos != bufferSize)
        {
            // mark editing flag
            editing = true;

            //
            int currentLine = max(0, cursorLine - startLine);

            _debug("WP_render_text::editing currentLine %d cursorLine %d cursorLineprev %d startLine %d\n", currentLine, cursorLine, cursorLine_prev, startLine);

            // Draw from the currentLine
            display_setline(currentLine);

            // delete a line and redraw the line
            WP_clear_row(currentLine);

            // and redraw the line
            WP_render_text_line(cursorLine, display_y(), NULL);
            WP_row_mark_shown(currentLine);
        }

        ////////////////////////////////////////////////

        //
        // Draw the new character entered
        else if (cursorPos != cursorPos_prev)
        {
            char *line = Editor::view().linePositions[cursorLine];
            int line_length = Editor::view().lineLengths[cursorLine];

            // pixel offset of the byte at cursorLinePos_prev: count the
            // display columns of the UTF-8 characters before it, since a
            // byte offset alone no longer maps 1:1 to a screen position
            int prefixCols = 0;
            for (int j = 0; j < cursorLinePos_prev && j < line_length;)
            {
                int charLen;
                uint32_t codepoint = utf8_decode(line + j, &charLen);
                prefixCols += Editor::view().charColumns(codepoint);
                j += charLen;
            }

            // render entire line
            int cursorX = MARGIN_X + display_fontwidth() * prefixCols;
            display_setline(cursorLine - startLine);
            int cursorY = display_y();
            _debug("cursorY: %d cursorLine: %d\n", cursorY, cursorLine);

            _debug("cursorLine: %d line: %d line_length: %d cursorLinePos_prev: %d\n", cursorLine, line, line_length, cursorLinePos_prev);

            // new character has been typed
            if (cursorLinePos_prev < line_length)
            {
                // Copy the line tail from the previous cursor position,
                // but not more than 255 bytes - it is already UTF-8
                char row[256];
                int tailLength = line_length - cursorLinePos_prev;
                int copyLength = (tailLength < 255) ? tailLength : 255;
                strncpy(row, line + cursorLinePos_prev, copyLength);
                row[copyLength] = '\0';

                //
                epd_poweron();
                writeln((GFXfont *)wp_font->fontData, row, &cursorX, &cursorY, NULL);
                epd_poweroff_all();
            }
        }
    }

    // reset prev flags
    cursorPos_prev = cursorPos;
    cursorLinePos_prev = cursorLinePos;
    bufferSize_prev = bufferSize;

    composing_prev = composing;
    composing_line_prev = composing ? cursorLine : -1;
    composing_char_start_prev = composing ? composingCharStart : -1;

    // line changed
    if (cursorLine_prev != cursorLine)
    {
        if (editing == true)
        {
            // The cursor left a line that was edited in the middle of the
            // text. That used to clear and redraw the whole screen every
            // time. Now the rows are compared with what they show: when the
            // edit didn't move any other line nothing is redrawn at all,
            // and otherwise only the rows from the first changed one down.
            int stale = WP_rows_first_stale();
            if (stale >= 0)
                WP_refresh_from_row(stale);

            editing = false;
            _debug("WP_render_text::editing line change. %d %d %d %d stale row %d\n", cursorLine, cursorLine_prev, cursorPos, cursorPos_prev, stale);
        }

        // reset prev flag
        cursorLine_prev = cursorLine;
    }

    // Outside of an edit in the middle of the text the rows are kept up to
    // date as they change, so what is shown is the text as it is now.
    if (!editing)
        WP_rows_mark_shown();
}

//
// Render Cursor
#define CURSOR_MARGIN 10
#define CURSOR_THICKNESS 5
#define CURSOR_DELAY 50

// fill a small buffer with solid black (nibble 0) and push only the
// cursor's tiny area through the proven image waveform, instead of
// a full-screen redraw or the unreliable raw pixel-push darken.
static void WP_draw_cursor(Rect_t area)
{
    int32_t cursor_buf_width = area.width / 2 + area.width % 2;
    uint8_t *cursor_buf = (uint8_t *)malloc(cursor_buf_width * area.height);
    if (cursor_buf)
    {
        memset(cursor_buf, 0x00, cursor_buf_width * area.height);

        epd_poweron();
        epd_draw_image(area, cursor_buf, BLACK_ON_WHITE);
        epd_poweroff_all();

        free(cursor_buf);
    }
}

// erase with a small padding margin so anti-aliased edges don't linger
// when the cursor is redrawn repeatedly at the same spot.
static void WP_erase_cursor(Rect_t area)
{
    Rect_t erase_area = display_rect(
        area.x - 10,
        area.y - 5,
        area.width + 20,
        area.height + 10);

    epd_poweron();
    epd_clear_quick(erase_area, 8, 50);
    epd_poweroff_all();
}

void WP_render_cursor()
{
    JsonDocument &app = status();

    // don't render at the round when screen is cleared
    // the cursor mark went with the rest of the screen, it is drawn again
    // in the next round
    if (cleared)
    {
        WP_cursor_wiped();
        return;
    }

    // Cursor information
    // (renderedCursorX and cursorVisible live at the top of the file)
    static int last = 0;

    static int cursorLineCols_prev = 0;
    static int cursorPos_prev = 0;
    int cursorLinePos = Editor::view().cursorLinePos;
    int cursorLineCols = Editor::view().cursorLineCols;
    int cursorLine = Editor::view().cursorLine;
    int cursorPos = Editor::view().cursorPos;

    static Rect_t area;

    //
    //

    // Calculate Cursor X position
    // reached the line where cursor is
    // cursor offset is measured in display columns so it stays correct
    // once double-width glyphs exist
    int cursorX = MARGIN_X;
    if (Editor::view().buffer[max(cursorPos - 1, 0)] != '\n' && cursorLinePos != 0)
    {
        // where to display the cursor
        cursorX = MARGIN_X + cursorLineCols * display_fontwidth() + 5;

        //
        // _debug("WP_render_cursor::cursorX %d\n", cursorX);
    }

    // cursor moved: erase if currently visible and restart the idle timer
    if (cursorPos != cursorPos_prev)
    {
        if (cursorVisible)
        {
            WP_erase_cursor(area);
            cursorVisible = false;
        }

        // render the cursor
        renderedCursorX = -1;

        // reset the timer
        last = millis();

        //
        cursorPos_prev = cursorPos;
        cursorLineCols_prev = cursorLineCols;
    }

    // when there are no types for a short duration then
    // display the cursor
    if (renderedCursorX == -1 && last + CURSOR_DELAY < millis())
    {
        // Cursor will be always at the bottom of the screen
        area = display_rect(
            MARGIN_X + cursorLineCols * display_fontwidth(),
            MARGIN_Y + CURSOR_MARGIN + display_lineheight() * (max(cursorLine - startLine, 0)),
            display_fontwidth() * abs(cursorLineCols - cursorLineCols_prev + 1),
            CURSOR_THICKNESS);

        WP_draw_cursor(area);
        cursorVisible = true;

        //
        renderedCursorX = cursorX;
    }

    // NOTE: no idle blinking. Each draw_image call costs ~275ms on this
    // panel (15 waveform frames x full-height row scan, even for a tiny
    // area), so a blinking cursor kept the display task busy roughly a
    // third of the time and made everything else feel sluggish. The cursor
    // appears after a short pause and stays solid until it moves.
}

// display file number
// display character total
// display keyboard layout - Rev.5 and Rev.7
// display save status
// FILE INDEX | BYTES | SAVED | LAYOUT
// 160 | 200 ---- 200 | 100
#define STATUS_REFRESH 300
// Draw the complete status bar into the frame buffer, so it reaches the
// panel together with the text in one pass. Used whenever the whole screen
// was cleared. Drawing the word count and the saved state this way, instead
// of as two separate partial updates right after, saves two clear + draw
// cycles on every full redraw.
static void WP_status_to_framebuffer()
{
    JsonDocument &app = status();
    Editor &editor = Editor::view();

    // status start Y position (writeln moves the cursor, so one per item)
    const int baseline = statusY + status_height - 8;
    int cursorX;
    int cursorY;

    // FILE INDEX 25 - 225
    cursorX = 25;
    cursorY = baseline;
    String file = format("FILE %d", app["config"]["file_index"].as<int>());
    writeln((GFXfont *)&systemFont, file.c_str(), &cursorX, &cursorY, display_EPD_framebuffer());

    // WORD COUNT 200 - 400
    cursorX = 200;
    cursorY = baseline;
    String wordCountFormatted = formatNumber(editor.wordCountFile + editor.wordCountBuffer);
    writeln((GFXfont *)&systemFont, wordCountFormatted.c_str(), &cursorX, &cursorY, display_EPD_framebuffer());

    // SAVED STATE 550 - 750
    cursorX = 550;
    cursorY = baseline;
    bool saved = editor.saved;
    writeln((GFXfont *)&systemFont, saved ? "SAVED" : "NOT SAVED", &cursorX, &cursorY, display_EPD_framebuffer());

    // KEYBOARD LAYOUT 860 - 960
    String layout = app["config"]["keyboard_layout"].as<String>();
    if (layout == "null" || layout.isEmpty())
        layout = "US"; // defaults to US layout
    cursorX = 860;
    cursorY = baseline;
    writeln((GFXfont *)&systemFont, layout.c_str(), &cursorX, &cursorY, display_EPD_framebuffer());

    // the partial updates below start from what is shown now
    status_filesize_prev = editor.seekPos + editor.getBufferSize();
    status_debouncing = false;
    status_saved_prev = saved;
}

void WP_render_status()
{
    // status start Y position
    int cursorY = statusY + status_height - 8;

    //
    size_t filesize = Editor::view().seekPos + Editor::view().getBufferSize();

    // Full Redraw
    // everything goes into the frame buffer and is shown with the text
    if (cleared)
    {
        WP_status_to_framebuffer();
        return;
    }

    // WORD COUNT DRAWS WHEN STOPPED EDITING FOR A WHILE
    if (filesize != status_filesize_prev)
    {
        // debounce for status_refresh amount
        status_last = millis();

        //
        status_filesize_prev = filesize;
        status_debouncing = true;
    }

    //
    if (status_debouncing == true && status_last + STATUS_REFRESH < (int)millis())
    {
        //
        status_last = millis();
        status_debouncing = false;

        // WORD COUNT 200 - 400
        int cursorX = 200;

        // remove previous text
        Rect_t area = display_rect(
            cursorX - 10,
            statusY,
            210,
            status_height);

        epd_poweron();
        epd_clear_quick(area, 4, 50);

        // redraw the word count
        int wordCount = Editor::view().wordCountFile + Editor::view().wordCountBuffer;
        String wordCountFormatted = formatNumber(wordCount);
        writeln((GFXfont *)&systemFont, wordCountFormatted.c_str(), &cursorX, &cursorY, NULL);
        epd_poweroff_all();

        // writeln moved the cursor
        cursorY = statusY + status_height - 8;
    }

    /////////////////////////////////////
    // DISPLAY SAVED STATE
    /////////////////////////////////////
    if (Editor::view().saved != status_saved_prev)
    {
        int cursorX = 550;

        // clear the status area
        Rect_t area = display_rect(
            cursorX - 10,
            statusY,
            210,
            status_height);

        //
        epd_poweron();
        epd_clear_quick(area, 4, 50);

        //
        String savedText = "NOT SAVED";
        if (Editor::view().saved)
        {
            // file is saved
            savedText = "SAVED";
        }

        // display the text
        writeln((GFXfont *)&systemFont, savedText.c_str(), &cursorX, &cursorY, NULL);
        epd_poweroff_all();

        // previous values
        status_saved_prev = Editor::view().saved;
    }
    /////////////////////////////////////
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
            _log("WP_keyboard::Menu Key Pressed. Saving file %d.txt\n", status()["config"]["file_index"].as<int>());
            Editor::getInstance().saveFile();

            //
            appState().screen = MENUSCREEN;

            //
            _debug("WP_keyboard::Moving to Menu Screen\n");
        }
    }

    // REFRESH SCREEN F5
    else if (key == 5)
    {
        if (!pressed)
        {
            clear_full = true;
            _log("WP_keyboard::F5 Key Pressed. Saving file %d.txt\n", status()["config"]["file_index"].as<int>());
            Editor::getInstance().saveFile();
        }
    }

    // SLEEP BUTTON - PAUSE
    else if (key == 24)
    {
        if (!pressed)
        {
            // go to sleep mode
            appState().screen = SLEEPSCREEN;
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
        // convert tab key to space
        if (key == '\t')
            key = ' ';

        // send the keys to the editor
        Editor::getInstance().keyboard(key, pressed);
    }
}

