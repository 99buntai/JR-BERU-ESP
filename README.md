# JR-Beru (JR発車ベル)

A Japanese train station departure bell and announcement system hardware replica with a responsive web-based control interface, designed to run on ESP32 microcontrollers and to be installed into the authentic housing(KASUGA BSW215B3). JR-Beru brings the nostalgic sounds of Japanese train stations to your home, offering both physical and Web-based remote control options.

<img src="https://github.com/user-attachments/assets/53154c43-97f1-4790-b953-0847c9b9c01a" alt="Demo Img" style="width:85%; height:auto; ">

---

## Live Demo (Un-mute to listen)
https://github.com/user-attachments/assets/cd463d8b-be39-4bcc-a3f9-045f05dc00bd


---

## Features

### 🚂 Audio Playback

- **Departure Melodies**: JR station departure melodies
- **ATOS Announcements**: Yamanote Line ATOS announcements.(Current file system only contains ATOS for the Yamanote Line)
- **Door Chimes**: Door opening and closing sounds.
- **Platform Announcements**: JR station platform announcements.(Current file system only contains announcements for the Yamanote Line)
- New audio files can be added to the SD Card. device will re-index on reboot.

### 🎧 Control Methods

- **Physical Button Control**:
  - Single press: Start melody loop(will random play by default, random play can be toggled in the web interface).
  - Release: Play ATOS announcement followed by door chime.
- **Web Interface Control**:
  - Custom station config file upload (with validation).
  - Dynamic station sign display with preconfigured audio selections.
  - Station name, line code, direction indicator dynamically updated from the config file.
  - Audio selection for all sound types, manual selection/auto populate from the config file.
  - Volume control (range: 0-30).
  - Play mode selection (selected, random, sequence).
  - Manual platform announcement trigger.

### 📱 Web Interface

- **Responsive Design**: 
  - Fully functional on both desktop and mobile devices.

- **Station Config Upload**:
  - Custom station config file upload with size limit (max 100KB).
  - Error handling/validation/recovery for invalid config files.
  - JSON structure validation including checks for required fields.
  - Station info dynamically updated from the config file.
  - Audio selection for all sound types configured in the config file.  

- **Station Sign Display**:
  - JR station sign display with dynamic station name, line code, direction indicator.
  - Station name, line code, direction indicator dynamically updated from the config file.
  - Multilingual station names (Japanese, Korean, English).
  - Line Marker displaying (e.g., JY03)(Created with only html/css code, no online resources required).

- **Station Config Editor**:
  - Web-based editor for creating and modifying station configuration files.
  - Visual preview of station signs based on your configuration.
  - Export optimized JSON files for use with JR-Beru.

- **Real-time Controls**: Instant feedback and response to user inputs.

- **Enhanced Error Handling**:
  - Consistent modal dialogs for all errors and notifications.
  - Clear visual feedback for success/error states with color-coding.
  - Improved display of backend JSON parsing errors.

<img src="https://github.com/user-attachments/assets/762f0230-c717-4456-a0a9-21966e94b34c" alt="WEB-UI" style="width:100%; height:auto; ">

### 🔧 System Features

- **WiFi Manager**: Simplified WiFi configuration with AP mode fallback.
- **OTA Updates**: Seamless over-the-air firmware updates.
- **Persist Memory**: Retains settings between power cycles.
- **Dual-core operation**: Audio playback and web interface run on different cores for improved reliability.
- **Memory optimization**: Efficient handling of large JSON configuration files.
- **Serial Output**: Detailed status logs for monitoring and debugging.
- **Serial Shell**: Interactive command-line interface for device control and diagnostics.
- **Improved Stability**: Enhanced stack sizes for critical tasks to prevent overflow errors.

---

## Hardware Requirements

- **ESP32 Board**: Compatible with ESP32 dev board.
- **DFPlayer Mini MP3 Player**
- **SD Card**: Formatted as FAT32 with audio files.
- **Speaker**: Suitable for MP3 playback.
- **Push Button**: [KASUGA BSW215B3.](https://www.amazon.co.jp/-/en/Electric-BSW215B3AB-Switcher-Rainproof-BSW/dp/B07KQ5P8YG)
- **Power Supply**: 5V regulated power source.

---

## Pin Configuration/Wiring

| Pin    | Function          |
| ---    | ----------------- |
| GPIO16 | DFPlayer RX       |
| GPIO17 | DFPlayer TX       |
| GPIO4  | Main Button Input |

<img src="https://github.com/user-attachments/assets/e2ea4942-b499-4de9-a3f0-4eed299d5568" alt="Wiring" style="width:40%; height:auto; ">

---

## Audio File Structure

The SD card file system must follow this structure:

```
/01/  # Departure melodies
/02/  # ATOS announcements
/03/  # Door chime sounds
/04/  # Platform announcements
```
 - SD File system can be downloaded here: https://github.com/99buntai/JR-BERU-ESP/releases/download/R0.4.16-R3.2.8/SD-files-250405.zip

---

## Setup Instructions

1. Format the SD card as FAT32.
2. Copy audio files to the appropriate folders.
3. Connect hardware according to the [Pin Configuration](#pin-configuration).
4. Flash firmware to the ESP32 using the Arduino IDE.
5. Power on the device.
6. Connect to the "JR-BERU-AP" WiFi network.
7. Configure your WiFi settings via the web interface.
8. Access the web interface at the device's assigned IP address.

---

## Configuration Files

### Station Configuration

The station configuration file is a JSON file that defines lines, stations, and tracks for the system. The structure includes:

- **lines**: Available JRlines (JY, JR, etc.)
  - **style**: Visual styling for the station sign elements (colors)
  - **stations**: List of stations on the selected line
    - **stationInfo**: Details about the station (names, codes)
    - **tracks**: Different track configurations 
      - **lineMarker**: Line and station number
      - **audio**: Audio file mappings
      - **direction**: Previous and next stations

You can use the included station config editor to create or modify configuration files.

---

## Hardware installation 

- Housing installation (Cut away all the original contactor and glue in a momentary switch.)
<img width="347" alt="Screenshot 2024-12-25 at 16 15 23" src="https://github.com/user-attachments/assets/b7926b30-5ec6-492a-8f21-e18ed6cb1226" />

- Make a Type C port to ESP32 board.
<img width="218" alt="Screenshot 2024-12-25 at 16 15 10" src="https://github.com/user-attachments/assets/42d0ba40-b03e-414c-a4b5-ffe235fed302" />

- Final installation 
<img width="349" alt="Screenshot 2024-12-25 at 16 14 58" src="https://github.com/user-attachments/assets/9a75d714-3629-457a-b01a-fbb4c9df8dc0" />

---

## Web Interface Access

- **URL**: `http://[device-ip]`

<img src="https://github.com/user-attachments/assets/fd04da57-b5ee-4061-a439-a20b33faee46" alt="WEB-UI" style="width:20%; height:auto; ">

---

## Station Config Web Editor

The Station Config Web Editor is a useful tool that allows you to create and customize JR BERU configurations without manually editing JSON files. This editor features a user-friendly interface for configuring all aspects of your virtual train station.

### Accessing the Editor

1. Open `station_config_editor.html` in your web browser
2. The editor works on both desktop and mobile devices

### Key Features

- **Visual Station Sign Preview**: See real-time updates as you modify your station configuration
- **Line Configuration**: Create and customize multiple JR lines with appropriate styling
- **Station Management**: Add, modify and remove stations with full multilingual support
- **Track Configuration**: Set up multiple tracks with custom audio mappings
- **Direction Settings**: Configure previous and next station information for accurate announcements
- **Color Customization**: Adjust colors for line markers, station numbers, and direction indicators
- **Audio Mapping**: Easily assign audio files to each station configuration
- **JSON Validation**: Automatic validation ensures your configuration will work with JR-Beru
- **Export/Import**: Save your configurations as JSON files for backup or sharing

### How to Use

1. **Create a Line**: Start by creating a JR line (like JY for Yamanote) and set its styling
2. **Add Stations**: Create stations with Japanese, English, Korean and Hiragana names
3. **Configure Tracks**: Set up track numbers, line codes, and audio mappings
4. **Set Directions**: Define previous and next stations for directional announcements
5. **Preview**: Use the visual preview to check your station sign appearance
6. **Export**: Download your configuration as a JSON file so you can upload it in JR-Beru web interface

### Tips for Effective Configuration

- Use accurate station codes and numbers based on the real JR system, id you leave station code blank, the station will not be displayed.
- Match color schemes to authentic JR line colors for a realistic experience
- Configure all language fields for proper multilingual support
- Test your configuration thoroughly before final upload
- Keep backup copies of your station configurations

<img src="https://github.com/user-attachments/assets/3108e517-734d-4235-b228-a7ea219650a7" alt="Station Editor" style="width:50%; height:auto; ">

---

## Uploading Station Config via Web UI

The JR-Beru web interface allows you to easily upload and apply custom station configuration files created with the Station Config Web Editor.

### Upload Process

1. **Access the Web Interface**: Navigate to `http://[device-ip]` in your browser
2. **Locate the Upload Section**: Find the "Upload" button in the sequence control section
3. **Select File**: Click "Choose File" and select your JSON configuration file
4. **Upload**: Click the "Upload" button to send the file to your JR-Beru device

### Validation and Processing

- The system automatically validates your configuration file before applying it
- Files are limited to 100KB maximum size
- JSON structure is checked for required fields and proper formatting
- If validation fails, you'll receive a specific error message explaining the issue

### After Upload

After a successful upload:
- The station sign will immediately update to show your new configuration
- Audio selections will be updated based on the configuration
- The device will save your new configuration and keep it across reboots
- You can immediately use the new configuration with the physical button or web controls

### Troubleshooting Upload Issues

- **File Size Error**: If you see "File too large", reduce the number of lines in your configuration
- **JSON Format Error**: Check your file for syntax errors (missing brackets, commas, etc.)
- **Missing Fields**: Ensure all required fields are present in your configuration
- **Audio File Issues**: If audio doesn't play, check that the file indices in your config match the available files on the SD card

---

## Firmware Updates

1. Access `http://[device-ip]/update`.
2. Enter credentials when prompted:
   - **Default OTA Username**: `JR`
   - **Default OTA Password**: `BERU`
3. Select the new firmware file.
4. Click upload to update.

---

## Version History

### **ESP32-MCU-R0.4.16 WebUI-R3.2.8**

- Enhanced error handling with consistent modal dialogs for all errors
- Optimized JavaScript code for better performance and reduced size
- Improved UI components with consistent styling and better feedback
- Increased button handler task stack size for improved stability
- Added better code organization with descriptive comments
- Fixed JSON error parsing and display from backend responses

### **ESP32-MCU-R0.4.15 WebUI-R3.2.7**

- Improved error handling and user feedback
- Enhanced stability for button handling
- Fixed minor UI issues
- Updated confirm dialogs to use the modal system

### **ESP32-MCU-R0.4.9 WebUI-R3.2.5**

- Migrated to ESP32 platform for improved performance and reliability
- Added station configuration editor
- Improved configuration file validation
- Added file size limit (100KB) for uploads
- Enhanced memory management for large configuration files
- Improved station sign display

### **MCU-R0.3.7 WebUI-R2.0.5**

- More accurate station sign display.
- Improved audio control reliability.
- Added Reset DF-Player support in WebUI.

### **MCU-R0.3.5 WebUI-R2.0.2**

- Enhanced station sign display.
- Improved audio control reliability.
- Added multilingual support.
- WiFi connection stability improvements.

---

## Serial Monitoring

The device outputs detailed status information via Serial (115200 baud rate). Use the Arduino IDE's Serial Monitor or any serial terminal to monitor real-time logs.

### Boot Sequence Output

```
=================================
|        JR-Beru Booting        |
=================================
Firmware Version: ESP32-MCU-Rx.x.x WebUI-Rx.x.x

Initializing MP3-Player ... (May take 3~5 seconds)
MP3-Player online.

=== Indexing Audio File ===
Melody Files: x
Atos Files: x
DoorChime Files: x
VA Files: x
===========================


===== Loading Station Config Json=====
Config file size: xx bytes
Free heap before reading: xx
Free heap after parsing: xx
Station Config Json Loaded
Final heap after loading: xx
======================================

Station config loaded successfully

=== Loading Device State ===

Stored values:
Random Play: OFF
Volume: xx
Current Melody: xx, Current Atos: xx, Current DoorChime: xx, Current VA: xx
Playback Mode: sequence

Restoring station selection:
Line: xx, Station: xx, Track: xx
Station restored successfully
============================

*wm:AutoConnect 
*wm:Connecting to SAVED AP: xx
*wm:AutoConnect: SUCCESS 
*wm:STA IP Address: xx
WiFi Connected!
IP Address: xx
Web server started!
PlayingFolder: xx
RandomPlay: xx/xx

========Boot up Completed!========


======== System Health ========
Memory    : xx% used (xx KB free)
Flash     : xx% used (xx KB free)
SPIFFS    : xx% used (xx KB free)
CPU Temp  : xx°C
===============================


Welcome to JR-Beru Shell. Type 'help' for available commands.
Firmware Version: ESP32-MCU-Rx.x.x WebUI-Rx.x.x

JR-Beru:_$ 

```

### Audio Playback Status

- Track selection: `PlayingFolder: X, Playing Audio: Y`
- Random play: `RandomPlay: X/Y`
- Playback completion: `Number:X Play Finished!`
- Door chime triggers: `====Door Chime playing====`

### File Operations

- Configuration validation: `File validation successful`
- Memory usage: `Free heap during parsing: X`
- File upload progress: `Upload progress: X bytes written`

### Error Messages

- `Time Out!`: MP3 player communication timeout.
- `Stack Wrong!`: MP3 player stack error.
- `Card not found`: SD card reading error.
- `Cannot Find File`: Requested audio file not found.
- `File Index Out of Bound`: Invalid file number requested.
- `File too large, max size is 100KB`: Attempted upload exceeds size limit.
- `Invalid JSON format (unbalanced braces)`: Configuration file error.

### WiFi Status

- Connection status.
- IP address assignment.
- Web server initialization.

### Debugging Tips

1. Open Serial Monitor in Arduino IDE.
2. Set baud rate to 115200.
3. Ensure "Newline" is selected for line ending.
4. Power cycle the device to see boot sequence.
5. Monitor real-time operation status.

## Serial Shell

The device includes an interactive command-line interface accessible through the serial monitor at 115200 baud rate. This shell provides direct control and diagnostic capabilities without requiring the web interface.

### Available Commands

| Command | Description |
| --- | --- |
| `health` | Display system health information (memory usage, SPIFFS usage, etc.) |
| `status` | Show current station and playback status |
| `volume [0-30]` | Get or set the volume level |
| `play melody` | Play current melody |
| `play atos` | Play current ATOS announcement |
| `play chime` | Play current door chime |
| `play va` | Play current platform announcement |
| `ls`  `/` | List files on SPIFFS |
| `ls stations`| List available stations with their tracks |
| `station [line] [station] [track]` | Set current station (e.g., `station JY Tokyo 4`) |
| `reset player` | Reset the DFPlayer Mini |
| `reset wifi` | Reset WiFi settings |
| `reboot` | Restart the ESP32 |
| `help` | Display list of available commands |

### Using the Serial Shell

1. Connect to the device using a serial terminal (Arduino IDE Serial Monitor or other terminal program)
2. Set the baud rate to 115200
3. After the device boots, you'll see the `JR-Beru_:` prompt
4. Type a command and press Enter to execute
5. For commands with parameters, separate them with spaces (e.g., `volume 20`)

Example session:
```
JR-Beru_: help
============= JR-Beru Shell Commands =============
help               - Display help
health             - Display system health information
status             - Display current station and playback status
volume             - Get or set volume level [0-30]
play               - Play current melody/atos/chime/va
index              - Reindex the file Count of the DFPlayer
ls                 - List files on SPIFFS or available stations
station            - Set current station [line] [station] [track]
reset              - Reset the WiFi or DFPlayer
reboot             - Restart ESP32
===================================================
JR-Beru_: status

======== Current Status ========
Current Line: JY
Current Station: 東京 (Tokyo)
Current Track: 4
Volume: 22/30
WiFi Status: Connected
IP Address: 192.168.1.2
===============================
JR-Beru_: 
```

## Station Config JSON File Structure

- Structure Details:
  - lines: Available JR lines (JY, JK, etc.)
  - style: Visual styling for the station sign elements (colors)
  - lineMarkerBgColor: Background color for the line marker
  - lineNumberBgColor: Background color for the station number display
  - directionBarBgColor: Background color for the direction indicator bars
  - stations: List of stations on the selected line
    - i: Station information array: [stationCode, nameJa, nameHiragana, nameEn, wardBox]
      - Element 0: Station code (e.g., "AKB")
      - Element 1: Japanese station name (e.g., "秋葉原")
      - Element 2: Hiragana reading (e.g., "あきはばら")
      - Element 3: English station name (e.g., "Akihabara")
      - Element 4: Ward designation (e.g., "山")
    - t: Array of track configurations, each containing:
      - Element 0: Track name (e.g., "Track1")
      - Element 1: Line code (e.g., "JY")
      - Element 2: Station number (e.g., "03")
      - Element 3: Audio file indices [melody, atos, doorchime, platform]
      - Element 4: Previous station info [nameJa, nameEn]
      - Element 5: Next station info [nameJa, nameEn]

- Example JSON file:
``` 
{
  "lines": {
    "JY": {
      "style": {
        "lineMarkerBgColor": "#000000",
        "lineNumberBgColor": "#80c241",
        "directionBarBgColor": "#006400"
      },
      "stations": {
        "Default Config": {
          "i": ["AKB", "秋葉原", "あきはばら", "Akihabara", "山"],
          "t": [
            [
              "Track1", 
              "JY", 
              "03", 
              [1, 1, 1, 1], 
              ["前の駅", "Previous Station"], 
              ["次の駅", "Next Station"]
            ]
          ]
        }
      }
    }
  }
}
```

---

## Credits

- Station melodies and announcements are the property of JR East.
- Web interface design inspired by JR Yamanote Line station signage.
- Built using the ESP32 Arduino Core and DFRobotDFPlayerMini library.

---

## License

This project is released under the Creative Commons Attribution-NonCommercial 4.0 International License (CC BY-NC 4.0).

This means you are free to:
- Share — copy and redistribute the material in any medium or format
- Adapt — remix, transform, and build upon the material

Under the following terms:
- Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made.
- NonCommercial — You may not use the material for commercial purposes.

For more details: [Creative Commons BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/)

---

## Support

For issues and feature requests, please use the [GitHub Issues](https://github.com/99buntai/JR-BERU-ESP/issues) page.
