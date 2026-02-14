# DoseRight UI Code (ESP32-S3-BOX)

## Stack used

- ESP-IDF 5.5.x (tested with 5.5.2)
- LVGL + esp_lvgl_port
- NVS for WiFi credential storage
- ESP-IDF WiFi + HTTP client

## Required code updates before build

Set these values in [main/main.c](main/main.c#L44-L46):

- `BACKEND_BASE_URL`: base URL for your backend API (example: `http://192.168.1.10:8080`)
- `DEVICE_ID`: unique device identifier
- `DEVICE_SECRET`: shared secret used for authorization headers

These are intentionally blank in git. Keep real values local.

Set these values in [main/ui/custom/profile_screen.c](main/ui/custom/profile_screen.c#L71-L73):

- `PROFILE_BASE_URL`: base URL for the profile API (example: `http://192.168.1.10:8080`)
- `PROFILE_DEVICE_ID`: device identifier used for profile requests
- `PROFILE_SECRET`: shared secret used for profile authorization headers

These are intentionally blank in git. Keep real values local.

## What you can customize

- **UI screens and widgets**: update or add screens under [main/ui/custom/](main/ui/custom/)
- **Theme and fonts**: adjust styles in [main/ui/](main/ui/)
- **Audio assets**: replace or add files in [audios/](audios/)
- **Backend routes**: change request paths in [main/main.c](main/main.c)
- **Intervals and demo values**: tweak refresh timings and demo readings in [main/main.c](main/main.c)

## Project layout

- App entry and logic: [main/main.c](main/main.c)
- LVGL UI sources: [main/ui/](main/ui/)
- Custom screens: [main/ui/custom/](main/ui/custom/)
- Audio assets: [audios/](audios/)
- ESP-IDF component manifest: [main/idf_component.yml](main/idf_component.yml)
- Root build config: [CMakeLists.txt](CMakeLists.txt)

## Local setup (Windows example)

1. Clone the esp-box repo from GitHub.
2. Copy this `hardware` folder into `D:\esp-box\examples\` so the path is:

	```
	D:\esp-box\examples\hardware
	```

3. Open a terminal in `D:\esp-box\examples\hardware`.
4. Ensure the ESP-IDF environment is set up for your shell (per your ESP-IDF install).

## Build and flash

1. Open a IDF in the project root.
2. Set the target (first time only):

	```bash
	idf.py set-target esp32s3
	```

3. Build, flash, and monitor:

	```bash
	idf.py fullclean
	```

4. Build:

	```bash
	idf.py build
	```

5. Flash and monitor:

	```bash
	idf.py flash monitor
	```

6. Exit the serial monitor with `Ctrl-]`.

Tip: after a full flash, you can reduce flash time with:

```bash
idf.py app-flash monitor
```

## Backend expectations

The firmware calls backend endpoints assembled from `BACKEND_BASE_URL` and `TIME_API_PATH` in [main/main.c](main/main.c). Typical flows include:

- Heartbeat submissions
- Dose event submissions
- Upcoming dose fetch
- Device time sync

If the backend is down or WiFi is disconnected, network actions are skipped and the UI shows connectivity status.

## WiFi behavior

The WiFi UI scans for networks and lets you select a network. Credentials are stored in NVS and are not committed to this repo.

## Application flow

### Boot sequence

1. **Power on**: Device initializes hardware (display, touch, buttons, NVS, WiFi)
2. **Boot screen**: Shows progress bar with status messages
3. **QR code screen**: Displays device ID as QR code for pairing/setup
4. **Home screen**: Main UI with time, WiFi status, and next dose info

### Main loop

The firmware runs several concurrent tasks:

- **UI updates**: LVGL refreshes the display at ~30 FPS
- **Time sync**: Fetches server time on first connect, then uses local clock with periodic re-sync
- **WiFi monitor**: Auto-reconnect if connection drops
- **Backend heartbeat**: Sends device status every 60 seconds
- **Dose fetch**: Polls for upcoming doses every 60 seconds
- **Button handling**: Responds to physical button presses (if present on hardware)

### User interactions

- **WiFi icon tap**: Opens WiFi list screen to scan and connect to networks
- **Swipe left/right**: Navigate between home, menu, and other screens
- **Menu selections**: Access settings, help, profile, refill, calibration, and alerts
- **Dose actions**: Tap "Take Now" or "Skip" on upcoming dose cards
- **Refill mode**: Manual slot control for testing stepper motor positions

### Backend communication

The device makes HTTP requests to the backend:

1. **Time sync** (`GET /api/hardware/time`): Fetches epoch time and offset
2. **Heartbeat** (`POST /api/hardware/heartbeat`): Sends device status (battery, WiFi strength, temp)
3. **Upcoming doses** (`GET /api/hardware/upcoming?deviceId=...`): Fetches next scheduled medications
4. **Dose event** (`POST /api/hardware/doses/{doseId}/{action}`): Reports taken/skipped doses
5. **Info fetch** (`GET /api/hardware/{path}?deviceId=...`): Generic endpoint for dynamic content

All requests include `Authorization: Bearer {DEVICE_SECRET}` header.

### Some Error handling

- **No WiFi**: UI shows disconnected status, network calls are skipped
- **Backend timeout**: Falls back to cached data or shows "Fetch failed" message
- **Invalid credentials**: WiFi connection fails, user returns to WiFi list screen
