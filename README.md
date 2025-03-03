# JR-Beru (JR発車ベル)

A Japanese train station departure bell and announcement system hardware replica with a responsive web-based control interface, designed to run on ESP32 microcontrollers and to be installed into the authentic housing(KASUGA BSW215B3). JR-Beru brings the nostalgic sounds of Japanese train stations to your home, offering both physical and Web-based remote control options.

<img src="https://github.com/user-attachments/assets/75df03ee-e178-4b44-9068-cacc9e46f1e3" alt="Demo Img" style="width:85%; height:auto; ">

---

## Live Demo (Un-mute to listen)

https://github.com/user-attachments/assets/d483ad4e-555a-42a0-9bc5-4ab3171fd644

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

<img src="https://github.com/user-attachments/assets/da28639d-7efb-4e0b-ba38-c74ee062fd0b" alt="WEB-UI" style="width:30%; height:auto; ">

### 🔧 System Features

- **WiFi Manager**: Simplified WiFi configuration with AP mode fallback.
- **OTA Updates**: Seamless over-the-air firmware updates.
- **Persist Memory**: Retains settings between power cycles.
- **Dual-core operation**: Audio playback and web interface run on different cores for improved reliability.
- **Memory optimization**: Efficient handling of large JSON configuration files.
- **Serial Output**: Detailed status logs for monitoring and debugging.

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

<img src="https://github.com/user-attachments/assets/32dc37f2-a0c2-441c-9ef2-05b297558cea" alt="Wiring" style="width:40%; height:auto; ">

---

## Audio File Structure

The SD card file system must follow this structure:

```
/01/  # Departure melodies
/02/  # ATOS announcements
/03/  # Door chime sounds
/04/  # Platform announcements
```
 - SD File system can be downloaded here: https://github.com/99buntai/JR-BERU-ESP/releases/download/R0.3.7R2.0.5/SD-files-241225.zip

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
- **Station Editor**: `/station_config_editor.html`

<img src="https://github.com/user-attachments/assets/da28639d-7efb-4e0b-ba38-c74ee062fd0b" alt="WEB-UI" style="width:20%; height:auto; ">

---

## Firmware Updates

1. Access `http://[device-ip]/update`.
2. Enter credentials when prompted:
   - **Default OTA Username**: `JR`
   - **Default OTA Password**: `beru`
3. Select the new firmware file.
4. Click upload to update.

---

## Version History

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
===================
 JR-Beru Booting 
===================
Firmware Version: ESP32-MCU-R0.4.9 WebUI-R3.2.5
Initializing MP3-Player ... (May take 3~5 seconds)
MP3-Player online.
=====Audio File Count:=====
Melody Files: xx
Atos Files: xx
DoorChime Files: xx
VA Files: xx
===========================
===== Loading Station Config Json=====
Config file size: xx bytes
Free heap before reading: xx
Free heap before parsing: xx
Parsing JSON string...
Free heap after parsing: xx
Station Config Json Loaded
Final heap after loading: xx
======================================

=== Loading Device State ===
Stored values:
Random Play: OFF
Volume: 21
Playback Mode: sequence
Restoring station selection:
Line: JY, Station: Shimbashi, Track: 4
Station restored successfully
===========================

PlayingFolder: 1
RandomPlay: 29/72
*wm:AutoConnect 
*wm:Connecting to SAVED AP: AP
*wm:AutoConnect: SUCCESS 
*wm:STA IP Address: 192.168.1.2
WiFi Connected!
IP Address: 192.168.1.2
Web server started!
========Boot up Completed!========
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

---

## Credits

- Station melodies and announcements are the property of JR East.
- Web interface design inspired by JR Yamanote Line station signage.
- Built using the ESP32 Arduino Core and DFRobotDFPlayerMini library.

---

## License

This project is released under the MIT License. See the `LICENSE` file for details.

---

## Support

For issues and feature requests, please use the [GitHub Issues](https://github.com/99buntai/JR-BERU-ESP/issues) page.
