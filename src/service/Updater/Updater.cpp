#include "Updater.h"
#include "app/app.h"
#include "display/display.h"

#include <Update.h> // Required for firmware update

void run_firmare_update(const char* filename)
{
    //
    JsonDocument &app = status();

    //
    _log("[run_firmare_update] Firmware update: %s\n", filename);

    //
    File firmwareFile = gfs()->open(filename, "r");
    if (!firmwareFile)
    {
        //
        appState().error = format("ERROR OPENING: %s\n", filename);
        appState().screen = ERRORSCREEN;

        _log("%s", appState().error.get().c_str());        

        return;
    }

    // Perform firmware update
    size_t fileSize = firmwareFile.size();
    if (Update.begin(fileSize))
    {
        if (Update.writeStream(firmwareFile) == fileSize)
        {
            if (Update.end())
            {
                Serial.println("Firmware update successful!");
                gfs()->remove(filename);

                // restart
                ESP.restart();
            }
            else
            {
                Update.printError(Serial);
                Serial.println("Error ending update!");

                //
                appState().error = "UPDATE FAILED AT THE END";
                appState().screen = ERRORSCREEN;
            }
        }
        else
        {
            Update.printError(Serial);
            Serial.println("Error writing firmware!");

            //
            appState().error = "UPDATE FAILED TO APPLY";
            appState().screen = ERRORSCREEN;
        }
    }
    else
    {
        Update.printError(Serial);
        Serial.println("Error beginning update!");

        //
        appState().error = "UPDATE FAILED TO BEGIN";
        appState().screen = ERRORSCREEN;
    }

    // Close firmware file
    firmwareFile.close();
}
