
# Micro Journal MCU Firmware

[Micro Journal](https://github.com/unkyulee/micro-journal) is an open-source DIY project for portable, distraction-free writing devices. It combines the focused experience of a typewriter with digital storage, with several hardware revisions exploring different displays, keyboards, and designs.

This repository contains the firmware source code for ESP32-based Micro Journal builds. It uses the Arduino framework and PlatformIO. Hardware designs, wiring, and device build guides are available in the [main Micro Journal repository](https://github.com/unkyulee/micro-journal).

## What you need

- A Windows, macOS, or Linux computer with an internet connection.
- The ESP32 development board specified by your Micro Journal build. The current ESP32 environments target ESP32-S3 hardware; check your build guide for the required flash, PSRAM, display, and wiring.
- A USB cable that supports data transfer.
- The matching display and keyboard hardware to use the device after uploading.

## Build and upload

### 1. Install Visual Studio Code

Download [Visual Studio Code](https://code.visualstudio.com/download) for your operating system, install it, and open it.

Also install [Git](https://git-scm.com/downloads). PlatformIO uses Git to download some of this project's dependencies, even if you download the source as a ZIP. Restart VS Code after installing Git.

### 2. Install PlatformIO

1. Open the **Extensions** view in the VS Code sidebar.
2. Search for **PlatformIO IDE** and install the extension published by PlatformIO.
3. Wait for its initial setup to finish, then reload VS Code if prompted.

PlatformIO installs the build tools and manages the libraries needed by this project. You do not need to install the Arduino IDE or PlatformIO CLI separately. See the [PlatformIO installation guide](https://docs.platformio.org/en/latest/integration/ide/vscode.html#installation) for more details.

### 3. Download and open the firmware project

1. Go to [micro-journal-mcu](https://github.com/unkyulee/micro-journal-mcu).
2. Select **Code > Download ZIP** and extract the archive.
3. In VS Code, select **File > Open Folder** and open the extracted folder containing `platformio.ini`.
4. Allow PlatformIO to initialize the project.

Alternatively, clone the repository with Git:

```sh
git clone https://github.com/unkyulee/micro-journal-mcu.git
```

### 4. Choose the environment for your hardware

Open the **PlatformIO** icon in the VS Code sidebar, then find **Project Tasks**. Expand the environment that matches your device:

| Environment | Micro Journal hardware |
| --- | --- |
| `rev_5_ili9341` | Rev.5 with an ILI9341 display and USB keyboard |
| `rev_5_st7789` | Rev.5 with an ST7789 display and USB keyboard |
| `rev_6_ili9341` | Rev.6 with an ILI9341 display and 48-key keypad |
| `rev_6_st7789` | Rev.6 with an ST7789 display and 48-key keypad |
| `rev_7` | Rev.7 with a LILYGO T5-ePaper-S3 board and e-ink display |
| `rev_8_type1` | Rev.8 with a type 1 reflective LCD and 68-key keypad |
| `rev_8_type2` | Rev.8 with a type 2 reflective LCD and 68-key keypad |

For Rev.8, use your hardware build guide to identify the display type. `rev_8` is the shared base configuration for the two display variants.

The project also includes `rev_4_68`, which targets an RP2040 board rather than an ESP32.

Use the tasks under your chosen environment explicitly. The `default_envs = rev_6` entry currently appears under `[common]`, and there is no environment named `rev_6`, so it is not a usable default selection.

### 5. Build the firmware

Under your chosen environment in **Project Tasks**, select **General > Build**.

The first build downloads the platform, compiler, and libraries, so it may take several minutes. Wait for the terminal to show **SUCCESS** before continuing.

### 6. Connect the ESP32 board

1. Connect the board to your computer using the USB data cable.
2. If the board has separate USB connectors, use the programming connector specified in its documentation. On a build that uses native USB for a keyboard, this may be the USB-to-UART connector.
3. Check that the computer detects a serial port. On Windows, look under **Ports (COM & LPT)** in Device Manager. On macOS or Linux, ports commonly appear as `/dev/cu.*`, `/dev/ttyUSB*`, or `/dev/ttyACM*`.

If no port appears, try another data cable or USB port. If your board uses a USB-to-serial chip, install the appropriate driver from the board manufacturer's documentation.

### 7. Upload the firmware

1. Close any serial monitor or other application using the board's port.
2. Under the same environment used to build, select **General > Upload**.
3. Wait for the upload to finish with **SUCCESS**.
4. The board should reset and start the firmware. If it does not, press **RESET** or **EN**.

PlatformIO normally detects the upload port automatically. If it selects the wrong port, add `upload_port` inside your selected environment's section in `platformio.ini`, using your actual port:

```ini
upload_port = COM5
```

The project currently excludes `COM17` during its custom upload-port selection. If your board appears on `COM17`, set `upload_port = COM17` explicitly or adjust `custom_upload_port_exclude` in `[env]`.

### 8. Check the device

With the matching display and keyboard connected according to your build guide, check that Micro Journal starts and responds to input.

For diagnostics, open **General > Monitor** under your environment. The ESP32 environments use **115200 baud**. Close the monitor before uploading again.

## Troubleshooting

- **Upload stays at “Connecting…”:** On boards with BOOT and RESET/EN buttons, hold **BOOT**, press and release **RESET/EN**, then release **BOOT** and retry uploading. The serial port may change when entering download mode, so check it again. See [Espressif's troubleshooting guide](https://docs.espressif.com/projects/esptool/en/latest/esp32/troubleshooting.html).
- **Port is busy or access is denied:** Close other serial applications. On Linux, check the [PlatformIO USB permission setup](https://docs.platformio.org/en/latest/core/installation/udev-rules.html).
- **Upload fails partway through:** Try another cable and set `upload_speed = 115200` in your selected environment before retrying.
- **Firmware uploads but the display is blank or incorrect:** Confirm that the selected environment matches the display controller, board memory, and wiring in your hardware build guide.
- **Dependency downloads fail:** Check your internet connection and confirm that `git --version` works in a terminal, then retry the build.

## Optional: build from the terminal

Open the **PlatformIO Core CLI** terminal from PlatformIO's sidebar. For example, to build and upload a Rev.6 with an ILI9341 display:

```sh
pio run -e rev_6_ili9341
pio device list
pio run -e rev_6_ili9341 -t upload --upload-port COM5
pio device monitor --port COM5 --baud 115200
```

Replace `rev_6_ili9341` and `COM5` with your environment and port. The [PlatformIO device list command](https://docs.platformio.org/en/latest/core/userguide/device/cmd_list.html) lists available serial ports.
