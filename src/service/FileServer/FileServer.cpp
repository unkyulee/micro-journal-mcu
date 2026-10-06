#include "FileServer.h"
#include "FileServerPage.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"
#include "service/WifiEntry/WifiEntry.h"

//
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

//
#define FILESERVER_HOSTNAME "microjournal"
#define FILESERVER_AP_SSID "MicroJournal"
#define FILESERVER_AP_PASSWORD "microjournal"

// how long to wait for each saved access point
#define FILESERVER_CONNECT_TIMEOUT 10000

// WiFi transmit power while Drive Mode is open, 8.5 dBm instead of the
// default 19.5 dBm. The radio transmitting at full power while a flash
// block is erased can pull the 3.3V supply low enough to reset the device
// (seen on Rev.6 with a weak supply). Range drops to roughly one room.
#define FILESERVER_TX_POWER WIFI_POWER_8_5dBm

// uploads land in a temp file first and are swapped in only when complete,
// so a dropped connection never leaves a half written journal behind
#define FILESERVER_TMP_SUFFIX ".webtmp"
#define FILESERVER_BAK_SUFFIX ".webbak"

// target file of an upload, URL encoded
#define FILESERVER_NAME_HEADER "X-File-Name"

// set to 1 by the upload button, the only request allowed to create a file
#define FILESERVER_UPLOAD_HEADER "X-Upload"

// requests come from the display core, the work happens in fileserver_loop
static volatile bool startRequested = false;
static volatile bool stopRequested = false;
static volatile int state = FILESERVER_IDLE;

//
static WebServer *server = nullptr;

// what the browser changed during this session
// decides how the device resumes when the session ends
static bool editorFileChanged = false;
static bool configChanged = false;

// upload in progress
static File uploadFile;
static String uploadTarget;
static String uploadError;
static bool uploadComplete = false;

// An upload is collected in PSRAM first and written out once the transfer is
// done, so the radio is not busy receiving while flash blocks are erased -
// the two together draw the most current.
// Without PSRAM, or when it doesn't fit, the body streams straight to the file.
static uint8_t *uploadBuffer = nullptr;
static size_t uploadBufferSize = 0;
static size_t uploadBufferUsed = 0;

//
static void fileserver_start();
static void fileserver_stop();

//
void fileserver_start_request()
{
    if (state == FILESERVER_IDLE)
    {
        stopRequested = false;
        startRequested = true;
        _log("[fileserver_start_request] File Server Start Requested\n");
    }
}

//
void fileserver_stop_request()
{
    startRequested = false;
    stopRequested = true;
    status()["clear"] = true;
    _log("[fileserver_stop_request] File Server Stop Requested\n");
}

//
int fileserver_state()
{
    return state;
}

//
int fileserver_status_lines(String *lines, int max)
{
    JsonDocument &app = status();
    int count = 0;
    auto add = [&](const String &line)
    {
        if (count < max)
            lines[count++] = line;
    };

    String message = app["fileserver"]["message"] | "";

    if (stopRequested)
    {
        add("Closing ...");
    }
    else if (state == FILESERVER_RUNNING && app["fileserver"]["ap_ssid"].is<const char *>())
    {
        // hotspot: the PC has to switch to the journal's own WiFi first
        add("No saved WiFi network nearby,");
        add("so the journal made its own WiFi.");
        add("1. On your PC or phone, join WiFi:");
        add(format("   %s", app["fileserver"]["ap_ssid"] | ""));
        add(format("   Password: %s", app["fileserver"]["ap_password"] | ""));
        add("2. Then open in your browser:");
        add(format("   %s", app["fileserver"]["url"] | ""));

        if (app["fileserver"]["login"] | false)
            add("   Login: " FILESERVER_USER " / web password");

        add("");
        add("Press ESC to finish");
    }
    else if (state == FILESERVER_RUNNING)
    {
        add("Open in a browser:");
        add(app["fileserver"]["url"] | "");

        if (app["fileserver"]["mdns"] | false)
            add("or http://" FILESERVER_HOSTNAME ".local");

        if (app["fileserver"]["login"] | false)
            add("Login: " FILESERVER_USER " / web password");

        add("");
        add("Press ESC to finish");
    }
    else if (state == FILESERVER_ERROR)
    {
        add(message);
        add("");
        add("Press any key to go back");
    }
    else
    {
        add(message.isEmpty() ? "Starting ..." : message);
    }

    return count;
}

//
void fileserver_loop()
{
    if (startRequested)
    {
        startRequested = false;
        fileserver_start();
    }

    if (stopRequested)
    {
        stopRequested = false;
        fileserver_stop();
        return;
    }

    if (server != nullptr)
    {
        server->handleClient();
    }
}

//
// HELPERS
//

// show progress on the device screen
static void fileserver_message(const String &message)
{
    JsonDocument &app = status();
    app["fileserver"]["message"] = message;
    app["clear"] = true;

    _log("[fileserver] %s\n", message.c_str());
}

// optional password from config.json - "web": { "password": "..." }
static String fileserver_password()
{
    JsonDocument &app = status();
    return String(app["config"]["web"]["password"] | "");
}

// check credentials without sending a response
static bool fileserver_credentials_ok()
{
    String password = fileserver_password();
    return password.isEmpty() || server->authenticate(FILESERVER_USER, password.c_str());
}

// check credentials and ask the browser to login when missing
static bool fileserver_authorized()
{
    if (fileserver_credentials_ok())
        return true;

    server->requestAuthentication(BASIC_AUTH, "Micro Journal");
    return false;
}

// only plain files in the root folder, no path tricks
static bool fileserver_valid_name(const String &name)
{
    if (name.length() < 2 || name.length() > 64 || name[0] != '/')
        return false;

    if (name.indexOf("..") >= 0)
        return false;

    for (unsigned int i = 1; i < name.length(); i++)
    {
        char c = name[i];
        if (c < 32 || strchr("/\\:*?\"<>|", c) != nullptr)
            return false;
    }

    // internal working files are not reachable
    if (name.endsWith(FILESERVER_TMP_SUFFIX) || name.endsWith(FILESERVER_BAK_SUFFIX))
        return false;

    return true;
}

//
static void fileserver_send_error(int code, const String &message)
{
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;

    String out;
    serializeJson(doc, out);
    server->send(code, "application/json", out);
}

//
static void fileserver_send_ok()
{
    server->send(200, "application/json", "{\"ok\":true}");
}

// remember what changed so the device can resume correctly
static void fileserver_mark_changed(const String &name)
{
    if (name == Editor::getInstance().fileName)
        editorFileChanged = true;

    if (name == "/config.json" || name == "/wifi.json")
        configChanged = true;
}

// replace target with tmp keeping the original until the new one is in place
// a leftover backup is only cleared when it blocks the rename, since some
// file systems log an error for every remove() of a missing file
static bool fileserver_commit(const String &tmp, const String &target)
{
    String bak = target + FILESERVER_BAK_SUFFIX;

    // a newly uploaded file has no original to keep
    if (!gfs()->exists(target.c_str()))
        return gfs()->rename(tmp.c_str(), target.c_str());

    if (!gfs()->rename(target.c_str(), bak.c_str()))
    {
        // backup left behind by an interrupted save
        gfs()->remove(bak.c_str());
        if (!gfs()->rename(target.c_str(), bak.c_str()))
            return false;
    }

    if (!gfs()->rename(tmp.c_str(), target.c_str()))
    {
        // put the original back
        gfs()->rename(bak.c_str(), target.c_str());
        return false;
    }

    gfs()->remove(bak.c_str());
    return true;
}

//
// HTTP HANDLERS
//

// single page app
static void fileserver_handle_index()
{
    if (!fileserver_authorized())
        return;

    server->sendHeader("Cache-Control", "no-cache");
    server->send_P(200, "text/html; charset=utf-8", FILESERVER_PAGE);
}

// list files in the root folder
static void fileserver_handle_list()
{
    if (!fileserver_authorized())
        return;

    JsonDocument doc;
    doc["version"] = VERSION;
    doc["total"] = (double)gfs()->totalBytes();
    doc["used"] = (double)gfs()->usedBytes();
    doc["active"] = Editor::getInstance().fileName;
    doc["restart"] = configChanged;

    JsonArray files = doc["files"].to<JsonArray>();

    File root = gfs()->open("/", "r");
    if (root && root.isDirectory())
    {
        File entry = root.openNextFile();
        while (entry)
        {
            if (!entry.isDirectory())
            {
                // some cores return the base name, some the full path
                String name = entry.name();
                if (!name.startsWith("/"))
                    name = "/" + name;

                if (fileserver_valid_name(name))
                {
                    JsonObject file = files.add<JsonObject>();
                    file["name"] = name;
                    file["size"] = entry.size();
                }
            }

            entry.close();
            entry = root.openNextFile();
        }
    }
    root.close();

    String out;
    serializeJson(doc, out);
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", out);
}

// read or download a file
static void fileserver_handle_read()
{
    if (!fileserver_authorized())
        return;

    String name = server->arg("name");
    if (!fileserver_valid_name(name))
    {
        fileserver_send_error(400, "Invalid file name");
        return;
    }

    File file = gfs()->open(name.c_str(), "r");
    if (!file || file.isDirectory())
    {
        file.close();
        fileserver_send_error(404, "File not found");
        return;
    }

    // the page sets the file name through the download attribute
    bool download = server->hasArg("download");
    if (download)
        server->sendHeader("Content-Disposition", "attachment");

    server->sendHeader("Cache-Control", "no-store");
    server->streamFile(file, download ? "application/octet-stream" : "text/plain; charset=utf-8");
    file.close();
}

//
static void fileserver_free_buffer()
{
    if (uploadBuffer != nullptr)
        free(uploadBuffer);

    uploadBuffer = nullptr;
    uploadBufferSize = 0;
    uploadBufferUsed = 0;
}

// write the collected upload to the temp file, WiFi is quiet by now
static void fileserver_flush_buffer()
{
    File file = gfs()->open((uploadTarget + FILESERVER_TMP_SUFFIX).c_str(), "w");
    if (!file)
    {
        uploadError = "Unable to create the file";
        return;
    }

    const size_t chunk = 4096;
    for (size_t offset = 0; offset < uploadBufferUsed; offset += chunk)
    {
        size_t size = min(chunk, uploadBufferUsed - offset);
        if (file.write(uploadBuffer + offset, size) != size)
        {
            uploadError = "Write failed. Storage may be full";
            break;
        }

        // let the network tasks keep the connection alive
        delay(1);
    }

    file.close();
}

// receives the raw request body chunk by chunk
// the page posts application/octet-stream, which the web server reads in
// 1.4KB blocks - far quicker than parsing a multipart form byte by byte
static void fileserver_handle_upload()
{
    HTTPRaw &raw = server->raw();

    if (raw.status == RAW_START)
    {
        // leftovers of a transfer that never finished
        if (uploadFile)
        {
            uploadFile.close();
            gfs()->remove((uploadTarget + FILESERVER_TMP_SUFFIX).c_str());
        }

        fileserver_free_buffer();

        uploadError = "";
        uploadTarget = "";
        uploadComplete = false;

        if (!fileserver_credentials_ok())
        {
            uploadError = "Unauthorized";
            return;
        }

        // the web server skips the query string for raw bodies,
        // so the page sends the name in a header as well
        String name = WebServer::urlDecode(server->header(FILESERVER_NAME_HEADER));
        if (name.isEmpty())
            name = server->arg("name");

        if (!fileserver_valid_name(name))
        {
            uploadError = "Invalid file name";
            return;
        }

        // the editor only changes existing files,
        // new files come in through the upload button
        bool upload = server->header(FILESERVER_UPLOAD_HEADER) == "1";
        if (!upload && !gfs()->exists(name.c_str()))
        {
            uploadError = "File not found";
            return;
        }

        uploadTarget = name;

        // the size is known up front, reserve room for the whole body
        int length = server->clientContentLength();
        if (length > 0)
        {
            uploadBuffer = (uint8_t *)heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            uploadBufferSize = uploadBuffer != nullptr ? length : 0;
        }

        if (uploadBuffer == nullptr)
        {
            uploadFile = gfs()->open((name + FILESERVER_TMP_SUFFIX).c_str(), "w");
            if (!uploadFile)
                uploadError = "Unable to create the file";
        }
    }

    else if (raw.status == RAW_WRITE)
    {
        if (uploadBuffer != nullptr)
        {
            if (uploadBufferUsed + raw.currentSize <= uploadBufferSize)
            {
                memcpy(uploadBuffer + uploadBufferUsed, raw.buf, raw.currentSize);
                uploadBufferUsed += raw.currentSize;
            }
            else
            {
                uploadError = "Upload is larger than announced";
            }
        }
        else if (uploadFile && uploadError.isEmpty())
        {
            if (uploadFile.write(raw.buf, raw.currentSize) != raw.currentSize)
                uploadError = "Write failed. Storage may be full";
        }
    }

    else if (raw.status == RAW_END)
    {
        if (uploadFile)
            uploadFile.close();

        // the body is complete, write it to storage
        if (uploadBuffer != nullptr && uploadError.isEmpty() && !uploadTarget.isEmpty())
            fileserver_flush_buffer();

        fileserver_free_buffer();
        uploadComplete = true;
    }

    else if (raw.status == RAW_ABORTED)
    {
        if (uploadFile)
            uploadFile.close();

        // the connection dropped and the web server won't call the save
        // handler, so the partial temp file has to be cleaned up here
        if (!uploadTarget.isEmpty() && uploadBuffer == nullptr)
            gfs()->remove((uploadTarget + FILESERVER_TMP_SUFFIX).c_str());

        fileserver_free_buffer();
        uploadTarget = "";
        uploadError = "";
    }
}

// called when the whole upload is received
static void fileserver_handle_save()
{
    // consume the state of this upload
    String target = uploadTarget;
    String error = uploadError;
    bool complete = uploadComplete;
    uploadTarget = "";
    uploadError = "";
    uploadComplete = false;

    String tmp = target + FILESERVER_TMP_SUFFIX;

    // never commit a body that stopped half way
    if (!target.isEmpty() && !complete && error.isEmpty())
        error = "Upload incomplete";

    //
    if (!fileserver_authorized())
    {
        if (!target.isEmpty())
            gfs()->remove(tmp.c_str());
        return;
    }

    if (target.isEmpty())
    {
        fileserver_send_error(400, error.isEmpty() ? "No file received" : error);
        return;
    }

    if (!error.isEmpty())
    {
        gfs()->remove(tmp.c_str());
        fileserver_send_error(500, error);
        return;
    }

    // a broken config.json would stop the device from booting
    if (target.endsWith(".json"))
    {
        File file = gfs()->open(tmp.c_str(), "r");
        JsonDocument doc;
        DeserializationError jsonError = deserializeJson(doc, file);
        file.close();

        if (jsonError)
        {
            gfs()->remove(tmp.c_str());
            fileserver_send_error(400, format("Invalid JSON: %s", jsonError.c_str()));
            return;
        }
    }

    if (!fileserver_commit(tmp, target))
    {
        gfs()->remove(tmp.c_str());
        fileserver_send_error(500, "Unable to replace the file");
        return;
    }

    fileserver_mark_changed(target);
    _log("[fileserver] Saved %s\n", target.c_str());

    fileserver_send_ok();
}

//
static void fileserver_handle_delete()
{
    if (!fileserver_authorized())
        return;

    String name = server->arg("name");
    if (!fileserver_valid_name(name))
    {
        fileserver_send_error(400, "Invalid file name");
        return;
    }

    if (!gfs()->exists(name.c_str()))
    {
        fileserver_send_error(404, "File not found");
        return;
    }

    if (!gfs()->remove(name.c_str()))
    {
        fileserver_send_error(500, "Unable to delete the file");
        return;
    }

    fileserver_mark_changed(name);
    _log("[fileserver] Deleted %s\n", name.c_str());

    fileserver_send_ok();
}

//
// WIFI
//

// try the networks saved in wifi.json, strongest signal first
static bool fileserver_connect_saved_wifi()
{
    JsonDocument &app = status();

    wifi_config_load();
    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();
    if (savedAccessPoints.isNull() || savedAccessPoints.size() == 0)
        return false;

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(FILESERVER_HOSTNAME);
    delay(1000);

    fileserver_message("Searching for WiFi ...");
    int networksFound = WiFi.scanNetworks();

    for (int i = 0; i < networksFound; i++)
    {
        String ssid = WiFi.SSID(i);

        // the same network can show up once per access point
        bool alreadyTried = false;
        for (int j = 0; j < i; j++)
        {
            if (WiFi.SSID(j) == ssid)
                alreadyTried = true;
        }
        if (alreadyTried || ssid.isEmpty())
            continue;

        for (JsonVariant savedAccessPoint : savedAccessPoints)
        {
            String savedSsid = savedAccessPoint["ssid"] | "";
            String savedPassword = savedAccessPoint["password"] | "";
            if (savedSsid != ssid)
                continue;

            fileserver_message(format("Connecting to %s ...", ssid.c_str()));
            WiFi.begin(savedSsid.c_str(), savedPassword.c_str());

            unsigned long started = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - started < FILESERVER_CONNECT_TIMEOUT)
                delay(100);

            if (WiFi.status() == WL_CONNECTED)
            {
                WiFi.scanDelete();

                app["fileserver"]["url"] = "http://" + WiFi.localIP().toString();
                app["network"]["IP"] = WiFi.localIP().toString();
                app["network"]["ssid"] = ssid;

                return true;
            }

            _log("[fileserver] Failed to connect to %s\n", ssid.c_str());
            WiFi.disconnect();
        }
    }

    WiFi.scanDelete();
    return false;
}

// no known network around, the device becomes the access point
static bool fileserver_start_access_point()
{
    JsonDocument &app = status();

    fileserver_message("Starting hotspot ...");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    delay(500);

    // WPA2 needs at least 8 characters
    String password = fileserver_password();
    if (password.length() < 8)
        password = FILESERVER_AP_PASSWORD;

    if (!WiFi.softAP(FILESERVER_AP_SSID, password.c_str()))
        return false;

    app["fileserver"]["ap_ssid"] = FILESERVER_AP_SSID;
    app["fileserver"]["ap_password"] = password;
    app["fileserver"]["url"] = "http://" + WiFi.softAPIP().toString();

    return true;
}

//
// SESSION
//

static void fileserver_start()
{
    JsonDocument &app = status();

    //
    state = FILESERVER_CONNECTING;
    editorFileChanged = false;
    configChanged = false;
    app["fileserver"].to<JsonObject>();

    // the browser should see the latest text typed on the device
    fileserver_message("Saving current file ...");
    Editor &editor = Editor::getInstance();
    if (!editor.saved)
        editor.saveFile();

    // full CPU speed while WiFi is in use
    setCpuFrequencyMhz(CPU_FREQUENCY_FULL);

    //
    if (!fileserver_connect_saved_wifi() && !fileserver_start_access_point())
    {
        WiFi.mode(WIFI_OFF);
        setCpuFrequencyMhz(CPU_FREQUENCY_LOW);

        state = FILESERVER_ERROR;
        fileserver_message("Unable to start WiFi");
        return;
    }

    // less current while flash is written, see FILESERVER_TX_POWER
    WiFi.setTxPower(FILESERVER_TX_POWER);

    // http://microjournal.local
    // works on most desktops and phones, the IP address is the fallback
    app["fileserver"]["mdns"] = MDNS.begin(FILESERVER_HOSTNAME);
    MDNS.addService("http", "tcp", 80);

    //
    server = new WebServer(80);

    // headers are the only request data available while a raw body streams in
    static const char *collectedHeaders[] = {FILESERVER_NAME_HEADER, FILESERVER_UPLOAD_HEADER};
    server->collectHeaders(collectedHeaders, 2);

    server->on("/", HTTP_GET, fileserver_handle_index);
    server->on("/api/list", HTTP_GET, fileserver_handle_list);
    server->on("/api/file", HTTP_GET, fileserver_handle_read);
    server->on("/api/file", HTTP_POST, fileserver_handle_save, fileserver_handle_upload);
    server->on("/api/file", HTTP_DELETE, fileserver_handle_delete);
    server->on("/favicon.ico", HTTP_GET, []()
               { server->send(204); });
    server->onNotFound([]()
                       { fileserver_send_error(404, "Not found"); });
    server->begin();

    //
    app["fileserver"]["login"] = !fileserver_password().isEmpty();
    state = FILESERVER_RUNNING;
    fileserver_message("Ready");
}

static void fileserver_stop()
{
    JsonDocument &app = status();

    _log("[fileserver_stop] Stopping file server\n");

    //
    if (server != nullptr)
    {
        server->stop();
        delete server;
        server = nullptr;
    }

    // a transfer cut off by the stop
    if (uploadFile)
    {
        uploadFile.close();
        gfs()->remove((uploadTarget + FILESERVER_TMP_SUFFIX).c_str());
    }
    fileserver_free_buffer();
    uploadTarget = "";
    uploadError = "";
    uploadComplete = false;

    // power down the WiFi radio entirely
    MDNS.end();
    if (WiFi.getMode() & WIFI_AP)
        WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // back to the battery saving CPU speed
    setCpuFrequencyMhz(CPU_FREQUENCY_LOW);

    state = FILESERVER_IDLE;

    // settings are read once at boot and the in-memory copy
    // would overwrite the browser's edit on the next config_save
    if (configChanged)
    {
        fileserver_message("Settings changed. Restarting ...");
        delay(2000);
        ESP.restart();
        return;
    }

    // the editor holds a window of the file in memory, refresh it
    if (editorFileChanged)
    {
        Editor &editor = Editor::getInstance();
        editor.loadFile(editor.fileName);
    }

    // next session starts with a clean screen
    app["fileserver"].to<JsonObject>();

    //
    app["screen"] = WORDPROCESSOR;
}
