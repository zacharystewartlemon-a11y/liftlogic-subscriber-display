# LiftLogic Subscriber Display

A standalone Wi-Fi YouTube subscriber display for inexpensive ESP32 touchscreen boards.

## Hardware targets

- **Current / v0.1:** ESP32-2432S028R ("Cheap Yellow Display"), 2.8-inch 320x240 ILI9341 resistive-touch board.
- **Planned:** ESP32-4848S040, 4-inch 480x480 ESP32-S3 board.

## Project goals

- Runs independently over Wi-Fi; no computer required after setup.
- Live YouTube subscriber updates.
- Browser-based first install.
- First-boot Wi-Fi configuration portal.
- LiftLogic branding and customizable appearance.
- Touch brightness control.
- Automatic bedtime dim/off schedule.
- OTA firmware updates.
- Shared application code across the 2.8-inch and 4-inch versions.

## Current milestone: hardware test v0.1

The first firmware intentionally does only a few things:

1. Initializes the 2.8-inch CYD display.
2. Controls the backlight over GPIO 21.
3. Opens a `LiftLogic-Setup` Wi-Fi captive portal when no Wi-Fi is saved.
4. Displays the board's local IP address after connecting.
5. Hosts a tiny local page for changing brightness.

This lets us validate the physical board before adding the YouTube API, touch UI, branding system, scheduler, and OTA updates.

## Building locally

This project uses PlatformIO:

```bash
pio run -e cyd
```

GitHub Actions also builds the firmware automatically and publishes the browser installer through GitHub Pages.
