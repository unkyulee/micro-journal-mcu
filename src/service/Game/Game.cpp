#include "Game.h"
#include "app/app.h"
#include "display/display.h"

// Rules
#define GAME_LIVES 10
#define GAME_POINTS_PER_WORD 10

// Pacing, tuned so someone new to a keyboard lasts a bit over a minute and a
// 70 WPM typist around five minutes. Every level both values are multiplied
// by the percentage until they reach the minimum.
#define GAME_LEVEL_MS 20000
#define GAME_SPAWN_MS 5500
#define GAME_SPAWN_PERCENT 90
#define GAME_SPAWN_MIN_MS 1000
#define GAME_FALL_MS 15000
#define GAME_FALL_PERCENT 95
#define GAME_FALL_MIN_MS 6000

// with an empty field the next word comes after this part of the interval
#define GAME_SPAWN_EMPTY_PERCENT 30

// longest possible frame time counted as play time
#define GAME_MAX_STEP_MS 2000

// the fastest pace has 7 words in the air
#define GAME_MAX_WORDS 12

// word position, from 0 at the top to GAME_GROUND when it lands
#define GAME_GROUND 100000L

// words of the same length, picked by the current level
static const char *const WORDS_3[] = {
    "act", "age", "air", "arm", "art", "bag", "bed", "big", "box", "bus",
    "car", "cat", "cup", "day", "dog", "ear", "egg", "eye", "fan", "fox",
    "fun", "hat", "ice", "jam", "key", "leg", "map", "mud", "net", "owl",
    "pen", "pig", "red", "run", "sea", "sky", "sun", "toy", "van", "web",
    "yes", "zoo"};
static const char *const WORDS_4[] = {
    "able", "baby", "ball", "bird", "blue", "book", "cake", "city", "door", "duck",
    "easy", "fish", "frog", "game", "gift", "gold", "hand", "home", "idea", "jump",
    "kite", "lake", "lamp", "leaf", "milk", "moon", "nest", "note", "open", "park",
    "quiz", "road", "rock", "ship", "snow", "star", "tree", "wind", "yard", "zero"};
static const char *const WORDS_5[] = {
    "beach", "bread", "chair", "cloud", "dance", "dream", "earth", "field", "fruit", "green",
    "happy", "house", "juice", "knife", "light", "magic", "music", "night", "ocean", "paper",
    "piano", "quiet", "river", "smile", "stone", "table", "tiger", "train", "under", "voice",
    "water", "whale", "world", "write", "young", "zebra"};
static const char *const WORDS_6[] = {
    "answer", "basket", "bridge", "camera", "circle", "dragon", "engine", "family", "flower", "forest",
    "garden", "guitar", "hammer", "island", "jacket", "kitten", "letter", "market", "nature", "orange",
    "pencil", "planet", "rabbit", "school", "silver", "spring", "summer", "ticket", "travel", "unique",
    "valley", "window", "winter", "yellow"};
static const char *const WORDS_7[] = {
    "balance", "battery", "bicycle", "captain", "chicken", "diamond", "dolphin", "evening", "fantasy", "freedom",
    "gallery", "harvest", "history", "journal", "journey", "kitchen", "library", "machine", "morning", "network",
    "october", "penguin", "picture", "quality", "rainbow", "science", "teacher", "thunder", "uniform", "village",
    "weather", "whisper"};
static const char *const WORDS_8[] = {
    "absolute", "airplane", "backpack", "birthday", "building", "calendar", "children", "computer", "daughter", "elephant",
    "exercise", "football", "fountain", "hospital", "keyboard", "language", "learning", "mountain", "notebook", "painting",
    "question", "sentence", "shoulder", "sunlight", "treasure", "umbrella", "vacation", "whatever", "wildlife", "yourself"};

struct GameWordList
{
    const char *const *words;
    int count;
};

#define GAME_WORD_LIST(list) {list, sizeof(list) / sizeof(list[0])}

// index is the word length minus GAME_MIN_LENGTH
#define GAME_MIN_LENGTH 3
static const GameWordList WORD_LISTS[] = {
    GAME_WORD_LIST(WORDS_3),
    GAME_WORD_LIST(WORDS_4),
    GAME_WORD_LIST(WORDS_5),
    GAME_WORD_LIST(WORDS_6),
    GAME_WORD_LIST(WORDS_7),
    GAME_WORD_LIST(WORDS_8),
};

enum GameState
{
    GAME_TITLE,
    GAME_PLAYING,
    GAME_OVER
};

struct GameWord
{
    const char *text; // NULL when the slot is free
    int length;
    int typed;
    int column;
    long position;
    int drawnY;
};

// layout
static GameLayout layout;
static int thickness;
static int textOffset; // baseline from the top of a text line
static int groundY;
static int firstY;
static int lastY;
static int columns;
static int marginX;
static long rowDistance; // the word position difference of one text line

// game
static GameState state;
static GameWord words[GAME_MAX_WORDS];
static int target; // index of the word being typed, -1 when none
static int score;
static int typedWords;
static int lives;
static int level;
static long spawnMs;
static long fallMs;
static long sinceSpawnMs;
static long nextSpawnMs;
static unsigned long playMs;

// redraw
static bool dirty;
static unsigned long lastUpdate;

// Key strokes from the keyboard core wait here until the display core takes
// them. One writer and one reader, each owns its index, no lock is needed.
#define GAME_KEY_QUEUE 32
static volatile int keyQueue[GAME_KEY_QUEUE];
static volatile unsigned int keyHead = 0;
static volatile unsigned int keyTail = 0;

// pixel baseline of a word
static int game_word_y(const GameWord &word)
{
    int y = firstY + (int)((long long)word.position * (lastY - firstY) / GAME_GROUND);
    return firstY + (y - firstY) / layout.stepPixels * layout.stepPixels;
}

static void game_update_pace()
{
    level = 1 + playMs / GAME_LEVEL_MS;

    spawnMs = GAME_SPAWN_MS;
    fallMs = GAME_FALL_MS;
    for (int i = 1; i < level; i++)
    {
        spawnMs = max(spawnMs * GAME_SPAWN_PERCENT / 100, (long)GAME_SPAWN_MIN_MS);
        fallMs = max(fallMs * GAME_FALL_PERCENT / 100, (long)GAME_FALL_MIN_MS);
    }
}

static void game_start()
{
    _log("Game started\n");

    randomSeed(micros());

    memset(words, 0, sizeof(words));
    target = -1;
    score = 0;
    typedWords = 0;
    lives = GAME_LIVES;
    playMs = 0;
    sinceSpawnMs = 0;
    game_update_pace();
    nextSpawnMs = spawnMs;

    state = GAME_PLAYING;
    dirty = true;
}

// is there a word this close below the top, at these columns?
static bool game_column_free(int column, int length)
{
    for (int i = 0; i < GAME_MAX_WORDS; i++)
    {
        const GameWord &word = words[i];
        if (word.text == NULL || word.position > rowDistance)
            continue;

        // keep one empty column between the words
        if (column <= word.column + word.length && word.column <= column + length)
            return false;
    }

    return true;
}

// every word starts with a different letter, the first key picks the word
static bool game_word_available(const char *text)
{
    for (int i = 0; i < GAME_MAX_WORDS; i++)
        if (words[i].text != NULL && words[i].text[0] == text[0])
            return false;

    return true;
}

// shortest and longest word of the current level
static void game_word_lengths(int &shortest, int &longest)
{
    if (level <= 2)
    {
        shortest = 3;
        longest = 4;
    }
    else if (level <= 5)
    {
        shortest = 3;
        longest = 5;
    }
    else if (level <= 9)
    {
        shortest = 4;
        longest = 6;
    }
    else
    {
        shortest = 5;
        longest = 8;
    }
}

static const char *game_pick_word()
{
    int shortest, longest;
    game_word_lengths(shortest, longest);

    const char *text = NULL;
    for (int tries = 0; tries < 10; tries++)
    {
        const GameWordList &list = WORD_LISTS[random(shortest, longest + 1) - GAME_MIN_LENGTH];
        text = list.words[random(list.count)];
        if (game_word_available(text))
            break;
    }

    return text;
}

// returns false when the top of the field has no room right now
static bool game_spawn()
{
    int slot = -1;
    for (int i = 0; i < GAME_MAX_WORDS && slot < 0; i++)
        if (words[i].text == NULL)
            slot = i;

    if (slot < 0)
        return false;

    const char *text = game_pick_word();
    int length = strlen(text);

    for (int tries = 0; tries < 10; tries++)
    {
        int column = random(columns - length + 1);
        if (game_column_free(column, length))
        {
            words[slot] = {text, length, 0, column, 0, firstY};
            dirty = true;
            return true;
        }
    }

    return false;
}

static int game_alive_words()
{
    int alive = 0;
    for (int i = 0; i < GAME_MAX_WORDS; i++)
        if (words[i].text != NULL)
            alive++;

    return alive;
}

static void game_remove_word(int index)
{
    words[index].text = NULL;
    if (target == index)
        target = -1;

    dirty = true;
}

static void game_over()
{
    _log("Game over. Score %d Words %d\n", score, typedWords);

    state = GAME_OVER;
    dirty = true;
}

static void game_update(long stepMs)
{
    playMs += stepMs;
    sinceSpawnMs += stepMs;

    int levelPrev = level;
    game_update_pace();
    if (level != levelPrev)
        dirty = true;

    // new word
    long wait = nextSpawnMs;
    if (game_alive_words() == 0)
        wait = wait * GAME_SPAWN_EMPTY_PERCENT / 100;

    if (sinceSpawnMs >= wait && game_spawn())
    {
        sinceSpawnMs = 0;
        nextSpawnMs = spawnMs;
    }

    // Every word falls at the same speed, so the distance between two words
    // never changes and a word placed without overlap never overlaps later.
    long distance = stepMs * GAME_GROUND / fallMs;
    for (int i = 0; i < GAME_MAX_WORDS; i++)
    {
        GameWord &word = words[i];
        if (word.text == NULL)
            continue;

        word.position += distance;

        // landed
        if (word.position >= GAME_GROUND)
        {
            game_remove_word(i);
            lives--;

            if (lives <= 0)
            {
                game_over();
                return;
            }
            continue;
        }

        if (game_word_y(word) != word.drawnY)
            dirty = true;
    }
}

static void game_type(int key)
{
    // backspace lets go of the current word
    if (key == '\b')
    {
        if (target >= 0)
        {
            words[target].typed = 0;
            target = -1;
            dirty = true;
        }
        return;
    }

    if (key >= 'A' && key <= 'Z')
        key = key - 'A' + 'a';

    // the first key picks the matching word closest to the ground
    if (target < 0)
    {
        for (int i = 0; i < GAME_MAX_WORDS; i++)
        {
            if (words[i].text == NULL || words[i].text[0] != key)
                continue;

            if (target < 0 || words[i].position > words[target].position)
                target = i;
        }

        if (target < 0)
            return;
    }

    // wrong keys are ignored, there is no penalty for a typo
    GameWord &word = words[target];
    if (word.text[word.typed] != key)
        return;

    word.typed++;
    dirty = true;

    if (word.typed == word.length)
    {
        game_remove_word(target);
        score += GAME_POINTS_PER_WORD;
        typedWords++;
    }
}

// returns false when the game was left
static bool game_handle_key(int key)
{
    if (key == MENU || key == 27)
    {
        appState().screen = MENUSCREEN;
        return false;
    }

    if (state == GAME_PLAYING)
        game_type(key);
    else if (key == '\n' || key == '\r')
        game_start();

    return true;
}

static void game_draw_centered(GameCanvas &canvas, int y, const char *str)
{
    canvas.text((layout.width - (int)strlen(str) * layout.charWidth) / 2, y, str);
}

// title and game over are a few lines in the middle of the screen
static void game_draw_message(GameCanvas &canvas, const char *const *lines, int count)
{
    int y = (layout.height - count * layout.lineHeight) / 2 + textOffset;
    for (int i = 0; i < count; i++)
    {
        if (lines[i][0] != 0)
            game_draw_centered(canvas, y, lines[i]);
        y += layout.lineHeight;
    }
}

static void game_draw_title(GameCanvas &canvas)
{
    const char *const lines[] = {
        "WORD RAIN",
        "",
        "Type the words",
        "before they land",
        "Enter: Start  Esc: Exit"};

    game_draw_message(canvas, lines, 5);
}

static void game_draw_over(GameCanvas &canvas)
{
    String scoreLine = format("SCORE %d", score);
    String wordsLine = format("%d WORDS IN %d:%02d", typedWords, (int)(playMs / 60000), (int)(playMs / 1000 % 60));

    const char *const lines[] = {
        "GAME OVER",
        "",
        scoreLine.c_str(),
        wordsLine.c_str(),
        "Enter: Again  Esc: Exit"};

    game_draw_message(canvas, lines, 5);
}

static void game_draw_status(GameCanvas &canvas)
{
    String left = format("SCORE %d", score);
    String middle = format("LV %d", level);
    String right = format("LIFE %d", lives);

    canvas.text(marginX, textOffset, left.c_str());
    canvas.text(layout.width - marginX - right.length() * layout.charWidth, textOffset, right.c_str());

    // the middle stays clear of the score on narrow screens
    int middleX = (layout.width - (int)middle.length() * layout.charWidth) / 2;
    middleX = max(middleX, marginX + ((int)left.length() + 1) * layout.charWidth);
    canvas.text(middleX, textOffset, middle.c_str());

    canvas.fill(0, layout.lineHeight, layout.width, thickness);
}

static void game_draw_field(GameCanvas &canvas)
{
    canvas.fill(0, groundY, layout.width, thickness * 2);

    for (int i = 0; i < GAME_MAX_WORDS; i++)
    {
        GameWord &word = words[i];
        if (word.text == NULL)
            continue;

        word.drawnY = game_word_y(word);

        int x = marginX + word.column * layout.charWidth;
        canvas.text(x, word.drawnY, word.text);

        // bar under the letters already typed
        if (word.typed > 0)
            canvas.fill(x, word.drawnY + thickness, word.typed * layout.charWidth, thickness * 2);
    }
}

//
void game_setup(const GameLayout &gameLayout)
{
    layout = gameLayout;

    thickness = max(1, layout.lineHeight / 16);
    textOffset = layout.lineHeight * 4 / 5;

    columns = layout.width / layout.charWidth;
    marginX = (layout.width - columns * layout.charWidth) / 2;

    // status bar on the top, the ground at the bottom. A word landing sits
    // right above the ground with room for the bar under its letters.
    groundY = layout.height - thickness * 2;
    firstY = layout.lineHeight + thickness + textOffset;
    lastY = groundY - thickness * 3;

    // one extra step, because positions round to a step when they are drawn
    rowDistance = (long)(layout.lineHeight + layout.stepPixels) * GAME_GROUND / (lastY - firstY);

    // keys left from an earlier game
    keyTail = keyHead;

    state = GAME_TITLE;
    dirty = true;
    lastUpdate = millis();
}

//
bool game_render(GameCanvas &canvas)
{
    while (keyTail != keyHead)
    {
        int key = keyQueue[keyTail];
        keyTail = (keyTail + 1) % GAME_KEY_QUEUE;

        if (!game_handle_key(key))
            return false;
    }

    unsigned long now = millis();
    long stepMs = min(now - lastUpdate, (unsigned long)GAME_MAX_STEP_MS);
    lastUpdate = now;

    if (state == GAME_PLAYING)
        game_update(stepMs);

    if (!dirty)
        return false;

    dirty = false;

    canvas.begin();

    if (state == GAME_TITLE)
        game_draw_title(canvas);
    else if (state == GAME_OVER)
        game_draw_over(canvas);
    else
    {
        game_draw_status(canvas);
        game_draw_field(canvas);
    }

    canvas.end();

    return true;
}

//
void game_keyboard(int key, bool pressed)
{
    // The menu key reacts to the release like everywhere else, the knob long
    // press only ever sends a release. Typing counts from the press, because
    // when typing fast a key is often released after the next one is pressed.
    if ((key == MENU || key == 27) ? pressed : !pressed)
        return;

    unsigned int next = (keyHead + 1) % GAME_KEY_QUEUE;

    // queue is full, the key is lost
    if (next == keyTail)
        return;

    keyQueue[keyHead] = key;
    keyHead = next;
}
