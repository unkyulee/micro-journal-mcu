#include "BleUI.h"

#define BLEUI_W 400
#define BLEUI_MARGIN 20
#define BLEUI_HEADER_LINE_Y 52
#define BLEUI_STATUS_Y 106
#define BLEUI_TEXT_Y 150
#define BLEUI_TEXT_SPACING 28
#define BLEUI_PROGRESS_Y 204
#define BLEUI_FOOTER_LINE_Y 248
#define BLEUI_CHIP_Y 260
#define BLEUI_CHIP_H 26

//
void BleUI_clear(ST7305_4p2_BW_DisplayDriver *display)
{
    display->clearDisplay();
}

//
void BleUI_header(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, const char *title, const char *detail)
{
    u8->setFont(u8g2_font_helvB18_tf);
    u8->setCursor(BLEUI_MARGIN, 38);
    u8->print(title);

    if (detail && detail[0])
    {
        u8->setFont(u8g2_font_helvR12_tf);
        u8->setCursor(BLEUI_W - BLEUI_MARGIN - u8->getUTF8Width(detail), 38);
        u8->print(detail);
    }

    display->drawLine(BLEUI_MARGIN, BLEUI_HEADER_LINE_Y, BLEUI_W - BLEUI_MARGIN, BLEUI_HEADER_LINE_Y, ST7305_COLOR_BLACK);
}

//
void BleUI_status(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int dot, const char *status)
{
    int cx = BLEUI_MARGIN + 8;
    int cy = BLEUI_STATUS_Y - 8;
    if (dot == BLEUI_DOT_FILLED)
        display->drawFilledCircle(cx, cy, 7, ST7305_COLOR_BLACK);
    else
    {
        display->drawCircle(cx, cy, 7, ST7305_COLOR_BLACK);
        display->drawCircle(cx, cy, 6, ST7305_COLOR_BLACK);
    }

    u8->setFont(u8g2_font_helvB18_tf);
    u8->setCursor(BLEUI_MARGIN + 28, BLEUI_STATUS_Y);
    u8->print(status);
}

//
void BleUI_text(U8G2_FOR_ST73XX *u8, int line, const char *text)
{
    if (!text || !text[0])
        return;

    u8->setFont(u8g2_font_helvR14_tf);
    u8->setCursor(BLEUI_MARGIN, BLEUI_TEXT_Y + line * BLEUI_TEXT_SPACING);
    u8->print(text);
}

//
void BleUI_progress(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    int barW = BLEUI_W - BLEUI_MARGIN * 2 - 64;

    // clear the progress row
    display->drawFilledRectangle(0, BLEUI_PROGRESS_Y - 14, BLEUI_W - 1, BLEUI_PROGRESS_Y + 16, ST7305_COLOR_WHITE);

    // track and fill
    display->drawRectangle(BLEUI_MARGIN, BLEUI_PROGRESS_Y, BLEUI_MARGIN + barW, BLEUI_PROGRESS_Y + 8, ST7305_COLOR_BLACK);
    int fillW = barW * percent / 100;
    if (fillW > 0)
        display->drawFilledRectangle(BLEUI_MARGIN, BLEUI_PROGRESS_Y, BLEUI_MARGIN + fillW, BLEUI_PROGRESS_Y + 8, ST7305_COLOR_BLACK);

    // percentage
    char label[8];
    snprintf(label, sizeof(label), "%d%%", percent);
    u8->setFont(u8g2_font_helvB14_tf);
    u8->setCursor(BLEUI_W - BLEUI_MARGIN - u8->getUTF8Width(label), BLEUI_PROGRESS_Y + 10);
    u8->print(label);
}

// inverted key chip followed by its action, returns the x after the pair
static int BleUI_chip(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int x, const char *key, const char *action)
{
    u8->setFont(u8g2_font_helvB12_tf);
    int keyW = u8->getUTF8Width(key) + 16;
    display->drawFilledRectangle(x, BLEUI_CHIP_Y, x + keyW, BLEUI_CHIP_Y + BLEUI_CHIP_H, ST7305_COLOR_BLACK);

    u8->setForegroundColor(ST7305_COLOR_WHITE);
    u8->setBackgroundColor(ST7305_COLOR_BLACK);
    u8->setCursor(x + 8, BLEUI_CHIP_Y + 19);
    u8->print(key);
    u8->setForegroundColor(ST7305_COLOR_BLACK);
    u8->setBackgroundColor(ST7305_COLOR_WHITE);

    x += keyW + 10;
    u8->setFont(u8g2_font_helvR12_tf);
    u8->setCursor(x, BLEUI_CHIP_Y + 19);
    u8->print(action);

    return x + u8->getUTF8Width(action) + 28;
}

//
void BleUI_footer(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, const char *key1, const char *action1, const char *key2, const char *action2)
{
    display->drawLine(BLEUI_MARGIN, BLEUI_FOOTER_LINE_Y, BLEUI_W - BLEUI_MARGIN, BLEUI_FOOTER_LINE_Y, ST7305_COLOR_BLACK);

    int x = BleUI_chip(display, u8, BLEUI_MARGIN, key1, action1);
    if (key2)
        BleUI_chip(display, u8, x, key2, action2);
}
