#include "GameScreen.h"
#include "app/app.h"
#include "display/ST7735/display_ST7735.h"

//
#include "service/Game/Game.h"

#define GAME_FONT u8g2_font_profont12_tf
#define GAME_WIDTH 160
#define GAME_HEIGHT 80
#define GAME_LINE_HEIGHT 12
#define GAME_STEP_PIXELS 1

// draws in a sprite and pushes the finished frame so that it doesn't flicker
class GameCanvasTFT : public GameCanvas
{
public:
    GameCanvasTFT(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
        : ptft(ptft), pu8f(pu8f), sprite(display_ST7735_sprite())
    {
        JsonDocument &app = status();
        background = app["config"]["background_color"].as<uint16_t>();
        foreground = app["config"]["foreground_color"].as<uint16_t>();
    }

    void begin() override
    {
        sprite.createSprite(GAME_WIDTH, GAME_HEIGHT);
        sprite.fillSprite(background);

        pu8f->begin(sprite);
        pu8f->setFont(GAME_FONT);
        pu8f->setForegroundColor(foreground);
        pu8f->setFontMode(1);
    }

    void text(int x, int y, const char *str) override
    {
        pu8f->setCursor(x, y);
        pu8f->print(str);
    }

    void fill(int x, int y, int width, int height) override
    {
        sprite.fillRect(x, y, width, height, foreground);
    }

    void end() override
    {
        sprite.pushSprite(0, 0);
        sprite.deleteSprite();
        pu8f->begin(*ptft);
    }

private:
    TFT_eSPI *ptft;
    U8g2_for_TFT_eSPI *pu8f;
    TFT_eSprite &sprite;
    uint16_t background;
    uint16_t foreground;
};

//
void GameScreen_setup(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    pu8f->setFont(GAME_FONT);
    int charWidth = pu8f->getUTF8Width("MM") - pu8f->getUTF8Width("M");

    game_setup({GAME_WIDTH, GAME_HEIGHT, charWidth, GAME_LINE_HEIGHT, GAME_STEP_PIXELS});
}

//
void GameScreen_render(TFT_eSPI *ptft, U8g2_for_TFT_eSPI *pu8f)
{
    GameCanvasTFT canvas(ptft, pu8f);
    game_render(canvas);
}

//
void GameScreen_keyboard(int key, bool pressed)
{
    game_keyboard(key, pressed);
}
