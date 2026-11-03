#include "GameScreen.h"
#include "app/app.h"

//
#include "service/Game/Game.h"

#define GAME_FONT u8g2_font_profont22_mf
#define GAME_WIDTH 400
#define GAME_HEIGHT 300
#define GAME_LINE_HEIGHT 24
#define GAME_STEP_PIXELS 2

static bool needsDisplay = true;

// draws in the frame buffer, the display loop pushes it to the panel
class GameCanvasRLCD : public GameCanvas
{
public:
    GameCanvasRLCD(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
        : display(display), u8(u8) {}

    void begin() override
    {
        display->clearDisplay();
        u8->setFont(GAME_FONT);
    }

    void text(int x, int y, const char *str) override
    {
        u8->drawUTF8(x, y, str);
    }

    void fill(int x, int y, int width, int height) override
    {
        display->drawFilledRectangle(x, y, x + width - 1, y + height - 1, ST7305_COLOR_BLACK);
    }

    void end() override {}

private:
    ST7305_4p2_BW_DisplayDriver *display;
    U8G2_FOR_ST73XX *u8;
};

//
void GameScreen_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    u8->setFont(GAME_FONT);
    int charWidth = u8->getUTF8Width("MM") - u8->getUTF8Width("M");

    game_setup({GAME_WIDTH, GAME_HEIGHT, charWidth, GAME_LINE_HEIGHT, GAME_STEP_PIXELS});
}

//
void GameScreen_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    GameCanvasRLCD canvas(display, u8);
    needsDisplay = game_render(canvas);
}

bool GameScreen_needsDisplay()
{
    return needsDisplay;
}

//
void GameScreen_keyboard(int key, bool pressed)
{
    game_keyboard(key, pressed);
}
