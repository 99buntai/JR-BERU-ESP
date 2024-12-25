# JR-Beru (JR発車ベル)
A Japanese train station departure bell and announcement system simulator with web ui control for running on ESP8266.

## Features

### 🚂 Audio Playback
- **Departure Melodies**: JR station departure melodies
- **ATOS Announcements**: Yamanote Line ATOS announcements
- **Door Chimes**:  Door opening/closing sounds
- **Platform Announcements**: JR station platform announcements

### 🎛️ Control Methods
- **Physical Button Control**
  - Single press: Start melody loop
  - Release: Play ATOS announcement followed by door chime
- **Web Interface Control**
  - Audio selection for all sound types
  - Volume control (0-30)
  - Random play toggle
  - Manual platform announcement trigger

### 📱 Web Interface
- **Responsive Design**: Works on both desktop and mobile devices
- **Station Sign Display**: (Static for now, can be changed to dynamic with further development)
  - Authentic JR station signage design (currently yamanote line)
  - Multilingual station names (Japanese, Korean, English)
  - Station code and numbering (JY03)
  - Direction indicators
- **Real-time Controls**: Instant response to user input
- **Password Protection**: Basic authentication OTA update ability for security
![alt text](https://github.com/99buntai/JR-BERU-ESP/blob/main/Demo-img/webui.png)



### 🔧 System Features
- **WiFi Manager**: Easy WiFi configuration for setup
- **OTA Updates**: Over-the-air firmware updates
- **Volume Memory**: Retains volume settings
- **Track Selection Memory**: Remembers last played tracks

## Hardware Requirements
- ESP8266 Board (NodeMCU, Wemos D1 Mini, etc.)
- DFPlayer Mini MP3 Player
- SD Card
- Speaker
- Push Button (BSW215B3)
- Power Supply

## Pin Configuration
- D5: DFPlayer RX
- D6: DFPlayer TX
- D4: Main Button Input

## Audio File Structure
SD card must be formatted as FAT32 and contain the following folders: 

/01/ # Departure melodies
/02/ # ATOS announcements
/03/ # Door chime sounds
/04/ # Platform announcements


## Setup Instructions
1. Format SD card as FAT32
2. Copy audio files to appropriate folders
3. Connect hardware according to pin configuration
4. Flash firmware to ESP8266
5. Power on device
6. Connect to "JR-BERU-AP" WiFi network
7. Configure your WiFi settings
8. Access web interface at device IP address

## Web Interface Access
- **URL**: http://[device-ip]


## Firmware Updates
1. Access http://[device-ip]/update
2. Enter credentials when prompted
- **Default OTA Username**: JR
- **Default OTA Password**: beru
3. Select new firmware file
4. Click upload

## Version History
- **MCU-R0.3.5 WebUI-R2.0.2**
  - Enhanced station sign display
  - Improved audio control reliability
  - Added Multi language support
  - WiFi connection stability improvements

## Serial Monitoring
  - The device outputs detailed status information via Serial (115200 baud rate). 
  - You can monitor these messages using the Arduino IDE's Serial Monitor or any serial terminal.


### Audio Playback Status
The serial output provides real-time information about audio playback:
- Track selection: `PlayingFolder: X, Playing Audio: Y`
- Random play: `RandomPlay: X/Y`
- Playback completion: `Number:X Play Finished!`
- Door chime triggers: `====Door Chime playing====`

### Error Messages
The system reports various error conditions:
- `Time Out!`: MP3 player communication timeout
- `Stack Wrong!`: MP3 player stack error
- `Card not found`: SD card reading error
- `Cannot Find File`: Requested audio file not found
- `File Index Out of Bound`: Invalid file number requested

### WiFi Status
- WiFi connection status
- IP address assignment
- Web server initialization

### Debugging Tips
1. Open Serial Monitor in Arduino IDE
2. Set baud rate to 115200
3. Ensure "Newline" is selected for line ending
4. Power cycle the device to see boot sequence
5. Monitor real-time operation status

====================================================================================================

## Credits
- Station melodies and announcements are property of JR East
- Web interface design inspired by JR Yamanote Line station signage. 
- Built with ESP8266 Arduino Core and DFRobotDFPlayerMini library

## License
This project is released under the MIT License. See LICENSE file for details.

## Support
For issues and feature requests, please use the GitHub issues page.
----
