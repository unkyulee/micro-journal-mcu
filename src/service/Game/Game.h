#pragma once

// WORD RAIN - words fall from the top of the screen, typing one removes it.
//
// The whole game lives here: rules, pacing, word list, input, screen layout
// and the decision of when to redraw. A display only reports its geometry
// and implements GameCanvas, see display/<type>/GameScreen.

// Drawing primitives every display provides. All values are in pixels and
// the service never draws outside of the width and height in GameLayout.
class GameCanvas
{
public:
    // start a new blank frame
    virtual void begin() = 0;

    // single line of text, y is the baseline
    virtual void text(int x, int y, const char *str) = 0;

    // rectangle in the text color
    virtual void fill(int x, int y, int width, int height) = 0;

    // show the finished frame
    virtual void end() = 0;
};

struct GameLayout
{
    int width;
    int height;

    // metrics of the fixed width font the canvas draws with
    int charWidth;
    int lineHeight;

    // falling words move in steps of this many pixels
    // a coarse step means fewer redraws for displays with a slow refresh
    int stepPixels;
};

// call when the game screen opens
void game_setup(const GameLayout &layout);

// Advance the game and redraw when something visible changed.
// Call from the display loop. Returns true when a new frame was drawn.
bool game_render(GameCanvas &canvas);

// Forward key strokes here, safe to call from the keyboard core.
// They are queued and handled inside game_render on the display core.
void game_keyboard(int key, bool pressed);
