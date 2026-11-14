// Things you might want to change.
#pragma once

// Which way is up. 270 matches the Waveshare demo / ActuallyOnTimeDisplay orientation.
// If the eyes look sideways or upside down on your mount, try 0, 90 or 180.
#define SCREEN_ROTATION 270

// 0..255. The AMOLED is very bright; the eyes are mostly black so this barely affects battery.
#define SCREEN_BRIGHTNESS 200

// Bluetooth name prefix the web app looks for. Each board adds its own ID: "EyeBuddy-3F2A".
#define BLE_NAME "EyeBuddy"

// Firmware release number (the web app offers an update when its copy is newer).
#define FW_VERSION 8

#define TARGET_FPS 40
