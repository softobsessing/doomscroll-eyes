// Things you might want to change.
#pragma once

// Which way is up. 270 matches the Waveshare demo / ActuallyOnTimeDisplay orientation.
// If the eyes look sideways or upside down on your mount, try 0, 90 or 180.
#define SCREEN_ROTATION 270

// 0..255. The AMOLED is very bright; the eyes are mostly black so this barely affects battery.
#define SCREEN_BRIGHTNESS 200

// Bluetooth name the web app looks for. Keep in sync with eyebuddy-web if you change it.
#define BLE_NAME "EyeBuddy"

#define TARGET_FPS 40
