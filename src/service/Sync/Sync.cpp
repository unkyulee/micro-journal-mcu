#include "Sync.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"

//
#include <HTTPClient.h>


// at least one network saved in wifi.json (loaded by wifi_config_load)
static bool sync_wifi_configured()
{
    JsonDocument &app = status();
    JsonArray accessPoints = app["wifi"]["access_points"].as<JsonArray>();
    for (JsonVariant accessPoint : accessPoints)
    {
        const char *ssid = accessPoint["ssid"] | "";
        if (ssid[0] != '\0')
            return true;
    }
    return false;
}

// "sync": { "url": "..." } in config.json
static bool sync_url_configured()
{
    JsonDocument &app = status();
    String url = app["config"]["sync"]["url"] | "";
    url.trim();
    return !url.isEmpty();
}

// Reset all the sync related flags
void sync_init()
{
    // reset the sync state
    // update app sync state
    JsonDocument &app = status();
    appState().syncState = SYNC_START;
    appState().syncError = "";
    appState().syncMessage = "";

    // nothing to start yet, the screen explains what is missing
    if (!sync_wifi_configured() || !sync_url_configured())
        appState().syncState = SYNC_NOT_CONFIGURED;

    // Earlier versions uploaded from a temporary encoded copy of the
    // journal and could leave it behind. Remove it here, while WiFi is
    // still off.
    String leftover = Editor::getInstance().fileName + ".base64";
    if (!Editor::getInstance().fileName.isEmpty() && gfs()->exists(leftover.c_str()))
        gfs()->remove(leftover.c_str());

    _log("[sync_init] sync_init state %d\n", appState().syncState.load());
}

//
int sync_setup_lines(String *lines, int max)
{
    int count = 0;
    auto add = [&](const char *line)
    {
        if (count < max)
            lines[count++] = line;
    };

    bool wifi = sync_wifi_configured();
    bool url = sync_url_configured();
    if (wifi && url)
        return 0;

    // lines are kept short enough for the narrowest screen
    if (!wifi)
    {
        add("WiFi is not set up.");
        add("Use MENU > [W] WIFI SETUP.");
        add("");
    }

    if (!url)
    {
        add("Google Drive backup is not set up.");
        add("Use MENU > [E] DRIVE MODE to edit");
        add("config.json and add the backup url:");
        add("  \"sync\": { \"url\": \"...\" }");
        add("");
    }

    add("Press any key to go back");

    return count;
}

// request background service to pick up the request
void sync_start_request()
{
    // asked on every redraw while the screen waits for the backup to begin,
    // the request is simply set again
    appState().requestTask(TASK_SYNC_START);
}

//
void sync_loop()
{
    static unsigned int last = 0;
    if (millis() - last > 1000)
    {
        last = millis();

        //
        JsonDocument &app = status();
        if (appState().takeTask(TASK_SYNC_START))
        {
            //
            _log("[sync_loop] Picked up Sync Start Request\n");

            //
            sync_start();
        }
    }
}

// Start Sync Process
// Search for WIFI ACCESS POINTS
void sync_start()
{
    //
    JsonDocument &app = status();

    //
    _log("[sync_start] Sync Start\n");

    //
    appState().syncState = SYNC_STARTED;

    // a redraw may have asked again between the request being picked up
    // and the state changing just now - this run is the one it asked for
    appState().cancelTask(TASK_SYNC_START);
    appState().syncMessage = "Connecting to WiFi";
    appState().clear = true;

    // full CPU speed while WiFi is in use
    setCpuFrequencyMhz(CPU_FREQUENCY_FULL);

    // Scan for available networks
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("MICROJOURNAL");

    // wait for decent amount of time before using wifi
    delay(3000);

    //
    int networksFound = WiFi.scanNetworks();
    appState().syncMessage = format("Found %d networks", networksFound);
    appState().clear = true;

    // reset the network.access_points array
    JsonArray access_points = app["network"]["access_points"].to<JsonArray>();
    access_points.clear();

    // Print information about each network
    for (int i = 0; i < networksFound; ++i)
    {
        //
        String ssid = WiFi.SSID(i);

        // print out the information
        _log("%d: %s (%d dBm)\n", i + 1, ssid.c_str(), WiFi.RSSI(i));

        // add to the access_points list
        access_points.add(ssid);
    }

    //
    // Load available WiFi networks from the app["network"]["access_points"] array
    JsonArray availableNetworks = app["network"]["access_points"].as<JsonArray>();

    // Load saved WiFi connection information from the app["config"]["access_points"] array
    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

    // Iterate through each available network
    for (JsonVariant availableNetwork : availableNetworks)
    {
        // available wifi network
        String availableSsid = availableNetwork.as<String>();
        _log("Trying: %s\n", availableSsid.c_str());

        // see if there are any wifi network allowed
        for (JsonVariant savedAccessPoint : savedAccessPoints)
        {
            const char *savedSsid = savedAccessPoint["ssid"];
            const char *savedPassword = savedAccessPoint["password"];

            _log("Trying to connect to :%s\n", savedSsid);

            // Check if the SSID and password match
            if (strcmp(availableSsid.c_str(), savedSsid) == 0)
            {

                // Try to connect to the matching access point
                if (sync_connect_wifi(app, savedSsid, savedPassword))
                {
                    _log("Connected to a matching WiFi network!\n");

                    //
                    appState().syncMessage = format("Connected to: %s\n", savedSsid);
                    appState().clear = true;
                    _log("%s", appState().syncMessage.get().c_str());
                    delay(1000); // give some delay for the connection to settle

                    // Sync File Start
                    sync_send();

                    return; // Exit the function if successfully connected
                }
            }
        }
    }

    // if it reached this point.
    // it means no network has been connected
    sync_stop();

    //
    appState().syncError = "NOT ABLE TO CONNECT TO WIFI";
    appState().syncState = SYNC_ERROR;
    appState().clear = true;
    //
}

void sync_stop()
{
    // power down the WiFi radio entirely
    // WiFi.disconnect alone leaves the radio on in STA mode draining the battery
    // WiFi.begin re-enables STA mode so retrying another network still works
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // back to the battery saving CPU speed
    setCpuFrequencyMhz(CPU_FREQUENCY_LOW);
}

bool sync_connect_wifi(JsonDocument &app, const char *ssid, const char *password)
{
    // Connect to WiFi
    delay(1000);

    //
    String message = format("trying to connect to %s ", ssid);
    appState().syncMessage = message;
    appState().clear = true;
    _log(message.c_str());

    //
    WiFi.begin(ssid, password);
    delay(1000);

    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 100)
    {
        delay(100);
        message += ".";
        appState().syncMessage = message;
        appState().clear = true;
        attempt++;
    }

    // Update the network_status: 1
    // meaning it is connected
    if (WiFi.status() == WL_CONNECTED)
    {
        // network status 1
        app["network"]["IP"] = WiFi.localIP().toString();
        app["network"]["ssid"] = ssid;
        app["network"]["status"] = 1;

        // print info
        appState().syncMessage = format("Connected - %s, %s\n", ssid, WiFi.localIP().toString().c_str());
        appState().clear = true;

        return true;
    }
    else
    {
        _log("Failed to connect to WiFi. Please check your credentials.\n");

        //
        appState().syncError = format("Failed to connect to %s", ssid);
        appState().syncState = SYNC_ERROR;
        appState().clear = true;

        // drop the association only
        // sync_start still retries the remaining saved networks
        // the final sync_stop powers down the radio
        WiFi.disconnect();

        return false; // Failed to connect
    }
}

// how long to wait for the server's answer to the upload, in milliseconds
// (HTTPClient keeps this in 16 bits, so 65 seconds is the ceiling)
#define SYNC_RESPONSE_TIMEOUT 60000

// Decide whether the upload worked. Returns the message for the screen,
// empty when the backup succeeded.
//   httpCode  - answer to the POST
//   resultUrl - where a redirect answer points to, empty otherwise
static String sync_result(int httpCode, const String &resultUrl)
{
    // connection errors: no route, timeout, TLS
    if (httpCode < 0)
        return format("Backup failed: %s", HTTPClient::errorToString(httpCode).c_str());

    // the server refused or failed the upload
    if (httpCode >= 400)
        return format("Backup failed: HTTP %d", httpCode);

    // a server that answers directly, without a redirect
    if (resultUrl.isEmpty())
        return "";

    // Google Apps Script: the script has run by now, and what it returned -
    // {"status":"OK"} or {"status":"ERROR",...} - is read from the address
    // the redirect points to. The HTTP status alone says nothing, a script
    // that failed still answers 200.
    HTTPClient http;
    http.begin(resultUrl);
    http.setTimeout(30000);
    int code = http.GET();
    String body = code == HTTP_CODE_OK ? http.getString() : String();
    http.end();

    _log("Backup result: %d %s\n", code, body.substring(0, 80).c_str());

    // The file was delivered and the script ran; only its report could not
    // be fetched. Not worth calling the backup failed.
    if (code != HTTP_CODE_OK)
        return "";

    JsonDocument result;
    if (deserializeJson(result, body))
    {
        // not the script's JSON: a Google sign-in page, when the web app
        // is not deployed with access for "Anyone"
        return "Unexpected reply. Set script access: Anyone";
    }

    String state = result["status"] | "";
    if (state == "ERROR")
    {
        // the script could not write the file, usually because the folder
        // it saves to is missing. It also emails the details.
        return "Google script error. Check uJournal folder.";
    }

    return "";
}

// Reads a file and hands it out base64 encoded, the form the backup script
// expects, while it is being uploaded.
//
// The encoded text used to be written to a temporary file first. That meant
// erasing and writing flash while the WiFi radio was on, and the two together
// can pull the 3.3V supply low enough to reset the device (the same reason
// Drive Mode lowers its transmit power). Encoding on the fly needs no flash
// write at all, and no free space for a second copy of the journal.
class Base64FileStream : public Stream
{
public:
    Base64FileStream(File &file) : _file(file) {}

    // length of the whole encoded text, known before anything is read
    size_t encodedSize()
    {
        return ((_file.size() + 2) / 3) * 4;
    }

    // -1 at the end of the file, which is what stops the upload loop
    int available() override
    {
        if (_pos >= _len)
            fill();
        return _len > _pos ? (int)(_len - _pos) : -1;
    }

    int read() override
    {
        if (available() <= 0)
            return -1;
        return _encoded[_pos++];
    }

    int peek() override
    {
        if (available() <= 0)
            return -1;
        return _encoded[_pos];
    }

    size_t readBytes(char *buffer, size_t length) override
    {
        size_t count = 0;
        while (count < length && available() > 0)
        {
            size_t size = _len - _pos;
            if (size > length - count)
                size = length - count;

            memcpy(buffer + count, _encoded + _pos, size);
            _pos += size;
            count += size;
        }
        return count;
    }

    void flush() override {}
    size_t write(uint8_t) override { return 0; }

private:
    // encode the next piece of the file
    void fill()
    {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        _pos = 0;
        _len = 0;

        // Always a multiple of 3 bytes, so padding can only appear at the
        // very end of the file. Keep reading until the piece is full: a
        // short read in the middle would put padding in the middle.
        uint8_t raw[RAW_SIZE];
        size_t count = 0;
        while (count < RAW_SIZE)
        {
            size_t size = _file.read(raw + count, RAW_SIZE - count);
            if (size == 0)
                break;
            count += size;
        }

        for (size_t i = 0; i < count; i += 3)
        {
            size_t left = count - i;
            uint32_t b = (uint32_t)raw[i] << 16;
            if (left > 1)
                b |= (uint32_t)raw[i + 1] << 8;
            if (left > 2)
                b |= raw[i + 2];

            _encoded[_len++] = table[(b >> 18) & 0x3F];
            _encoded[_len++] = table[(b >> 12) & 0x3F];
            _encoded[_len++] = left > 1 ? table[(b >> 6) & 0x3F] : '=';
            _encoded[_len++] = left > 2 ? table[b & 0x3F] : '=';
        }
    }

    static const size_t RAW_SIZE = 300; // multiple of 3
    File &_file;
    char _encoded[RAW_SIZE / 3 * 4];
    size_t _len = 0;
    size_t _pos = 0;
};

void sync_send()
{
    _log("[sync_send] Backup Send\n");

    //
    JsonDocument &app = status();

    // The order below matters: everything that needs the network happens
    // first, then WiFi is switched off (sync_stop), and only then the
    // result goes on screen. Nothing writes to flash while the radio is on.

    // where to send the file
    String url = app["config"]["sync"]["url"] | "";
    url.trim();
    if (url.isEmpty())
    {
        sync_stop();

        appState().syncError = "BACKUP URL NOT FOUND\n";
        appState().syncState = SYNC_ERROR;
        appState().clear = true;

        _log("%s", appState().syncError.get().c_str());
        return;
    }

    // Get the input file
    File inputFile = gfs()->open(Editor::getInstance().fileName.c_str(), "r");
    if (!inputFile)
    {
        sync_stop();

        appState().syncError = "Error opening input file!\n";
        appState().syncState = SYNC_ERROR;
        appState().clear = true;

        _log("%s", appState().syncError.get().c_str());
        return;
    }

    // Check file size
    if (inputFile.size() == 0)
    {
        inputFile.close();
        sync_stop();

        appState().syncError = "File is empty. Nothing to back up.\n";
        appState().syncState = SYNC_ERROR;
        appState().clear = true;

        _log("%s", appState().syncError.get().c_str());
        return;
    }

    //
    appState().syncMessage = "Sending file to drive ... ";
    appState().clear = true;

    _log("Requesting backup ... \n");
    _log("%s\n", url.c_str());

    int httpCode;
    String resultUrl;
    {
        // the file is encoded as it is read, see Base64FileStream
        Base64FileStream body(inputFile);
        size_t bodySize = body.encodedSize();
        _log("Sending file: %s, %d bytes encoded\n",
             Editor::getInstance().fileName.c_str(), (int)bodySize);

        // prepare http client
        HTTPClient http;
        http.begin(url);

        // close the connection when done: the result is fetched over a
        // second secure connection, and two of them open at once need
        // more memory than there may be
        http.setReuse(false);

        // A Google Apps Script only answers after it has run - decoding the
        // text and creating the file on Drive takes several seconds. The
        // default of 5 seconds gave up on a backup that was in fact working
        // ("read Timeout").
        http.setTimeout(SYNC_RESPONSE_TIMEOUT);

        httpCode = http.sendRequest("POST", &body, bodySize);
        _log("Backup response: %d\n", httpCode);

        // Apps Script never answers the POST with the result itself: it
        // replies 302 and the script's own output waits at that address.
        // (HTTPClient does not follow redirects for a streamed upload.)
        if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND ||
            httpCode == HTTP_CODE_SEE_OTHER || httpCode == HTTP_CODE_TEMPORARY_REDIRECT)
        {
            resultUrl = http.getLocation();
        }

        // close http connection
        http.end();
    }

    //
    inputFile.close();

    // work out the outcome while WiFi is still up
    String error = sync_result(httpCode, resultUrl);

    // radio off before anything else happens
    sync_stop();

    if (error.isEmpty())
    {
        appState().syncState = SYNC_COMPLETED;
        appState().clear = true;
        _log("Backup State: SYNC_COMPLETED\n");
    }
    else
    {
        appState().syncError = error;
        appState().syncState = SYNC_ERROR;
        appState().clear = true;

        _log("Backup State: SYNC_ERROR %s\n", error.c_str());
    }
}
