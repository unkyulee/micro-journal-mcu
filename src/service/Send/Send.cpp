#include "Send.h"
#include "app/app.h"

//
#include "service/Editor/Editor.h"

//
SendFunc send_func = nullptr;;
void send_register(SendFunc f) {
    send_func = f;
}

// progress of the current send
static volatile size_t send_total = 0;
static volatile size_t send_sent = 0;

int send_progress()
{
    if (send_total == 0)
        return 0;
    return (int)(send_sent * 100 / send_total);
}

void send_progress_reset()
{
    send_total = 0;
    send_sent = 0;
}

// Monitor if there are any send request
void send_loop()
{
    static unsigned int last = 0;
    if (millis() - last > 1000)
    {
        last = millis();

        //
        JsonDocument &app = status();
        if (appState().takeTask(TASK_SEND_START))
        {
            //
            _log("[send_loop] Send Start Request\n");

            appState().sendFinished = false;

            //
            send_start();
        }
    }
}

//
void send_start()
{
    //
    JsonDocument &app = status();

    // Loading File
    int file_index = app["config"]["file_index"].as<int>();
    Editor::getInstance().loadFile(format("/%d.txt", file_index));

    // Get File reference
    File file = gfs()->open(Editor::getInstance().fileName.c_str(), "r");
    if (!file || !file.available())
    {
        _log("Send_render: Failed to open file: %s\n", Editor::getInstance().fileName.c_str());
        appState().sendFinished = true;
        return;
    }

    //
    _log("Sending file content over keyboard...\n");
    send_sent = 0;
    send_total = file.size();

    // Read file content in small chunks
    while (file.available() && appState().sendStop.load() == false)
    {
        //
        int c = file.read();

        // use the send function pointer to send
        if (send_func != nullptr)
            send_func(c);
        send_sent = send_sent + 1;

        // Small delay to prevent buffer overflow
        delay(30); // You can tweak this as needed
        yield();
    }
    //
    file.close();

    //
    appState().sendFinished = true;
}