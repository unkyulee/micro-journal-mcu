#include "Config.h"
#include "app/app.h"
#include "display/display.h"


// Default Configuration
void _set_default_config()
{
    // load app status
    JsonDocument &app = status();
    _log("Creating default config.json template\n");
    
    // create a default config
    app["config"]["sync"]["url"] = "";
    app["config"]["ble"]["remote"] = "";

    //
    config_save();
}

// see Config.h
void config_cache_refresh()
{
    JsonDocument &app = status();

    // an empty layout means the default (US)
    appState().keyboardLayout = String(app["config"]["keyboard_layout"] | "");

    // a bluetooth keyboard is paired
    appState().bleConfigured = app["config"]["ble"]["address"].is<const char *>();
}

//
bool config_load()
{
    //
    _log("Loading config ... %s\n", VERSION);

    // load app status
    JsonDocument &app = status();

    // load config.json
    _log("Opening config.json file\n");
    File configFile = gfs()->open("/config.json", "r");
    if (configFile)
    {
        // read the file
        String configString = configFile.readString();
        configFile.close();

        // check if configString is empty
        if (configString.length() == 0)
        {
            // create a default config.json
            _set_default_config();

            // configuration is loaded with default
            // no need to proceed further
            return true;
        }

        //
        _debug(configString.c_str());

        // Prepare a JsonDocument for the configuration
        // The size should be adjusted according to your configuration's needs
        JsonDocument configDoc;

        // convert to JsonObject
        DeserializationError error = deserializeJson(configDoc, configString);
        _log("Deserializing config.json file\n");
        if (error || !configDoc.is<JsonObject>())
        {
            //
            _log("config.json is not valid JSON: %s\n", error.c_str());

            // A broken config.json used to stop the boot at an error
            // screen, with no way to repair the file from the device
            // itself. Keep the broken file as config.bad so nothing the
            // user wrote is lost (it can be fixed and renamed back in
            // Drive Mode), start from default settings and carry on.
            if (gfs()->exists("/config.bad"))
                gfs()->remove("/config.bad");

            if (gfs()->rename("/config.json", "/config.bad"))
                _log("Broken config.json kept as config.bad\n");
            else
                _log("Failed to keep the broken config.json as config.bad\n");

            // writes a fresh config.json
            _set_default_config();

            return true;
        }

        // Assign the loaded configuration to "config" property of app
        _log("Loading app status config\n");
        app["config"] = configDoc.as<JsonObject>();        
        _log("Config loaded successfully!\n");

        config_cache_refresh();
    }
    else
    {
        // file doesn't exist
        _log("config.json file doens't exist\n");

        // create a default config.json
        _set_default_config();        
    }

    return true;
}

//
void config_save()
{
    // the document just changed, bring the cached copies along
    config_cache_refresh();

    // load app status
    JsonDocument &app = status();

    // save config
    // Open the file for writing
    File configFile = gfs()->open("/config.json", "w");    
    if (!configFile)
    {
        //
        appState().error = "Failed to open config.json file for writing.\n";
        appState().screen = ERRORSCREEN; // ERROR SCREEN IS 0

        _log("%s", appState().error.get().c_str());

        return;
    }

    // Serialize the "config" property of the app Document directly to the file
    if (app["config"].is<JsonObject>())
    {
        String jsonOutput;
        serializeJsonPretty(app["config"], jsonOutput);
        configFile.println(jsonOutput);

        // debug output configuration content
        //_debug("config_save\n%s\n", jsonOutput.c_str());

        //
        //_log("Config updated successfully.\n");
    }
    else
    {
        //
        appState().error = "No 'config' property found in app Document.\n";
        appState().screen = ERRORSCREEN;

        _log("%s", appState().error.get().c_str());
    }

    // close config.json
    configFile.close();
}