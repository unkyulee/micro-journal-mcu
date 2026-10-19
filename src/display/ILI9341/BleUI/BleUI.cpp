#include "BleUI.h"

// FreeSans fonts come from TFT_eSPI (LOAD_GFXFF includes them via gfxfont.h)

#define BLEUI_MARGIN 16
#define BLEUI_HEADER_LINE_Y 44
#define BLEUI_STATUS_Y 90
#define BLEUI_TEXT_Y 124
#define BLEUI_TEXT_SPACING 22
#define BLEUI_PROGRESS_Y 170
#define BLEUI_FOOTER_LINE_Y 200
#define BLEUI_CHIP_Y 210
#define BLEUI_CHIP_H 22

//
void BleUI_clear(TFT_eSPI *tft)
{
    tft->fillScreen(BLEUI_BG);
}

//
void BleUI_header(TFT_eSPI *tft, const char *title, const char *detail)
{
    int width = tft->width();

    tft->setFreeFont(&FreeSansBold12pt7b);
    tft->setTextColor(BLEUI_TEXT);
    tft->setTextDatum(L_BASELINE);
    tft->drawString(title, BLEUI_MARGIN, 30);

    if (detail && detail[0])
    {
        tft->setFreeFont(&FreeSans9pt7b);
        tft->setTextColor(BLEUI_MUTED);
        tft->setTextDatum(R_BASELINE);
        tft->drawString(detail, width - BLEUI_MARGIN, 30);
    }

    tft->drawFastHLine(BLEUI_MARGIN, BLEUI_HEADER_LINE_Y, width - BLEUI_MARGIN * 2, BLEUI_LINE);
}

//
void BleUI_status(TFT_eSPI *tft, uint16_t color, const char *status)
{
    // status dot with a soft halo
    tft->fillSmoothCircle(BLEUI_MARGIN + 7, BLEUI_STATUS_Y - 8, 9, BLEUI_LINE, BLEUI_BG);
    tft->fillSmoothCircle(BLEUI_MARGIN + 7, BLEUI_STATUS_Y - 8, 5, color, BLEUI_LINE);

    tft->setFreeFont(&FreeSansBold12pt7b);
    tft->setTextColor(BLEUI_TEXT);
    tft->setTextDatum(L_BASELINE);
    tft->drawString(status, BLEUI_MARGIN + 26, BLEUI_STATUS_Y);
}

//
void BleUI_text(TFT_eSPI *tft, int line, const char *text)
{
    if (!text || !text[0])
        return;

    tft->setFreeFont(&FreeSans9pt7b);
    tft->setTextColor(BLEUI_MUTED);
    tft->setTextDatum(L_BASELINE);
    tft->drawString(text, BLEUI_MARGIN, BLEUI_TEXT_Y + line * BLEUI_TEXT_SPACING);
}

//
void BleUI_progress(TFT_eSPI *tft, int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    int width = tft->width();
    int barW = width - BLEUI_MARGIN * 2 - 52;

    // clear the progress row
    tft->fillRect(0, BLEUI_PROGRESS_Y - 12, width, 26, BLEUI_BG);

    // track and fill
    tft->fillSmoothRoundRect(BLEUI_MARGIN, BLEUI_PROGRESS_Y, barW, 6, 3, BLEUI_LINE, BLEUI_BG);
    int fillW = barW * percent / 100;
    if (fillW >= 6)
        tft->fillSmoothRoundRect(BLEUI_MARGIN, BLEUI_PROGRESS_Y, fillW, 6, 3, BLEUI_ACTIVE, BLEUI_LINE);

    // percentage
    char label[8];
    snprintf(label, sizeof(label), "%d%%", percent);
    tft->setFreeFont(&FreeSans9pt7b);
    tft->setTextColor(BLEUI_TEXT);
    tft->setTextDatum(R_BASELINE);
    tft->drawString(label, width - BLEUI_MARGIN, BLEUI_PROGRESS_Y + 8);
}

// key chip followed by its action, returns the x after the pair
static int BleUI_chip(TFT_eSPI *tft, int x, const char *key, const char *action)
{
    tft->setFreeFont(&FreeSansBold9pt7b);
    int keyW = tft->textWidth(key) + 16;
    tft->fillSmoothRoundRect(x, BLEUI_CHIP_Y, keyW, BLEUI_CHIP_H, 5, BLEUI_CHIP, BLEUI_BG);
    tft->setTextColor(BLEUI_TEXT);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(key, x + keyW / 2, BLEUI_CHIP_Y + BLEUI_CHIP_H / 2);

    x += keyW + 8;
    tft->setFreeFont(&FreeSans9pt7b);
    tft->setTextColor(BLEUI_MUTED);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(action, x, BLEUI_CHIP_Y + BLEUI_CHIP_H / 2);

    return x + tft->textWidth(action) + 24;
}

//
void BleUI_footer(TFT_eSPI *tft, const char *key1, const char *action1, const char *key2, const char *action2)
{
    tft->drawFastHLine(BLEUI_MARGIN, BLEUI_FOOTER_LINE_Y, tft->width() - BLEUI_MARGIN * 2, BLEUI_LINE);

    int x = BleUI_chip(tft, BLEUI_MARGIN, key1, action1);
    if (key2)
        BleUI_chip(tft, x, key2, action2);
}

//
void BleUI_end(TFT_eSPI *tft)
{
    tft->setFreeFont(NULL);
    tft->setTextFont(1);
    tft->setTextDatum(TL_DATUM);
}
