#include "BleUI.h"
#include "../display_EPD.h"

#define BLEUI_MARGIN 40
#define BLEUI_HEADER_LINE_Y 104
#define BLEUI_STATUS_Y 190
#define BLEUI_TEXT_Y 260
#define BLEUI_TEXT_SPACING 42
#define BLEUI_FOOTER_LINE_Y 430
#define BLEUI_CHIP_Y 456
#define BLEUI_CHIP_H 46

// 8 bit colors for shapes
#define BLEUI_INK 0x00
#define BLEUI_LINE 0xA0

// 4 bit colors for text
#define BLEUI_TEXT_INK 0
#define BLEUI_TEXT_MUTED 6
#define BLEUI_TEXT_WHITE 15

static void BleUI_write(GFXfont *font, const char *text, int32_t x, int32_t y, uint8_t fg, uint8_t bg)
{
    FontProperties props = {};
    props.fg_color = fg;
    props.bg_color = bg;
    write_mode(font, text, &x, &y, display_EPD_framebuffer(), BLACK_ON_WHITE, &props);
}

static int32_t BleUI_width(GFXfont *font, const char *text)
{
    int32_t x = 0, y = 0, x1, y1, w, h;
    get_text_bounds(font, text, &x, &y, &x1, &y1, &w, &h, NULL);
    return w;
}

//
void BleUI_begin()
{
    epd_poweron();
    epd_clear_quick(epd_full_screen(), 4, 50);
    epd_poweroff();

    memset(display_EPD_framebuffer(), 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}

//
void BleUI_header(const char *title, const char *detail)
{
    BleUI_write(display_EPD_font(), title, BLEUI_MARGIN, 80, BLEUI_TEXT_INK, BLEUI_TEXT_WHITE);

    if (detail && detail[0])
    {
        GFXfont *font = (GFXfont *)&systemFont;
        BleUI_write(font, detail, EPD_WIDTH - BLEUI_MARGIN - BleUI_width(font, detail), 80, BLEUI_TEXT_MUTED, BLEUI_TEXT_WHITE);
    }

    epd_draw_hline(BLEUI_MARGIN, BLEUI_HEADER_LINE_Y, EPD_WIDTH - BLEUI_MARGIN * 2, BLEUI_LINE, display_EPD_framebuffer());
}

//
void BleUI_status(int dot, const char *status)
{
    int cx = BLEUI_MARGIN + 12;
    int cy = BLEUI_STATUS_Y - 14;
    if (dot == BLEUI_DOT_FILLED)
        epd_fill_circle(cx, cy, 12, BLEUI_INK, display_EPD_framebuffer());
    else
        for (int r = 9; r <= 12; r++)
            epd_draw_circle(cx, cy, r, BLEUI_INK, display_EPD_framebuffer());

    BleUI_write(display_EPD_font(), status, BLEUI_MARGIN + 44, BLEUI_STATUS_Y, BLEUI_TEXT_INK, BLEUI_TEXT_WHITE);
}

//
void BleUI_text(int line, const char *text)
{
    if (!text || !text[0])
        return;

    BleUI_write((GFXfont *)&systemFont, text, BLEUI_MARGIN, BLEUI_TEXT_Y + line * BLEUI_TEXT_SPACING, BLEUI_TEXT_MUTED, BLEUI_TEXT_WHITE);
}

// inverted key chip followed by its action, returns the x after the pair
static int BleUI_chip(int x, const char *key, const char *action)
{
    GFXfont *font = (GFXfont *)&systemFont;
    int baseline = BLEUI_CHIP_Y + 32;

    int keyW = BleUI_width(font, key) + 32;
    epd_fill_rect(x, BLEUI_CHIP_Y, keyW, BLEUI_CHIP_H, BLEUI_INK, display_EPD_framebuffer());
    BleUI_write(font, key, x + 16, baseline, BLEUI_TEXT_WHITE, BLEUI_TEXT_INK);

    x += keyW + 16;
    BleUI_write(font, action, x, baseline, BLEUI_TEXT_MUTED, BLEUI_TEXT_WHITE);

    return x + BleUI_width(font, action) + 48;
}

//
void BleUI_footer(const char *key1, const char *action1, const char *key2, const char *action2)
{
    epd_draw_hline(BLEUI_MARGIN, BLEUI_FOOTER_LINE_Y, EPD_WIDTH - BLEUI_MARGIN * 2, BLEUI_LINE, display_EPD_framebuffer());

    int x = BleUI_chip(BLEUI_MARGIN, key1, action1);
    if (key2)
        BleUI_chip(x, key2, action2);
}

//
void BleUI_end()
{
    display_draw_buffer();
}
