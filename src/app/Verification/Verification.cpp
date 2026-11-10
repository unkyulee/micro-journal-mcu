#include "Verification.h"
#include "app/app.h"
#include "display/display.h"

#if defined(BOARD_ESP32_S3) && defined(SD_CS)
#include <SD.h>
#endif

// check firmware update
bool firmware_check()
{
#ifdef BOARD_ESP32_S3
    // load app status
    JsonDocument &app = status();

    // Check if there is a firmware file in the storage
    // FIRMWARE is defiined in platformio.ini
    _log("Checking for firmware update: %s\n", FIRMWARE);
    if (gfs()->exists(FIRMWARE))
    {
        // move to firmware update screen
        appState().screen = UPDATESCREEN;
        _log("New firmware found. Preparing Update.\n");
        return true;
    }
#endif

    _log("No new firmware found.\n");
    return false;
}

// check if file system is correctly loaded
bool filesystem_check()
{
    //
    JsonDocument &app = status();

    // Call File System for the first time to initialize
    gfs();

    // gfs() already put the reason on the error screen
    if (!gfs_ready())
    {
        return false;
    }

#if defined(SD_CS)
    // Check if SD card is inserted
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE)
    {
        //
        appState().error = "SD CARD NOT INSERTED\n";
        appState().screen = ERRORSCREEN;

        //
        _log("%s", appState().error.get().c_str());

        return false;
    }

    //
    _log("SD Card detected\n");
#endif

#if defined(DEBUG_FILE)
    // Delete debug.log
    const char *debugPath = "/debug.log";
    if (gfs()->exists(debugPath))
    {
        if (gfs()->remove(debugPath))
        {
            _log("Deleted %s\n", debugPath);
        }
        else
        {
            _log("Failed to delete %s\n", debugPath);
        }
    }
#endif

    // file system check pass
    return true;
}


// see Verification.h
void filesystem_format_confirm(char key)
{
    if (key != 'y' && key != 'Y')
        return;

    // only when the error screen is actually asking for it
    if (!gfs()->needsFormat())
        return;

    _log("Format confirmed by the user\n");
    if (!gfs()->format())
        _log("Format failed\n");
}
