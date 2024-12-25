# JR-Beru (JR発車ベル)

A Japanese train station departure bell and announcement system hardware replica with a responsive web-based control interface, designed to run on ESP8266 microcontrollers and to be installed into the authentic housing. JR-Beru brings the nostalgic sounds of Japanese train stations to your home, offering both physical and remote control options.

![Edited_Demo_Image](https://github.com/user-attachments/assets/e69d89e7-2854-455d-899f-9492c6638674)

<img src="https://github.com/user-attachments/assets/e69d89e7-2854-455d-899f-9492c6638674" alt="Alt Text" style="width:50%; height:auto;">

---

## Features

### 🚂 Audio Playback

- **Departure Melodies**: JR station departure melodies
- **ATOS Announcements**: Yamanote Line ATOS announcements.
- **Door Chimes**: Door opening and closing sounds.
- **Platform Announcements**: JR station platform announcements.

### 🎧 Control Methods

- **Physical Button Control**:
  - Single press: Start melody loop.
  - Release: Play ATOS announcement followed by door chime.
- **Web Interface Control**:
  - Audio selection for all sound types.
  - Volume control (range: 0-30).
  - Random play toggle.
  - Manual platform announcement trigger.

### 📱 Web Interface

- **Responsive Design**: Fully functional on both desktop and mobile devices.
- **Station Sign Display**:
  - JR station signage design (Static for now, can be changed to dynamic with further development).
  - Multilingual station names (Japanese, Korean, English).
  - Line Marker displaying (e.g., JY03).
  - Direction indicators for prev/next stations.
- **Real-time Controls**: Instant feedback and response to user inputs.

<img width="294" alt="Screenshot 2024-12-25 at 14 20 59" src="https://github.com/user-attachments/assets/da28639d-7efb-4e0b-ba38-c74ee062fd0b" />



### 🔧 System Features

- **WiFi Manager**: Simplified WiFi configuration with AP mode fallback.
- **OTA Updates**: Seamless over-the-air firmware updates.
- **Volume Memory**: Retains last volume settings between power cycles.
- **Track Selection Memory**: Remembers the last played tracks for each folder.
- **Serial Output**: Detailed status logs for monitoring and debugging.

---

## Hardware Requirements

- **ESP8266 Board**: Compatible with NodeMCU, Wemos D1 Mini, etc.
- **DFPlayer Mini MP3 Player**
- **SD Card**: Formatted as FAT32 with audio files.
- **Speaker**: Suitable for MP3 playback.
- **Push Button**: Recommended: BSW215B3.
- **Power Supply**: 5V regulated power source.

---

## Pin Configuration

| Pin | Function          |
| --- | ----------------- |
| D5  | DFPlayer RX       |
| D6  | DFPlayer TX       |
| D4  | Main Button Input |

---

## Audio File Structure

The SD card file system must follow this structure:

```
/01/  # Departure melodies
/02/  # ATOS announcements
/03/  # Door chime sounds
/04/  # Platform announcements
```

---

## Setup Instructions

1. Format the SD card as FAT32.
2. Copy audio files to the appropriate folders.
3. Connect hardware according to the [Pin Configuration](#pin-configuration).
4. Flash firmware to the ESP8266 using the Arduino IDE.
5. Power on the device.
6. Connect to the "JR-BERU-AP" WiFi network.
7. Configure your WiFi settings via the web interface.
8. Access the web interface at the device’s assigned IP address.

---

## Web Interface Access

- **URL**: `http://[device-ip]`

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

The device outputs detailed status information via Serial (115200 baud rate). Use the Arduino IDE’s Serial Monitor or any serial terminal to monitor real-time logs.

### Boot Sequence Output

```
===================
JR-Beru Booting
===================
Firmware Version: MCU-RX.X.X WebUI-RX.X.X
Initializing MP3-Player ... (May take 3~5 seconds)
MP3-Player online.
=======Audio File Count:=======
Melody Files: X
Atos Files: X
DoorChime Files: X
VA Files: X
===============================
Starting Wifi Manager...
Wifi Manager Started!
Web server started!
========Boot up Completed!========
```

### Audio Playback Status

- Track selection: `PlayingFolder: X, Playing Audio: Y`
- Random play: `RandomPlay: X/Y`
- Playback completion: `Number:X Play Finished!`
- Door chime triggers: `====Door Chime playing====`

### Error Messages

- `Time Out!`: MP3 player communication timeout.
- `Stack Wrong!`: MP3 player stack error.
- `Card not found`: SD card reading error.
- `Cannot Find File`: Requested audio file not found.
- `File Index Out of Bound`: Invalid file number requested.

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
- Built using the ESP8266 Arduino Core and DFRobotDFPlayerMini library.

---

## License

This project is released under the MIT License. See the `LICENSE` file for details.

---

## Support

For issues and feature requests, please use the [GitHub Issues](https://github.com/99buntai/JR-BERU-ESP/issues) page.

