#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <SPIFFS.h>
#include <DFRobotDFPlayerMini.h>
#include <ElegantOTA.h>
#include <ArduinoJson.h>

WebServer server(80);

#define RX_PIN 16  // For DFPlayer
#define TX_PIN 17  // For DFPlayer
#define BUTTON_PIN 4  // Replace D4 with GPIO4
HardwareSerial dfPlayerSerial(1); // Use ESP32's second UART
#define FPSerial dfPlayerSerial

DFRobotDFPlayerMini myDFPlayer;
void printDetail(uint8_t type, int value);

#define CONFIG_ASYNC_TCP_RUNNING_CORE 0
#define CONFIG_ASYNC_TCP_USE_WDT 0

//================================
// Global Configuration Constants
//================================
#define FW_VERSION      "ESP32-MCU-R0.4.10 WebUI-R3.2.5"  // Current firmware version

// Audio folder mappings on SD card
int MelodyFolder = 1;    // /01/ - Contains departure melodies
int AtosFolder = 2;      // /02/ - Contains ATOS announcements
int DoorChimeFolder = 3; // /03/ - Contains door chime sounds
int VAFolder = 4;        // /04/ - Contains platform announcements
int FileCount;
// Volume configuration
int globalVolume = 22;   // Initial volume (range: 0-30)

// Track counters - stores number of files in each folder
int MelodyCount = 1;     // Total number of melody files
int AtosCount = 1;       // Total number of ATOS files
int DoorChimeCount = 1;  // Total number of door chime files
int VACount = 1;         // Total number of platform announcement files

int currentMelody = 1;   //current melody
int currentAtos = 1;    //current atos
int currentDoorChime = 1; //current door chime
int currentVA = 1;      //current va

int MainButtonPin = BUTTON_PIN;  // Use the ESP32 GPIO definition we set earlier
int MainButtonState = HIGH;  // Current state of the button
int lastMainButtonState = HIGH;  // Previous state of the button
int loopPlaying = false;
int DonePlaying = false;
int AtosLastPlayed = false;
bool atosPlayed = false;
bool shouldSaveConfig = false;
bool RandomPlayOn = true;

unsigned long lastDebounceTime = 0;  // Last time the button state changed
unsigned long debounceDelay = 10;  // Debounce time; increase if the output flickers

//HTTP Logins
const char* HTTP_USERNAME = "JR";
const char* HTTP_PASSWORD = "beru";

char ssid[32];  // Stores SSID of the WiFi network
char pass[32];  // Stores password of the WiFi network

// Add these variables to store current station data
struct StationData {
    String lineCode = "";
    String stationCodeLine = "";
    String stationNumber = "";
    String lineMarkerBgColor = "#FFFFFF";
    String lineNumberBgColor = "#FFFFFF";
    String directionBarBgColor = "#FFFFFF";
    String stationNameJa = "";
    String stationNameHiragana = "";
    String stationNameKo = "";
    String stationNameEn = "";
    String prevStation = "";
    String prevStationEn = "";
    String nextStation = "";
    String nextStationEn = "";
    String wardBox = "";
    String trackKey = "";

    // Constructor with default initialization
    StationData() = default;

    // Reset function to clear all values
    void reset() {
        lineCode = stationCodeLine = stationNumber = "";
        lineMarkerBgColor = lineNumberBgColor = directionBarBgColor = "#FFFFFF";
        stationNameJa = stationNameHiragana = stationNameKo = stationNameEn = "";
        prevStation = prevStationEn = nextStation = nextStationEn = "";
        wardBox = trackKey = "";
    }
} currentStation;

DynamicJsonDocument stationConfig(120768); // 120KB buffer for JSON
bool configLoaded = false;

String currentConfigFilename = "station_config.json";

String playbackMode = "selected"; // Options: "selected", "random", "sequence"
int sequenceCurrentIndex = 0; // For tracking current position in sequence
String sequenceLine = ""; // Current line for sequence play
uint32_t stationChangeCounter = 0; // Counter for tracking station changes
bool sequenceInProgress = false; // Flag to prevent multiple sequence triggers

// This structure to hold system health data
struct SystemHealth {
    size_t totalHeap;
    size_t freeHeap;
    float heapUsagePercent;
    size_t totalPsram;
    size_t freePsram;
    float psramUsagePercent;
    float flashUsagePercent;
    float spiffsUsagePercent;
    float cpu0Usage;
    float cpu1Usage;
    float temperature;
};

// These global variables is for CPU usage calculation
static uint32_t previousCpu0Time = 0;
static uint32_t previousCpu1Time = 0;
static uint32_t previousIdleTime0 = 0;
static uint32_t previousIdleTime1 = 0;

// System monitoring
unsigned long lastHealthCheck = 0;  // For tracking health check intervals

// This structure is for device state
struct DeviceState {
    bool randomPlay;
    int volume;
    int currentMelody;
    int currentAtos;
    int currentDoorChime;
    int currentVA;
    String selectedLine;
    String selectedStation;
    String selectedTrack;
};







//================================WiFi and Server Setup=============================
void saveConfigCallback() {shouldSaveConfig = true;}
void setupWiFiManager() {
  WiFiManager wifiManager;

  // Configure timeouts
  wifiManager.setConfigPortalTimeout(1800); // Portal timeout in seconds
  wifiManager.setConnectTimeout(30); // Connection attempt timeout
  
  // Configure AP settings
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Set WiFi mode explicitly
  WiFi.mode(WIFI_STA);
  
  // First try to connect using saved credentials
  bool autoConnectSuccess = wifiManager.autoConnect("JR-BERU-AP");
  
  if (!autoConnectSuccess) {
    Serial.println("Failed to connect, starting config portal");
    
    // Start config portal in blocking mode if auto-connect failed
    if (!wifiManager.startConfigPortal("JR-BERU-AP")) {
      Serial.println("Failed to connect and hit timeout");
      // Don't restart, just continue without WiFi
    }
  }
  
  // If we get here, either connection was successful or we timed out
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected - continuing without WiFi");
  }
}


void setupOTA() {
  ElegantOTA.begin(&server); // Start ElegantOTA with the server object
//  ElegantOTA.setFormAuthentication("admin", "admin"); // Set authentication credentials if needed
}
//==================================================================================

// Sequence Queue for safe sequence operations
QueueHandle_t sequenceQueue;

// Serial shell variables
const char* PROMPT = "JR-Beru:_$ ";
String serialCommand = "";
bool commandComplete = false;

void setup() {
  dfPlayerSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(600);

  Serial.println(F("\n==================="));
  Serial.println(F(" JR-Beru Booting "));
  Serial.println(F("==================="));
  Serial.printf_P(PSTR("Firmware Version: %s\n"), FW_VERSION);

  // Initialize MP3 player
  Serial.println(F("\nInitializing MP3-Player ... (May take 3~5 seconds)"));
  if (!myDFPlayer.begin(FPSerial, /*isACK = */true, /*doReset = */false)) {
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    while(true){
      delay(0);
    }
  }
  Serial.println(F("MP3-Player online."));
  myDFPlayer.volume(globalVolume);

  UpdateFileCount();

  // Initialize SPIFFS
  if(!SPIFFS.begin(true)) {
    Serial.println(F("SPIFFS Mount Failed"));
    return;
  }

  // Load config and state in the correct order
  loadConfigFilename();
  
  // Make sure station config is loaded first
  if (!loadStationConfig()) {
    Serial.println(F("Failed to load station config"));
  } else {
    Serial.println(F("Station config loaded successfully"));
  }
  
  // Now load device state which will use the station config
  loadDeviceState();
  
  // Debug output after loading
  Serial.println(F("After loading device state:"));
  Serial.printf("Current values - Line: '%s', Station: '%s', Track: '%s'\n", 
               currentStation.stationCodeLine.c_str(), 
               currentStation.stationNameEn.c_str(), 
               currentStation.trackKey.c_str());

  myDFPlayer.disableLoop();
  PlayRandomAudio(MelodyFolder,MelodyCount);

  // Create button handler task on Core 0
  xTaskCreatePinnedToCore(
    buttonHandlerTask,
    "buttonHandler",
    2048,  // Stack size
    NULL,
    23,     // Priority
    NULL,
    0  // runs on core
  );

  // Start WiFi setup
  setupWiFiManager();

  // Only initialize web server if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    ElegantOTA.setAuth("JR", "BERU");
    setupOTA();
    setupWebServer();
  }
  
  // Continue with normal operation regardless of WiFi status
  Serial.println(F("========Boot up Completed!========"));

  // Initial system health check
  checkSysHealth();

  // Create a queue for sequence operations
  sequenceQueue = xQueueCreate(1, sizeof(bool));

    // Create a task for sequence handling 
    xTaskCreatePinnedToCore(
      sequenceHandlerTask,
      "sequenceHandler",
      8192,  // stack size
      NULL,
      1,
      NULL,
      CONFIG_ASYNC_TCP_RUNNING_CORE
    );

    // Initialize serial shell
    Serial.println(F("\nWelcome to JR-Beru Shell. Type 'help' for available commands."));
     Serial.println(F("Firmware Version: " FW_VERSION));Serial.println("");
    Serial.print(PROMPT);
}



//===============================================================
// Main Loop
//===============================================================

void loop()
{
  // Handle serial input for the shell
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    
    // Echo the character back to the terminal
    Serial.print(inChar);
    
    // If newline or carriage return, command is complete
    if (inChar == '\n' || inChar == '\r') {
      Serial.println(); // Force a new line
      commandComplete = true;
    } else {
      // Add character to command string
      serialCommand += inChar;
    }
  }
  
  // Process command if complete
  if (commandComplete) {
    processCommand();
    serialCommand = ""; // Clear the command buffer
    commandComplete = false;
  }

  // Only handle web-related tasks if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    ElegantOTA.loop();
    checkWiFiConnection();
  }
  
  // Handle button input and other tasks
  handleButton();
  CheckDFPStatus();
  DoorChime();
  
  // Small delay to prevent watchdog issues
  delay(1);
}

//===============================================================
//  HTML Page
//===============================================================
void handleRoot() {
  String html = "<html><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "* { box-sizing: border-box; margin: 0; padding: 0; }"
                "body { font-family: 'Segoe UI', Roboto, Arial, sans-serif; background: #f5f5f5; color: #333; line-height: 1.6; }"
                
                // Navbar 
                ".navbar { position: fixed; top: 0; left: 0; right: 0; background: #2c3e50; color: white; "
                "padding: 8px; text-align: center; z-index: 1000; }"
                ".navbar h1 { font-size: 1.5rem; font-weight: 500; margin: 0; }"
                
                // Main container
                ".container { max-width: 800px; margin: 80px auto 100px; padding: 20px; }"
                
                // Panel and controls - consolidated properties
                ".panel { background: white; border-radius: 10px; padding: 15px; margin-bottom: 15px; box-shadow: 0 2px 4px rgba(0,0,0,0.05); }"
                ".panel-header { font-size: 1.2rem; color: #2c3e50; margin-bottom: 12px; font-weight: bold; }"
                ".control-group { display: flex; align-items: center; margin-bottom: 12px; gap: 10px; }"
                ".control-label { flex: 1; font-size: 0.95rem; color: #555; }"
                
                // Selectors
                "select { flex: 0 0 80px; padding: 8px; border: 1px solid #ddd; border-radius: 6px; background: #fff; font-size: 0.9rem; color: #333; }"
                
                // Toggle switch
                ".toggle-switch { position: relative; display: inline-block; width: 40px; height: 22px; }"
                ".toggle-switch input { opacity: 0; width: 0; height: 0; }"
                ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }"
                ".slider:before { position: absolute; content: ''; height: 16px; width: 16px; left: 3px; bottom: 3px; background-color: white; transition: .4s; border-radius: 50%; }"
                "input:checked + .slider { background-color: #2196F3; }"
                "input:checked + .slider:before { transform: translateX(18px); }"
                
                // Volume slider
                ".volume-slider { flex: 1; -webkit-appearance: none; height: 5px; border-radius: 5px; background: #ddd; outline: none; }"
                ".volume-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: #2196F3; cursor: pointer; }"
                
                // Buttons
                ".btn { background: #2196F3; color: white; border: none; padding: 10px 15px; border-radius: 6px; "
                "cursor: pointer; font-size: 0.9rem; transition: all 0.3s; display: flex; align-items: center; justify-content: center; }"
                ".btn:hover { filter: brightness(1.1); }"
                ".btn-compact { padding: 6px 12px; font-size: 0.85rem; }"
                ".btn-primary { background: #2196F3; }"
                ".btn-secondary { background: #757575; }"
                ".btn-warning { background: #e84636; }"
                
                // Footer
                ".footer { position: fixed; bottom: 0; left: 0; right: 0; background: #2c3e50; color: #fff; "
                "text-align: center; padding: 1rem; font-size: 0.8rem; }"
                
                // Version info
                ".version-info { text-align: center; color: #666; font-size: 0.8rem; margin-top: 20px; }"
                
                // Station sign - maintained position property for ward-label positioning
                ".station-sign { background: linear-gradient(180deg, #ffffff 0%, #f0f0f0 100%); "
                "margin: 0 0 20px 0; padding: 15px 15px 0; position: relative; border-radius: 10px; "
                "box-shadow: 0 2px 4px rgba(0,0,0,0.05); overflow: hidden; }"
                
                // Content container
                ".station-content { position: relative; width: 100%; display: flex; justify-content: center; "
                "max-width: 800px; margin: 0 auto; padding: 0 60px; }"
                
                // Line marker - kept as is, contains dynamic color
                ".line-marker { position: absolute; top: 0; transform: translateX(calc(-100% - 10px)); "
                "width: 48px; height: 66px; background: " + currentStation.lineMarkerBgColor + "; border-radius: 8px; "
                "box-shadow: 0 2px 4px rgba(0,0,0,0.2); }"
                
                // Consolidated line codes
                ".line-code-akb, .line-code-jy { font-weight: bold; font-size: 14px; }"
                ".line-code-akb { position: absolute; top: 0; left: 0; right: 0; color: white; text-align: center; }"
                ".line-code-jy { color: #000; line-height: 1; margin-bottom: 2px; }"
                
                // Station number - kept as is, contains dynamic color
                ".station-number-container { position: absolute; top: 20px; left: 3px; right: 3px; bottom: 3px; "
                "background: " + currentStation.lineNumberBgColor + "; border-radius: 5px; padding: 3.7px; }"
                ".station-number-inner { background: white; border-radius: 2px; height: 100%; width: 100%; "
                "display: flex; flex-direction: column; justify-content: center; align-items: center; }"
                ".line-number { color: #000; font-weight: bold; font-size: 19px; line-height: 0.7; }"
                
                // Station info - consolidated text alignments
                ".station-info { position: relative; display: flex; flex-direction: column; align-items: center; width: auto; min-width: 200px; margin: 0 auto; }"
                ".staName-ja, .staName-hiragana, .staName-ko { text-align: center; }"
                ".staName-ja { position: relative; display: inline-block; font-size: 42px; font-weight: bold; "
                "line-height: 1.2; font-family: 'MS Gothic', 'Yu Gothic', sans-serif; white-space: nowrap; }"
                ".staName-hiragana { font-size: 16px; color: #333;font-weight: bold; }"
                ".staName-ko { font-size: 14px; color: #666; margin-bottom: 6px; }"
                
                // Ward label - critical for positioning, kept exactly as is
                ".ward-label { position: absolute; top: 15px; right: 50px; display: flex; gap: 1px; }"
                ".ward-box { background: white; color: black; width: 16px; height: 16px; "
                "border: 1px solid black; border-radius: 2px; display: flex; align-items: center; "
                "justify-content: center; font-size: 12px; }"
                ".ward-text { background: black; color: white; width: 16px; height: 16px; "
                "border-radius: 2px; display: flex; align-items: center; justify-content: center; "
                "font-size: 12px; }"
                
                // Direction bar - kept as is, contains dynamic color
                ".direction-bar { background: " + currentStation.directionBarBgColor + "; height: 32px; "
                "display: flex; justify-content: space-between; align-items: center; color: white; position: relative; "
                "clip-path: polygon(0 0, calc(100% - 15px) 0, 100% 50%, calc(100% - 15px) 100%, 0 100%); "
                "overflow: visible; }"
                
                // Station indicator - kept as is, contains dynamic color
                ".station-indicator { position: absolute; top: 0; left: 50%; transform: translateX(-50%); "
                "width: 32px; height: 32px; background: " + currentStation.lineNumberBgColor + "; }"
                
                // Direction stations - simplified
                ".direction-left, .direction-right { width: 33%; }"
                ".direction-left { font-weight: bold; padding-left: 15px; text-align: left; }"
                ".direction-right { font-weight: bold; padding-right: 20px; text-align: right; }"
                ".direction-station { font-size: 16px; font-weight: bold; }"
                
                // English station names - consolidated styles
                ".staName-en { font-size: 16px; color: #333; }"
                ".staName-en.current { font-weight: bold; }"
                ".staName-en-left { width: 33%; text-align: left; }"
                ".staName-en-center { width: 33%; text-align: center; }"
                ".staName-en-right { width: 33%; text-align: right; }"
                ".station-names-container { display: flex; justify-content: space-between; padding: 5px 15px; }"
                
                // Direction bar and names container - kept as is
                ".direction-bar, .station-names-container { width: 100%; max-width: 700px; margin: 0 auto; }"
                
                // Control layout - consolidated grid styles
                ".controls-grid, .button-group, .station-selectors { display: grid; gap: 12px; }"
                ".controls-grid { margin: 0; }"
                ".button-group { grid-template-columns: repeat(3, 1fr); gap: 8px; }"
                ".station-selectors { grid-template-columns: repeat(3, 1fr); gap: 8px; }"
                
                // Other control classes 
                ".control-item { display: flex; align-items: center; gap: 10px; }"
                ".control-item.right { justify-content: flex-end; }"
                ".control-item label { font-size: 0.95rem; color: #555; }"
                ".volume-container { grid-column: 1 / -1; display: flex; align-items: center; gap: 15px; }"
                ".config-info { font-size: 0.85rem; color: #666; background: #f5f5f5; border-radius: 4px; }"
                
                // Sequence controls 
                ".sequence-controls { display: grid; grid-template-columns: 2fr 1fr; gap: 8px; }"
                ".sequence-buttons { display: flex; justify-content: flex-end; }"
                ".play-button-container { display: flex; justify-content: flex-end; grid-column: 2 / 3; }"
                
                // Consolidated media queries
                "@media (max-width: 800px) {"
                "  .container { padding: 10px; margin-top: 70px; }"
                "  .panel { padding: 12px; }"
                "  .control-group { margin-bottom: 8px; gap: 8px; }"
                "  .control-label { font-size: 0.9rem; flex: 0 0 110px; }"
                "  .btn-compact, select { width: 100%; }"
                "  .btn-compact { padding: 8px; font-size: 0.8rem; }"
                "  .controls-grid { gap: 8px; }"
                "  .station-selectors { grid-template-columns: 1fr; }"
                "}"
                
                "</style></head><body>"
                
                "<div class='navbar'><h1>" + 
                String("\xE7\x99\xBA\xE8\xBB\x8A\xE3\x83\x99\xE3\x83\xAB") + // 発車ベル
                "</h1></div>"
                "<div class='container'>";

  // Add Station Sign
  html += "<div class='station-sign'>"
          "<div class='station-content'>"
          "<div class='line-marker'>"
          "<div class='line-code-akb'>" + currentStation.lineCode + "</div>"
          "<div class='station-number-container'>"
          "<div class='station-number-inner'>"
          "<div class='line-code-jy'>" + currentStation.stationCodeLine + "</div>"
          "<div class='line-number'>" + currentStation.stationNumber + "</div>"
          "</div>"
          "</div>"
          "</div>"
          "<div class='station-info'>"
          "<div class='staName-ja'>" + currentStation.stationNameJa + "</div>"
          "<div class='staName-hiragana'>" + currentStation.stationNameHiragana + "</div>"
          "<div class='staName-ko'>" + currentStation.stationNameKo + "</div>"
          "</div>"
          "</div>"
          "<div class='ward-label'>"
          "<div class='ward-box'>" + currentStation.wardBox + "</div>"
          "<div class='ward-text'>区</div>"
          "</div>"
          "<div class='direction-bar'>"
          "<div class='station-indicator'></div>"
          "<div class='direction-left'>"
          "<span class='direction-station'>" + currentStation.prevStation + "</span>"
          "</div>"
          "<div class='direction-right'>"
          "<span class='direction-station'>" + currentStation.nextStation + "</span>"
          "</div>"
          "</div>"
          "<div class='station-names-container'>"
          "<span class='staName-en staName-en-left'>" + currentStation.prevStationEn + "</span>"
          "<span class='staName-en staName-en-center current'>" + currentStation.stationNameEn + "</span>"
          "<span class='staName-en staName-en-right'>" + currentStation.nextStationEn + "</span>"
          "</div>";

  // Add the Sequence Control panel first
  html += "<div class='panel'>"
          "<div class='panel-header'>Sequence Control</div>"
          "<div class='controls-grid'>"
          
          // Config file info
          "<div class='config-info'>"
          "Config File: <span id='configName'></span>"
          "</div>"
          
          // File controls in a grid layout
          "<div class='button-group'>"
          "<input type='file' id='configFile' accept='.json' style='display:none'>"
          "<button class='btn btn-compact' onclick='document.getElementById(\"configFile\").click()'>Upload</button>"
          "<button class='btn btn-compact' onclick='downloadConfig()'>Download</button>"
          "<button class='btn btn-compact btn-warning' onclick='deleteConfig()'>Delete</button>"
          "</div>"
          
          // Sequence controls container
          // Station selectors
          "<div class='station-selectors'>"
          "<select id='lineSelect' onchange='updateStationSelect()'><option>Select Line</option></select>"
          "<select id='stationSelect' onchange='updateTrackSelect()'><option>Select Station</option></select>"
          "<select id='trackSelect' onchange='updateStationSign()'><option>Select Track</option></select>"
          "</div>"

          // Platform announcement button
          "<div class='play-button-container' style='display: flex; gap: 8px;'>"
          "<select id='playMode' class='play-mode-select' style='flex: 1;'>"
          "<option value='selected'>Play Selected</option>"
          "<option value='sequence'>Play in Sequence</option>"
          "<option value='random'>Random Play</option>"
          "</select>"
          "<button class='btn btn-primary btn-compact' onclick='playVA()' style='flex: 1;'>Platform Announcement</button>"
          "</div>"
          
          "</div></div>";

  // Then add the Audio Selection panel
  html += "<div class='panel'>"
          "<div class='panel-header'>Audio Selection</div>";
  
  // Melody selector
  html += "<div class='control-group'>"
          "<label class='control-label'>Melody</label>"
          "<select id='melody' name='melody' onchange='updateConfig()'>" + 
          populateOptions("01melody", MelodyCount, currentMelody) + 
          "</select></div>";

  // Atos selector
  html += "<div class='control-group'>"
          "<label class='control-label'>Atos</label>"
          "<select id='atos' name='atos' onchange='updateConfig()'>" + 
          populateOptions("02atos", AtosCount, currentAtos) + 
          "</select></div>";

  // Door Chime selector
  html += "<div class='control-group'>"
          "<label class='control-label'>Door Chime</label>"
          "<select id='doorchime' name='doorchime' onchange='updateConfig()'>" + 
          populateOptions("03DoorChime", DoorChimeCount, currentDoorChime) + 
          "</select></div>";

  // Platform Announcement selector
  html += "<div class='control-group'>"
          "<label class='control-label'>Platform Announcement</label>"
          "<select id='platform' name='platform' onchange='updateConfig()'>" + 
          populateOptions("04PlatformAnnouncement", VACount, currentVA) + 
          "</select></div></div>";

  // Playback Controls Panel
  html += "<div class='panel'>"
          "<div class='panel-header'>Playback Controls</div>"
          "<div class='controls-grid'>"
          
          // Reinit DFPlayer Button
          "<div class='control-item right'>"
          "<button class='btn btn-warning' onclick='reinitDFPlayer()'>Reinit DFPlayer</button>"
          "</div>"
          
          // Volume Control
          "<div class='volume-container'>"
          "<label>Volume</label>"
          "<input type='range' class='volume-slider' id='volumeControl' min='0' max='30' "
          "value='" + String(globalVolume) + "' oninput='setVolume(this.value)'>"
          "<span id='volumeValue' style='min-width: 25px;'>" + String(globalVolume) + "</span>"
          "</div>"
          
          "</div>";

  // Version info
  html += "<div class='version-info'>Firmware Version: " FW_VERSION "</div>";

  // Footer
  html += "<div class='footer'>&copy; 2025 " + 
          String("\xE7\x99\xBA\xE8\xBB\x8A\xE3\x83\x99\xE3\x83\xAB") + // 発車ベ尔
          "</div>";

  // JavaScript
  html += "<script>"
          // API calls
          "const api = (url, method='GET') => fetch(url, {method});"
          "const $ = id => document.getElementById(id);"
          "const $all = (selector, parent=document) => parent.querySelectorAll(selector);"
          
          // Update audio config
          "function updateConfig() {"
          "  const params = ['melody', 'atos', 'doorchime', 'platform']"
          "    .map(id => id + '=' + $(id).value).join('&');"
          "  api('/updateConfig?' + params);"
          "}"

          // Play VA function
          "function playVA() {"
          "  const mode = $('playMode').value;"
          "  fetch('/playVA?mode=' + mode);"  // call the playVA endpoint
          "}"

          // Volume control
          "function setVolume(v) {"
          "  api('/setVolume?value=' + v);"
          "  $('volumeValue').textContent = v;"
          "}"
          
          // DFPlayer reinitialization
          "function reinitDFPlayer() {"
          "  api('/reinitDFPlayer');"
          "  alert('DFPlayer reinitialization requested');"
          "}"

          // Line marker position
          "function updateLineMarkerPosition() {"
          "  const stationNameJa = document.querySelector('.staName-ja');"
          "  const lineMarker = document.querySelector('.line-marker');"
          "  const stationContent = document.querySelector('.station-content');"
          "  const stationCode = document.querySelector('.line-code-akb');"
          "  "
          "  if (stationNameJa && lineMarker && stationContent) {"
              // Handle if station code is blank
          "    if (stationCode && (!stationCode.textContent || stationCode.textContent.trim() === '')) {"
          "      lineMarker.style.backgroundColor = 'transparent';"
          "      lineMarker.style.boxShadow = 'none';"
          "      lineMarker.style.transform = 'translate(-55px, -16px)';" // no station code station marker posotion X,Y
          "    } else {"
                // When code exists, force reset all modified styles"
          "      lineMarker.style.backgroundColor = '';"
          "      lineMarker.style.boxShadow = '';"
          "      lineMarker.style.transform = '';"
          "    }"
          "    "
              // Update Line Marker position
          "    const offset = stationNameJa.getBoundingClientRect().left - "
          "                   stationContent.getBoundingClientRect().left;"
          "    lineMarker.style.left = offset + 'px';" // Maintain original positioning logic
          "  }"
          "}"
          
          // Update Line Marker position on load
          "window.addEventListener('load', function() {"
          "  setTimeout(updateLineMarkerPosition, 10);"
          "});"

              // File handling functions
          "function uploadConfig() {"
          "  const file = $('configFile').files[0];"
          "  if (!file) return;"
          "  const fd = new FormData();"
          "  fd.append('config', file);"
          "  fetch('/upload', {method: 'POST', body: fd})"
          "    .then(response => {"
          "      if (!response.ok) {"
          "        return response.json().then(data => {"
          "          throw new Error(data.error || 'Unknown error occurred');"
          "        });"
          "      }"
          "      return response;"
          "    })"
          "    .then(() => forceConfigReload())"
          "    .catch(e => {"
          "      console.error('Error:', e);"
          "      alert('Upload failed: ' + e.message);"
          "    });"
          "}"

          "function downloadConfig() { location.href = '/downloadConfig'; }"

          "function deleteConfig() {"
          "  if (!confirm('Delete configuration file?')) return;"
          "  api('/deleteConfig')"
          "    .then(r => r.ok ? r.text() : Promise.reject('Status: ' + r.status))"
          "    .then(() => {"
          "      return forceConfigReload();"
          "    })"
          "    .then(() => {"
          "      alert('Configuration deleted successfully');"
          "    })"
          "    .catch(e => alert('Failed to delete configuration: ' + e));"
          "}"

          // Cached config and loading
          "let cachedConfig = null;"
          "function loadConfig() {"
            // Clear the cached config first
          "  cachedConfig = null;"
            // Reset all selectors to initial state
          "  $('lineSelect').innerHTML = '<option>Select Line</option>';"
          "  $('stationSelect').innerHTML = '<option>Select Station</option>';"
          "  $('trackSelect').innerHTML = '<option>Select Track</option>';"
          "  Promise.all(["
          "    api('/getCurrentConfig').then(r => r.json()),"
          "    api('/getStationConfig').then(r => r.json())"
          "  ]).then(([configInfo, config]) => {"
          "    $('configName').textContent = configInfo.filename;"
              // Update cached config with fresh data
          "    cachedConfig = JSON.parse(JSON.stringify(config));"
          "    $('playMode').value = configInfo.playbackMode;"
          "    if (configInfo.volume !== undefined) {"
          "      const volumeControl = $('volumeControl');"
          "      if (volumeControl) volumeControl.value = configInfo.volume;"
          "      const volumeValue = $('volumeValue');"
          "      if (volumeValue) volumeValue.textContent = configInfo.volume;"
          "    }"
          "    populateSelector('lineSelect', config.lines, null);"
          "    return api('/getCurrentSelections').then(r => r.json());"
          "  }).then(sel => {"
          "    if (!sel) return;"
          "    if (sel.line && sel.station && sel.track) {"
          "      $('lineSelect').value = sel.line;"
          "      if (cachedConfig?.lines[sel.line]) {"
          "        populateSelector('stationSelect', cachedConfig.lines[sel.line].stations, sel.station);"
          "        $('stationSelect').value = sel.station;"
          "        const stationData = cachedConfig.lines[sel.line].stations[sel.station];"
          "        if (stationData?.t && stationData.t.length > 0) {"
          "          const tracks = {};"
          "          stationData.t.forEach(trackArray => {"
          "            if (trackArray && trackArray.length > 0) {"
          "              const trackKey = trackArray[0];"
          "              tracks[trackKey] = trackKey;"
          "            }"
          "          });"
          "          populateSelector('trackSelect', tracks, sel.track);"
          "          $('trackSelect').value = sel.track;"
          "          updateStationSign();"
"        }"
"      }"
          "    }"
          "  }).catch(e => {"
          "    console.error('Error loading config:', e);"
              // Reset cached config on error
          "    cachedConfig = null;"
          "  });"
          "}"
          
          // Add a function to force config reload
          "function forceConfigReload() {"
          "  cachedConfig = null;"
          "  return loadConfig();"
          "}"
          
          // Populate selectors
          "function populateSelector(id, items, selected) {"
          "  const sel = $(id);"
          "  if (!sel) return;"
          "  sel.innerHTML = '<option>' + sel.options[0].text + '</option>';"
          "  const keys = Object.keys(items || {});"
          "  keys.forEach(item => {"
          "    const opt = document.createElement('option');"
          "    opt.value = opt.textContent = item;"
          "    opt.selected = item === selected;"
          "    sel.appendChild(opt);"
          "  });"
          "}"
          
          // Update selectors with selection data
          "function updateSelectors(sel, config) {"
          "  console.log('updateSelectors called with:', sel);"
          "  if (!config) {"
          "    console.log('No config provided, returning');"
          "    return;"
          "  }"
          "  const line = config?.lines[sel.line];"
          "  const station = line?.stations[sel.station];"
          "  console.log('Line:', line ? 'found' : 'not found', 'Station:', station ? 'found' : 'not found');"
          "  const lsel = $('lineSelect');"
          "  const lines = Array.from(lsel.options).find(o => o.value === sel.line);"
          "  if (lines) lines.selected = true;"
          "  if (line) populateSelector('stationSelect', line.stations, sel.station);"
          "  if (station && station.t) {"
          "    console.log('Station has t array with length:', station.t.length);"
          "    const tracks = {};"
          "    station.t.forEach(trackArray => {"
          "      if (trackArray && trackArray.length > 0) {"
          "        const trackKey = trackArray[0];"
          "        tracks[trackKey] = trackKey;"
          "        console.log('Added track key:', trackKey);"
          "      }"
          "    });"
          "    console.log('Populating track selector with keys:', Object.keys(tracks));"
          "    populateSelector('trackSelect', tracks, sel.track);"
          "    console.log('Track selector populated, selected track:', sel.track);"
          "  } else {"
          "    console.log('Station does not have t array or is null');"
          "  }"
          "}"

          // Update station selector
          "function updateStationSelect() {"
          "  const line = $('lineSelect').value;"
          "  $('stationSelect').innerHTML = '<option>Select Station</option>';"
          "  $('trackSelect').innerHTML = '<option>Select Track</option>';"
          "  if (line && cachedConfig?.lines[line]) {"
          "    populateSelector('stationSelect', cachedConfig.lines[line].stations);"
          "    if ($('stationSelect').options.length > 1) {"
          "      $('stationSelect').selectedIndex = 1;"
          "      updateTrackSelect();"
          "    }"
          "  }"
          "}"

          // Update track selector
          "function updateTrackSelect() {"
          "  const line = $('lineSelect').value;"
          "  const station = $('stationSelect').value;"
          "  $('trackSelect').innerHTML = '<option>Select Track</option>';"
          "  if (line && station && cachedConfig?.lines[line]?.stations[station]) {"
          "    const stationData = cachedConfig.lines[line].stations[station];"
          "    if (stationData.t && stationData.t.length > 0) {"
          "      const tracks = {};"
          "      stationData.t.forEach(trackArray => {"
          "        if (trackArray && trackArray.length > 0) {"
          "          const trackKey = String(trackArray[0]);"
          "          tracks[trackKey] = trackKey;"
          "        }"
          "      });"
          "      populateSelector('trackSelect', tracks);"
          "      if ($('trackSelect').options.length > 1) {"
          "        $('trackSelect').selectedIndex = 1;"
          "        updateStationSign();"
          "      }"
          "    }"
          "  }"
          "}"

          // Update station sign
          "let updateInProgress = false;"
          "function updateStationSign() {"
          "  if (updateInProgress) return;"
          "  updateInProgress = true;"
          
          "  const line = $('lineSelect').value;"
          "  const station = $('stationSelect').value;"
          "  const track = $('trackSelect').value;"

          "  if (!line || !station || !track || track === 'Select Track') {"
          "    updateInProgress = false;"
          "    return;"
          "  }"

          "  const requiredElements = ["
          "    '.line-marker', '.station-number-container', '.station-indicator', '.direction-bar',"
          "    '.staName-ja', '.staName-hiragana', '.staName-ko', '.ward-box',"
          "    '.line-code-akb', '.line-code-jy', '.line-number',"
          "    '.direction-left .direction-station', '.direction-right .direction-station',"
          "    '.staName-en-left', '.staName-en-center', '.staName-en-right'"
          "  ];"
          
          "  if (!requiredElements.every(selector => document.querySelector(selector))) {"
          "    setTimeout(() => {"
          "      updateInProgress = false;"
          "      updateStationSign();"
          "    }, 100);"
          "    return;"
          "  }"

          "  fetch(`/updateStationSign?line=${line}&station=${station}&track=${track}`)"
          "    .then(response => response.ok ? response : Promise.reject('Failed to update'))"
          "    .then(() => {"
          "      if (!cachedConfig) return;"

          "      const stationData = cachedConfig.lines[line].stations[station];"
          "      if (!stationData) return;"
          
          "      const style = cachedConfig.lines[line].style;"
          
          "      let trackData = null;"
          "      if (stationData.t && stationData.t.length > 0) {"
          "        for (const trackArray of stationData.t) {"
          "          if (String(trackArray[0]) === String(track)) {"
          "            trackData = trackArray;"
          "            break;"
          "          }"
          "        }"
          "      }"

          "      if (!trackData) return;"

          // Background color updates
          "      document.querySelector('.line-marker').style.backgroundColor = style.lineMarkerBgColor;"
          "      document.querySelector('.station-number-container').style.backgroundColor = style.lineNumberBgColor;"
          "      document.querySelector('.station-indicator').style.backgroundColor = style.lineNumberBgColor;"
          "      document.querySelector('.direction-bar').style.backgroundColor = style.directionBarBgColor;"

          // Update line marker position
          "      setTimeout(updateLineMarkerPosition, 10);"
          
          // Audio value updates
          "      if ($('melody')) $('melody').value = trackData[3] && trackData[3][0] ? trackData[3][0] : 1;"
          "      if ($('atos')) $('atos').value = trackData[3] && trackData[3][1] ? trackData[3][1] : 1;"
          "      if ($('doorchime')) $('doorchime').value = trackData[3] && trackData[3][2] ? trackData[3][2] : 1;"
          "      if ($('platform')) $('platform').value = trackData[3] && trackData[3][3] ? trackData[3][3] : 1;"

          // Text updates from i array
          "      const stInfo = stationData.i || [];"
          "      document.querySelector('.staName-ja').textContent = stInfo[1] || '';"
          "      document.querySelector('.staName-hiragana').textContent = stInfo[2] || '';"
          "      document.querySelector('.staName-ko').textContent = stInfo[3] || '';"
          "      document.querySelector('.ward-box').textContent = stInfo[4] || '';"

          // Line code and station number
          "      document.querySelector('.line-code-akb').textContent = stInfo[0] || '';"
          "      document.querySelector('.line-code-jy').textContent = trackData[1] || '';"
          "      document.querySelector('.line-number').textContent = trackData[2] || '';"

          // Direction information
          "      document.querySelector('.direction-left .direction-station').textContent = "
          "        trackData[4] && trackData[4][0] ? trackData[4][0] : '';"
          "      document.querySelector('.direction-right .direction-station').textContent = "
          "        trackData[5] && trackData[5][0] ? trackData[5][0] : '';"

          // Station names in English
          "      document.querySelector('.staName-en-left').textContent = "
          "        trackData[4] && trackData[4][1] ? trackData[4][1] : '';"
          "      document.querySelector('.staName-en-center').textContent = station;"
          "      document.querySelector('.staName-en-right').textContent = "
          "        trackData[5] && trackData[5][1] ? trackData[5][1] : '';"

          // Update config values
          "      updateConfig();"
          "    })"
          "    .catch(error => {"
          "      setTimeout(() => updateStationSign(), 500);"
          "    })"
          "    .finally(() => {"
          "      updateInProgress = false;"
          "    });"
          "}"

          "$('configFile').addEventListener('change', uploadConfig);"
          "loadConfig();"

          // Add explicit event listeners for selectors
          "document.addEventListener('DOMContentLoaded', function() {"
          "  if ($('lineSelect')) {"
          "    $('lineSelect').addEventListener('change', function() {"
          "      updateStationSelect();"
          "    });"
          "  }"
          
          "  if ($('stationSelect')) {"
          "    $('stationSelect').addEventListener('change', function() {"
          "      updateTrackSelect();"
          "      updateStationSign();" // Add immediate update
          "    });"
          "  }"
          
          "  if ($('trackSelect')) {"
          "    $('trackSelect').addEventListener('change', function() {"
          "      updateStationSign();"
          "    });"
          "  }"
          "});"

// Poll station changes, update UI 
"function pollStationChanges() {"
"  const currentCounter = window.lastStationCounter || 0;"
"  fetch('/checkStationChanged?counter=' + currentCounter)"
"    .then(r => r.json())"
"    .then(data => {"
      // Always update the counter
"      window.lastStationCounter = data.counter;"
"      if (data.changed) {"
"        console.log('Station changed to:', data.station, 'Counter:', data.counter);"
       // Update UI selectors
"        if ($('lineSelect').value !== data.line) {"
"          $('lineSelect').value = data.line;"
"          populateSelector('stationSelect', cachedConfig.lines[data.line].stations, data.station);"
"        }"
"        $('stationSelect').value = data.station;"
"        const stationData = cachedConfig.lines[data.line].stations[data.station];"
"        if (stationData.t && stationData.t.length > 0) {"
"          const tracks = {};"
"          stationData.t.forEach(trackArray => {"
"            if (trackArray && trackArray.length > 0) {"
"              const trackKey = trackArray[0];"
"              tracks[trackKey] = trackKey;"
"            }"
"          });"
"          populateSelector('trackSelect', tracks, data.track);"
"          $('trackSelect').value = data.track;"
"          console.log('Calling updateStationSign from pollStationChanges');"
          // First make sure the server has the latest selection
"          fetch(`/updateStationSign?line=${data.line}&station=${data.station}&track=${data.track}`)"
"            .then(response => response.ok ? response : Promise.reject('Failed to update'))"
"            .then(() => {"
"              console.log('Server updated, now updating UI');"
"              updateStationSign();"
"            })"
"            .catch(error => console.error('Error updating station sign:', error));"
"        }"
"      }"
"    })"
"    .catch(e => console.error('Error checking station changes:', e));"
"}"
// Poll every 3 seconds
"setInterval(pollStationChanges, 3000);"

          // Add an event listener to set the playback mode on page load
          "document.getElementById('playMode').addEventListener('change', function() {"
          "  const mode = this.value;"
          "  fetch('/setPlaybackMode?mode=' + mode)"
          "    .then(response => response.ok ? response.text() : Promise.reject('Failed to set mode'))"
          "    .catch(error => console.error('Error:', error));"
          "});"
          "</script></body></html>";

  server.send(200, "text/html", html);

}


//===============================================================
// Populate Options
//===============================================================

String populateOptions(const char* folderName, int count, int selectedOption) {
  String options = "";
  for (int i = 1; i <= count; ++i) {
    options += "<option value='" + String(i) + "' " + (i == selectedOption ? "selected" : "") + ">" + String(i) + "</option>";
  }
  return options;
}




//===============================================================
// Update File Count
//===============================================================

void UpdateFileCount() {
   Serial.println(F("\n=====Audio File Count:====="));

   MelodyCount = CheckFileInFolder(MelodyFolder);
    Serial.printf("Melody Files: %d\n", MelodyCount);

   AtosCount = CheckFileInFolder(AtosFolder);
    Serial.printf("Atos Files: %d\n", AtosCount);

    DoorChimeCount = CheckFileInFolder(DoorChimeFolder);
    Serial.printf("DoorChime Files: %d\n", DoorChimeCount);

   VACount = CheckFileInFolder(VAFolder);
    Serial.printf("VA Files: %d\n", VACount);
    
   Serial.println(F("==========================="));
}

//===============================================================
// Play Current Audio
//===============================================================


void PlayCurrentAudio(int folder, int audioIndex) {
  myDFPlayer.playFolder(folder, audioIndex);
  Serial.printf_P(PSTR("PlayingFolder: %d\n"), folder);
  Serial.printf_P(PSTR("Playing Audio: %d\n"), audioIndex);
  delay(100);
}


//===============================================================
// Play Random Audio
//===============================================================

void PlayRandomAudio(int folder, int TotalAudioCount) {

   int RandomAudio = generateRandomNumber(1, TotalAudioCount);
    myDFPlayer.playFolder(folder, RandomAudio);
    Serial.printf_P(PSTR("PlayingFolder: %d\n"), folder);
    Serial.printf_P(PSTR("RandomPlay: %d/%d\n"), RandomAudio, TotalAudioCount);
    delay(100); // Waiting for mp3 board
}

//===============================================================
// Generate Random Number
//===============================================================
int generateRandomNumber(int min, int max) {
  return random(min, max + 1); // The +1 ensures that the upper bound is inclusive
}



  //===============================================================
  // Check File In Folder
  //===============================================================



int CheckFileInFolder(int folder) {
  int fileCount = 0;
  int retryCount = 0;
  const int MAX_RETRIES = 3;
  
  // Try up to MAX_RETRIES times to get a valid file count
  while (retryCount < MAX_RETRIES) {
    fileCount = myDFPlayer.readFileCountsInFolder(folder);
    fileCount = myDFPlayer.readFileCountsInFolder(folder);//bug fix! do not remove run twice to get the correct value
    if (fileCount > 0) {
      break; // Valid count received
    }
    retryCount++;
    delay(10); // Short delay between retries
  }
  
  return (fileCount > 0) ? fileCount : 0; // Return 0 if no valid count
}



//===============================================================
// Main Button Handler
//===============================================================

void handleButton() {
    unsigned long currentTime = millis();
    int reading = digitalRead(BUTTON_PIN);

    if (reading != lastMainButtonState) {
        lastDebounceTime = currentTime;
    }

    if ((currentTime - lastDebounceTime) > debounceDelay && reading != MainButtonState) {
      MainButtonState = reading;

        if (MainButtonState == LOW) {
          Serial.println(F("\n==Button Pressed=="));
            if (!loopPlaying) {
               handlePlayMelody();
                myDFPlayer.enableLoop(); // Call only once
                loopPlaying = true;
                AtosLastPlayed = false;
                //notifyUIofStationChange();
            }
        } else if (loopPlaying) {
          Serial.println(F("\n==Button Released=="));
            //myDFPlayer.stop();
            myDFPlayer.disableLoop();
            loopPlaying = false;
            handlePlayAtos();

          AtosLastPlayed = true;
    }
  }

  lastMainButtonState = reading;
}



//===============================================================
// Toggle Random Play
//===============================================================

void toggleRandomPlay() {
  Serial.print("Toggling RandomPlay to ");
  Serial.println(RandomPlayOn ? "off" : "on");
  RandomPlayOn = !RandomPlayOn;
}

//===============================================================
// Play Audio
//===============================================================

void playAudio(int folder, int currentTrack, int totalTracks) {
 DonePlaying = false;
    myDFPlayer.playFolder(folder, RandomPlayOn ? random(1, totalTracks + 1) : currentTrack);
    Serial.printf_P(PSTR("Playing %s audio %d/%d from folder %d\n"), 
                   RandomPlayOn ? "random" : "current", 
                   RandomPlayOn ? random(1, totalTracks + 1) : currentTrack,
                   totalTracks, folder);
    delay(100); // Waiting for mp3 board
}

// Simplified audio handlers using the consolidated playAudio function
void handlePlayMelody() { playAudio(MelodyFolder, currentMelody, MelodyCount); }
void handlePlayAtos() { playAudio(AtosFolder, currentAtos, AtosCount); }
void handlePlayVA() { playAudio(VAFolder, currentVA, VACount); }

// Optimized door chime function
void DoorChime() {
    if (!DonePlaying || !AtosLastPlayed) return;
    
    Serial.println(F("\n====Door Chime playing===="));
    DonePlaying = AtosLastPlayed = false;
    
    playAudio(DoorChimeFolder, currentDoorChime, DoorChimeCount);
    
    if (playbackMode == "sequence" && !sequenceInProgress) {
        triggerSequencePlay();
    }
}

/**
 * Trigger sequence play via the queue
 */
void triggerSequencePlay() {
  Serial.println(F("## Sequence mode active ##"));
  
  // Set the sequence in progress flag to prevent multiple triggers
  sequenceInProgress = true;
  
  // Use the queue to schedule the advancement in the separate task
  bool signal = true;
  if (xQueueSend(sequenceQueue, &signal, 0) != pdTRUE) {
    Serial.println(F("Error: Failed to send sequence signal to queue"));
    sequenceInProgress = false;
  }
}

//================================MP3-Player-Sleep=============================
void DFPSleep() {
  if (DonePlaying) {
    myDFPlayer.sleep();
    Serial.println(F("==MP3 Board going to sleep=="));
  }
}

//===============================================================
// DFPlayer Status Check
//===============================================================

void CheckDFPStatus(){

        if (myDFPlayer.available()) {;printDetail(myDFPlayer.readType(), myDFPlayer.read());}
}

void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerUSBInserted:
      Serial.println("USB Inserted!");
      break;
    case DFPlayerUSBRemoved:
      Serial.println("USB Removed!");
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      DonePlaying = true;

      //old code, new code is in handleSequencePlay()
    /* if (playbackMode == "sequence" && value == currentAtos) {
        // Only advance to next station if the ATOS announcement finished
        Serial.println(F("Sequence mode: Advancing to next station"));
        handleSequencePlay();
      }
      */
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

}





// Task management for web server
void setupWebServerTask() {
    xTaskCreatePinnedToCore(
        webServerTask,
        "serverLoop",
        30000,
        NULL,
        1,
        NULL,
        CONFIG_ASYNC_TCP_RUNNING_CORE
    );
}

// Memory monitoring
void checkHeap() {
    Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());
}

void webServerTask(void * parameter) {
  for(;;) {
    server.handleClient();
    delay(1);
  }
}

//===============================================================
// Web Server Setup
//===============================================================

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.enableCORS(true);
  

  server.on("/playVA", HTTP_GET, []() {
    String mode = server.arg("mode");
    
    // If no Playback mode specified, use the stored value
    if (mode.isEmpty()) {
      mode = playbackMode;
    }
    
    Serial.printf_P(PSTR("Playing platform announcement with mode: %s\n"), mode.c_str());
    
    if (mode == "random") {
      // Random play logic
      RandomPlayOn = true;
      int randomTrack = random(1, VACount + 1);
      PlayCurrentAudio(VAFolder, randomTrack);
    }
    else { 
      // "selected" mode or any other mode - just play current selection
      // Don't advance sequences from the UI button
      RandomPlayOn = false;
      PlayCurrentAudio(VAFolder, currentVA);
    }
    
    server.send(200, "text/plain", "Playing announcement");
  });
  
  server.on("/setVolume", HTTP_GET, []() {
    String value = server.arg("value");
    if (value != "") {
      globalVolume = value.toInt();
      myDFPlayer.volume(globalVolume);
      Serial.print(F("Updated Volume:"));Serial.println(globalVolume);
    }
    server.send(200, "text/plain", "Volume set to " + String(globalVolume));
  });

  server.on("/updateConfig", HTTP_GET, []() {
    int melodyValue = server.arg("melody").toInt();
    int atosValue = server.arg("atos").toInt();
    int doorchimeValue = server.arg("doorchime").toInt();
    int platformValue = server.arg("platform").toInt();

    currentMelody = melodyValue;
    currentAtos = atosValue;
    currentDoorChime = doorchimeValue;
    currentVA = platformValue;

    Serial.println(F("===Audio Selection Updated!===="));
    Serial.print(F("Melody: ")); Serial.println(currentMelody);
    Serial.print(F("Atos: ")); Serial.println(currentAtos);
    Serial.print(F("DoorChime: ")); Serial.println(currentDoorChime);
    Serial.print(F("VA: ")); Serial.println(currentVA);
    Serial.println(F("==============================="));

    server.send(200, "text/plain", "Configuration updated");
  });

  server.on("/reinitDFPlayer", HTTP_GET, []() {
    myDFPlayer.reset();
    Serial.println(F("====DFPlayer Reinitialized!===="));
    delay(100);
    myDFPlayer.volume(globalVolume);
    UpdateFileCount();
    server.send(200, "text/plain", "DFPlayer reinitialized");
  });

// Upload config file handler
  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "File uploaded");
  }, handleFileUpload);

  server.on("/downloadConfig", HTTP_GET, []() {
    if (SPIFFS.exists("/station_config.json")) {
      File file = SPIFFS.open("/station_config.json", "r");
      server.streamFile(file, "application/json");
      file.close();
    } else {
      server.send(404, "text/plain", "Config file not found");
    }
  });

  server.on("/deleteConfig", HTTP_GET, []() {
    if (!SPIFFS.exists("/station_config.json")) {
        server.send(404, "text/plain", "Config file not found");
        return;
    }
    
    bool success = true;
    
    // Delete the config file
    if (!SPIFFS.remove("/station_config.json")) {
        success = false;
    }

    // Delete any temporary upload files if they exist
    if (SPIFFS.exists("/temp_upload.json")) {
        Serial.println("Removing temporary upload file");
        if (!SPIFFS.remove("/temp_upload.json")) {
            Serial.println("Failed to delete temporary upload file");
            // Don't set success to false since this is not critical
        }
    }
    
    // Delete the config filename file
    if (SPIFFS.exists("/config_filename.txt")) {
        if (!SPIFFS.remove("/config_filename.txt")) {
            success = false;
        }
    }
    
    if (success) {
        configLoaded = false;  // Reset the config loaded flag
        stationConfig.clear(); // Clear the JSON document
        currentConfigFilename = "station_config.json";  // Reset to default filename
        
        // Create and load default config
        createDefaultConfig();
        loadStationConfig();
        selectFirstAvailableOptions();
        saveDeviceState();
        
        server.send(200, "text/plain", "Config deleted");
    } else {
        server.send(500, "text/plain", "Failed to delete file");
    }
  });


  server.on("/getStationConfig", HTTP_GET, []() {
    if (!configLoaded) loadStationConfig();
    String jsonStr;
    serializeJson(stationConfig, jsonStr);
    server.send(200, "application/json", jsonStr);
  });

  // Add this new endpoint in setupWebServer():
  server.on("/updateStationSign", HTTP_GET, []() {
    const String line = server.arg("line");
    const String station = server.arg("station");
    const String track = server.arg("track");
    
    if (line.isEmpty() || station.isEmpty() || track.isEmpty() || track == "Select Track" || !configLoaded) {
        server.send(400, "text/plain", "Invalid selection");
        return;
    }

    JsonObject stationData;
    JsonObject style;
    if (!getStationData(line, station, stationData, style)) {
        server.send(400, "text/plain", "Invalid station data");
        return;
    }

    // Create a temporary track object
    StaticJsonDocument<512> trackDoc;
    JsonObject trackData = trackDoc.to<JsonObject>();
    
    if (!findTrackData(stationData, track, trackData)) {
        server.send(404, "text/plain", "Track not found");
        return;
    }

    // Update currentStation with all values
    updateCurrentStation(stationData, trackData, style, line.c_str(), station.c_str(), track.c_str());
    
    saveDeviceState();
    server.send(200, "text/plain", "Station sign updated");
  });

  // Add an endpoint to get the current filename:
  server.on("/getCurrentConfig", HTTP_GET, []() {
    // Return config filename, playback mode and volume in a JSON response
    StaticJsonDocument<256> response;
    response["filename"] = currentConfigFilename;
    response["playbackMode"] = playbackMode;
    response["volume"] = globalVolume;  // Add volume information
    
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  });

  // First, add new endpoints in setupWebServer():

  server.on("/getCurrentSelections", HTTP_GET, []() {
    // Create a JSON object with current selections
    StaticJsonDocument<200> doc;
    doc["line"] = currentStation.stationCodeLine;//set the line to the current station code line
    doc["station"] = currentStation.stationNameEn;//set the station to the current station name
    doc["track"] = currentStation.trackKey;//set the track to the current track 
    
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  });

  // Add endpoint to get current station
  server.on("/getCurrentStation", HTTP_GET, []() {
    // Create a JSON object with current station information
    StaticJsonDocument<200> doc;
    
    // Debug output to serial
    Serial.println(F("getCurrentStation endpoint called"));
    Serial.printf("Current values - Line: '%s', Station: '%s', Track: '%s'\n", 
                 currentStation.stationCodeLine.c_str(), 
                 currentStation.stationNameEn.c_str(), 
                 currentStation.trackKey.c_str());
    
    // Simply return the current values without any conditions
    doc["line"] = currentStation.stationCodeLine;
    doc["station"] = currentStation.stationNameEn;
    doc["track"] = currentStation.trackKey;
    
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    Serial.printf("Sending response: %s\n", jsonResponse.c_str());
    server.send(200, "application/json", jsonResponse);
  });

  // Create a new endpoint to set playback mode
  // Add this in the setupWebServer function
  server.on("/setPlaybackMode", HTTP_GET, []() {
    String mode = server.arg("mode");
    if (!mode.isEmpty()) {
      // Add detailed logging
      Serial.printf_P(PSTR("\n===== PLAYBACK MODE CHANGE ======\n"));
      Serial.printf_P(PSTR("Previous mode: %s\n"), playbackMode.c_str());
      Serial.printf_P(PSTR("Current mode: %s\n"), mode.c_str());
      Serial.println(F("================================"));
      
      // Reset sequence flag when changing modes
      sequenceInProgress = false;
      
      playbackMode = mode;
      saveDeviceState(); // Save it persistently
      
      // Update other settings based on mode
      if (mode == "random") {
        RandomPlayOn = true;
      } else {
        RandomPlayOn = false;
      }
    }
    server.send(200, "text/plain", "Playback mode set to: " + playbackMode);
  });

  // Add this endpoint INSIDE the setupWebServer function, not at global scope
  server.on("/checkStationChanged", HTTP_GET, []() {
    // Get the client's last known counter value
    uint32_t clientCounter = 0;
    if (server.hasArg("counter")) {
      clientCounter = server.arg("counter").toInt();
    }
    
    // If server counter is higher, send the update
    if (stationChangeCounter > clientCounter) {
      StaticJsonDocument<512> doc;
      doc["changed"] = true; //set the changed to true
      doc["counter"] = stationChangeCounter; //set the counter to the station change counter
      doc["line"] = currentStation.stationCodeLine; //set the line to the current station code line
      doc["station"] = currentStation.stationNameEn; //set the station to the current station name
      doc["track"] = currentStation.trackKey; //set the track to the current track key
      
      String response;
      serializeJson(doc, response);//serialize the json document to a string
      server.send(200, "application/json", response);
    } else {
      // No changes, return the current counter
      server.send(200, "application/json", "{\"changed\":false,\"counter\":" + String(stationChangeCounter) + "}");
    }
  });

  server.begin();
  Serial.println(F("Web server started!"));
  
  xTaskCreatePinnedToCore(//create a task
    webServerTask,//the task function
    "serverLoop",//the name of the task
    30000,//the stack size of the task
    NULL,//the task parameter
    1,//the priority of the task
    NULL,//the task handle
    CONFIG_ASYNC_TCP_RUNNING_CORE//the core to run the task on
  );
}




//===============================================================
// Button Handler Task
//===============================================================

void buttonHandlerTask(void * parameter) {
  for(;;) {
    handleButton();
    CheckDFPStatus();
    DoorChime();
    delay(1); // Small delay to prevent watchdog issues

  }
}



    //===============================================================
    // Save Device State
    //===============================================================

void saveDeviceState() {
    StaticJsonDocument<512> doc;
    
    doc["randomPlay"] = RandomPlayOn;
    doc["volume"] = globalVolume;
    doc["melody"] = currentMelody;
    doc["atos"] = currentAtos;
    doc["doorChime"] = currentDoorChime;
    doc["va"] = currentVA;
    doc["line"] = currentStation.stationCodeLine;
    doc["station"] = currentStation.stationNameEn;
    doc["track"] = currentStation.trackKey;
    doc["playbackMode"] = playbackMode; // Add playback mode to saved state

    File file = SPIFFS.open("/device_state.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
    }
}

//===============================================================
// Load Device State
//===============================================================

void loadDeviceState() {
    Serial.println(F("\n=== Loading Device State ==="));
    
    // Initialize with default values first
    if (!configLoaded) {
        loadStationConfig();
    }
    
    // Set default playback mode
    playbackMode = "selected";
    
    // If no saved state file exists, set defaults from config
    if (!SPIFFS.exists("/device_state.json")) {
        Serial.println(F("No saved state found - using defaults from config"));
      if (configLoaded) {
            // Get the first line
          String firstLine;
          JsonObject lines = stationConfig["lines"];
          for (JsonPair line : lines) {
              firstLine = line.key().c_str();
              break;
          }
          
            if (!firstLine.isEmpty()) {
                // Get the first station
          String firstStation;
          JsonObject stations = lines[firstLine]["stations"];
          for (JsonPair station : stations) {
              firstStation = station.key().c_str();
              break;
          }
          
                if (!firstStation.isEmpty()) {
                    JsonObject stationData;
                    JsonObject style;
                    if (!getStationData(firstLine, firstStation, stationData, style)) {
                        return;
                    }
                    
                    // Create a temporary track object
                    StaticJsonDocument<512> trackDoc;
                    JsonObject trackData = trackDoc.to<JsonObject>();
                    String firstTrack;
                    
                    if (!getFirstTrackData(stationData, firstTrack, trackData)) {
                        return;
                    }
                    
                    // Update currentStation with default values
          updateCurrentStation(stationData, trackData, style, firstLine.c_str(), firstStation.c_str(), firstTrack.c_str());
                    saveDeviceState();  // Save these defaults
                    
                    Serial.println(F("Default station set from config"));
                    return;
                }
            }
        }
        return;
    }

    // Try to open the file
    File file = SPIFFS.open("/device_state.json", "r");
    if (!file) {
        Serial.println(F("Failed to open device state file"));
        playbackMode = "selected"; // Default value
        return;
    }

    // Read the file and handle potential errors
    String jsonStr = file.readString();
    file.close();
    
    if (jsonStr.length() == 0) {
        Serial.println(F("Empty device state file"));
        playbackMode = "selected";
        return;
    }

    // Try to parse the JSON with extra error handling
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.print("Failed to parse device state: ");
        Serial.println(error.c_str());
        Serial.println("Resetting to defaults");
        
        // Delete corrupted file and create a new one with defaults
        SPIFFS.remove("/device_state.json");
        
        // Set defaults
        RandomPlayOn = false;
        globalVolume = 22;
        currentMelody = 1;
        currentAtos = 1;
        currentDoorChime = 1;
        currentVA = 1;
        playbackMode = "selected";
        
        // Save default state
        saveDeviceState();
        return;
    }

    // Print current stored values
    Serial.println(F("\nStored values:"));
    Serial.printf("Random Play: %s\n", doc["randomPlay"].as<bool>() ? "ON" : "OFF");
    Serial.printf("Volume: %d\n", doc["volume"].as<int>());
    
    // Restore state with careful error handling
    RandomPlayOn = doc["randomPlay"].isNull() ? false : doc["randomPlay"].as<bool>();
    globalVolume = doc["volume"].isNull() ? 22 : doc["volume"].as<int>();
    currentMelody = doc["melody"].isNull() ? 1 : doc["melody"].as<int>();
    currentAtos = doc["atos"].isNull() ? 1 : doc["atos"].as<int>();
    currentDoorChime = doc["doorChime"].isNull() ? 1 : doc["doorChime"].as<int>();
    currentVA = doc["va"].isNull() ? 1 : doc["va"].as<int>();

    // Apply volume
    myDFPlayer.volume(globalVolume);

    // Safely set playback mode with a default if not present
    playbackMode = doc["playbackMode"].isNull() ? "selected" : doc["playbackMode"].as<String>();
    Serial.printf("Playback Mode: %s\n", playbackMode.c_str());

    // If we have station data, restore it
    if (doc.containsKey("line") && doc.containsKey("station") && doc.containsKey("track")) {
        String line = doc["line"].isNull() ? "" : doc["line"].as<String>();
        String station = doc["station"].isNull() ? "" : doc["station"].as<String>();
        String track = doc["track"].isNull() ? "" : doc["track"].as<String>();
        
        // Only attempt to restore if we have valid data
        if (line.length() > 0 && station.length() > 0 && track.length() > 0) {
            Serial.println(F("\nRestoring station selection:"));
            Serial.printf("Line: %s, Station: %s, Track: %s\n", line.c_str(), station.c_str(), track.c_str());
            
            // Safety check - verify all paths exist in the config before accessing
            if (configLoaded) {
                JsonObject stationData;
                JsonObject style;
                if (!getStationData(line, station, stationData, style)) {
                    Serial.println(F("Invalid station data in saved state"));
                    return;
                }
                
                // Create a temporary track object
                StaticJsonDocument<512> trackDoc;
                JsonObject trackData = trackDoc.to<JsonObject>();
                
                if (!findTrackData(stationData, track, trackData)) {
                    Serial.println(F("Track not found in saved state"));
                    return;
                }
                
                // Update currentStation with the loaded values
                updateCurrentStation(stationData, trackData, style, line.c_str(), station.c_str(), track.c_str());
                Serial.println(F("Station restored successfully"));
            } else {
                Serial.println(F("Config not loaded, cannot restore station"));
            }
        } else {
            Serial.println(F("No valid station data in saved state"));
        }
    } else {
        Serial.println(F("No station data in saved state"));
    }
    
    Serial.println(F("===========================\n"));
}





//===============================================================
// Validate Json File
//===============================================================

bool validateJsonFile(const char* filename, String& errorMsg) {
    // Required JSON properties to validate
    const char* requiredProps[] = {"lines", "style", "stations", "i", "t"};
    const char* propDescriptions[] = {
        "'lines' property",
        "'style' in line config",
        "'stations' in line config",
        "'info' property in station data",
        "'track' property in station data"
    };
    
    if (!SPIFFS.exists(filename)) {
        errorMsg = "File not found";
        return false;
    }
    
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        errorMsg = "Failed to open file";
        return false;
    }
    
    size_t fileSize = file.size();
    Serial.printf("Validating file: %s (%u bytes)\n", filename, fileSize);
    
    String jsonStr;
    jsonStr.reserve(fileSize + 1);
    
    // Read file in chunks
    const size_t BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];
    size_t totalRead = 0;
    
    while (file.available()) {
        size_t bytesRead = file.readBytes(buffer, BUFFER_SIZE);
        jsonStr += String(buffer).substring(0, bytesRead);
        
        totalRead += bytesRead;
        if (fileSize > 20000 && (totalRead % 20000) < BUFFER_SIZE) {
            Serial.printf("Validation progress: %u/%u bytes (%.1f%%)\n", 
                        totalRead, fileSize, (totalRead * 100.0) / fileSize);
        }
    }
    file.close();
    
    // Basic JSON structure validation
    if (!jsonStr.startsWith("{")) {
        errorMsg = "Invalid format (not a JSON file)";
        return false;
    }
    
    // Check for balanced braces
    int braceCount = 0;
    for (char c : jsonStr) {
        if (c == '{') braceCount++;
        else if (c == '}') braceCount--;
        if (braceCount < 0) {
            errorMsg = "Invalid format (more closing braces than opening)";
            return false;
        }
    }
    
    if (braceCount != 0) {
        errorMsg = "Invalid format (missing " + String(braceCount) + " closing braces)";
        return false;
    }
    
    // Check required properties
    for (size_t i = 0; i < sizeof(requiredProps)/sizeof(requiredProps[0]); i++) {
        if (jsonStr.indexOf("\"" + String(requiredProps[i]) + "\"") == -1) {
            errorMsg = "Missing " + String(propDescriptions[i]);
            return false;
        }
    }
    
    Serial.println("File validation successful");
    return true;
}

//===============================================================
// File Upload Handler
//===============================================================

void handleFileUpload() {
  HTTPUpload& upload = server.upload();//get the upload
  
  static File fsUploadFile;//set the file
  static size_t totalBytes = 0;//set the total bytes
  static unsigned long uploadStartTime = 0;//set the upload start time
  static String tempFilename;//set the temp filename
  static bool isStationConfigFile = false;//set the is station config file
  static String uploadError = ""; // Store upload errors
  static const size_t MAX_FILE_SIZE = 102400; // 100KB file size limit
  
  if (upload.status == UPLOAD_FILE_START) {
    uploadStartTime = millis();//get the upload start time
    totalBytes = 0;//set the total bytes to 0
    uploadError = ""; // Clear previous errors
    String filename = upload.filename;//get the filename
    Serial.println(F("\n==========Config Upload Utility=========="));
    Serial.printf_P(PSTR("Upload Start: %s\n"), filename.c_str());
    
    // Simply check if this is a JSON file - assume all JSON uploads are config files
    isStationConfigFile = filename.endsWith(".json");
    
    if (isStationConfigFile) {
      // Use a temporary file during upload for station config
      tempFilename = "/temp_upload.json";//set the temp filename
      filename = "/station_config.json";//set the filename
    } else if (!filename.startsWith("/")) {
      filename = "/" + filename;//set the filename
      tempFilename = filename;//set the temp filename
    } else {
      tempFilename = filename;//set the temp filename
    }
    
    // Always write to temp file first
    Serial.printf_P(PSTR("Writing to temp file: %s\n"), tempFilename.c_str());
    fsUploadFile = SPIFFS.open(tempFilename, "w");
    if (!fsUploadFile) {
      Serial.println("Failed to open file for writing");
      uploadError = "Failed to open file for writing";
      return;
    }
    Serial.println("File opened successfully for writing");
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {  //if the upload status is UPLOAD_FILE_WRITE
    totalBytes += upload.currentSize;//update the total bytes
    
    // Check if file exceeds size limit
    if (totalBytes > MAX_FILE_SIZE) {
      Serial.printf_P(PSTR("ERR: File too large, max size is %u bytes\n"), MAX_FILE_SIZE);
      uploadError = "File too large, max size is 100KB";
      
      // Close and delete the partial file
      if (fsUploadFile) {fsUploadFile.close();SPIFFS.remove(tempFilename);}
      // Send error response to frontend
      server.send(400, "application/json", "{\"error\":\"" + uploadError + "\"}");
      return;
    }
    
    if (fsUploadFile) {
      // Write data directly to SPIFFS without storing in memory
      size_t bytesWritten = fsUploadFile.write(upload.buf, upload.currentSize);
      
      // Every ~10KB to monitor the upload progress
      if (totalBytes % 10240 < upload.currentSize) {
        Serial.printf_P(PSTR("Upload progress: %u bytes written | Free heap: %u\n"), totalBytes, ESP.getFreeHeap());
      }
      
      if (bytesWritten != upload.currentSize) {
        Serial.printf_P(PSTR("Write error: only %u of %u bytes written\n"), bytesWritten, upload.currentSize);
        uploadError = "Write error: only " + String(bytesWritten) + " of " + 
                      String(upload.currentSize) + " bytes written";
      }
    }
    else {
      Serial.println("ERROR: File not open for writing during UPLOAD_FILE_WRITE");
      uploadError = "File not open for writing";
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    unsigned long uploadDuration = millis() - uploadStartTime;//get the upload duration 
    
    if (fsUploadFile) {
      fsUploadFile.close();
      Serial.printf_P(PSTR("Upload Complete: %u bytes in %lu ms\n"), totalBytes, uploadDuration);
      
      // Process the JSON file as a station config
      if (isStationConfigFile) {
        // Validate the uploaded file before replacing existing configuration
        String validationError;
        if (!validateJsonFile(tempFilename.c_str(), validationError)) {
          Serial.printf_P(PSTR("Validation failed: %s\n"), validationError.c_str());
          Serial.println(F("Deleting invalid temp file"));
          SPIFFS.remove(tempFilename);
          Serial.println(F("Keeping existing configuration"));
          Serial.println(F("========================================="));
          // Send error response immediately
          StaticJsonDocument<256> errorResponse;
          errorResponse["error"] = "Validation failed: " + validationError;
          String jsonResponse;
          serializeJson(errorResponse, jsonResponse);
          server.send(400, "application/json", jsonResponse);
          return;
        }
        
        Serial.println(F("Uploaded file validated!"));
        
        // If existing config exists, remove it
        if (SPIFFS.exists("/station_config.json")) {
          Serial.println(F("Removing existing station config"));
          SPIFFS.remove("/station_config.json");
        }
        
        // Rename temp file to the actual config file
        Serial.printf_P(PSTR("Renaming %s to /station_config.json\n"), tempFilename.c_str());
        if (SPIFFS.rename(tempFilename, "/station_config.json")) {
          Serial.println(F("File replaced successfully"));
        } else {
          Serial.println(F("Error replacing file"));
          uploadError = "Error renaming temporary file";
        }
        
        // Update the current config filename to the original uploaded filename
        currentConfigFilename = upload.filename;
        Serial.printf_P(PSTR("Updating config filename to: %s\n"), currentConfigFilename.c_str());
        
        // Save this filename to SPIFFS so it persists across reboots
        saveConfigFilename(currentConfigFilename);
        
        configLoaded = false;
        Serial.println(F("Loading new station configuration"));
        loadStationConfig();
        
        if (configLoaded) {
          Serial.println(F("New configuration loaded successfully"));
          Serial.println(F("========================================="));
          // Select first available options from the new config
          selectFirstAvailableOptions();
        } else {
          Serial.println(F("Failed to load new configuration"));
          Serial.println(F("========================================="));
          uploadError = "Failed to load new configuration";
        }
      }
      else {
        Serial.printf_P(PSTR("File uploaded as: %s (not a station configuration)\n"), tempFilename.c_str());
      }
    }
    else {
      Serial.println("ERROR: File not open during UPLOAD_FILE_END");
      uploadError = "File not open during upload completion";
    }
    
    // Send success response if we reach here
    server.send(200, "text/plain", "File uploaded successfully");
  }
}

//===============================================================
// Load Station Config Json
//===============================================================
bool loadStationConfig() {
  Serial.println(F("\n===== Loading Station Config Json====="));
  if (!SPIFFS.exists("/station_config.json")) {
    Serial.println(F("Config file not found, creating default Station config"));
    createDefaultConfig();
  }

  File file = SPIFFS.open("/station_config.json", "r");
  if (!file) {
    Serial.println(F("Failed to open Station config file"));
    return false;
  }

  size_t fileSize = file.size();//get the file size
  Serial.printf("Config file size: %u bytes\n", fileSize);

  stationConfig.clear();

  Serial.printf("Free heap before reading: %u\n", ESP.getFreeHeap());

  String jsonStr;
  jsonStr.reserve(fileSize + 1); // Pre-allocate memory

  const size_t bufferSize = 1024;//set the buffer size
  char buffer[bufferSize];//set the buffer
  size_t totalBytesRead = 0;//set the total bytes read

  //read the file
  while (totalBytesRead < fileSize) {
    size_t bytesToRead = min(bufferSize, fileSize - totalBytesRead);//get the bytes to read
    size_t bytesRead = file.read((uint8_t*)buffer, bytesToRead);//read the file
    if (bytesRead == 0) {
      Serial.println("Unexpected end of file or read error");
      break;
    }
    jsonStr += String(buffer).substring(0, bytesRead);//add the read data to the json string
    totalBytesRead += bytesRead;//update the total bytes read

    // Print progress every ~10KB
    if (totalBytesRead % 10240 < bufferSize) { 
      Serial.printf("Read progress: %u of %u bytes | Free heap: %u\n", 
                    totalBytesRead, fileSize, ESP.getFreeHeap());
    }
  }

  file.close();

  if (totalBytesRead != fileSize) {
    Serial.printf_P(PSTR("File read incomplete: expected %u bytes, got %u bytes\n"), fileSize, totalBytesRead);
    Serial.println(F("Aborting JSON parsing due to incomplete file read"));
    return false;
  }

  Serial.printf("Free heap before parsing: %u\n", ESP.getFreeHeap());

  Serial.println("Parsing JSON string...");
  DeserializationError error = deserializeJson(stationConfig, jsonStr);//deserialize the json string  
  
  Serial.printf("Free heap after parsing: %u\n", ESP.getFreeHeap());

  if (error) {
    Serial.println("Failed to parse config file");
    Serial.printf("Parsing error: %s\n", error.c_str());

    String failedFilename = currentConfigFilename;//get the current config filename
    Serial.printf("Deleting corrupted config file: %s\n", failedFilename.c_str());
    SPIFFS.remove("/station_config.json");//delete the corrupted config file
    SPIFFS.remove("/config_filename.txt");//delete the config filename file

    currentConfigFilename = "error_recovered.json";//set the current config filename to the error recovered file
    saveConfigFilename(currentConfigFilename);//save the current config filename

    stationConfig.clear();
    configLoaded = false;

    createDefaultConfig();

    file = SPIFFS.open("/station_config.json", "r");//open the default config file
    if (!file) {
      Serial.println("Failed to open default config");
    return false;
  }

    String defaultJsonStr = file.readString();//read the default config file
    file.close();

    DeserializationError defaultError = deserializeJson(stationConfig, defaultJsonStr);//deserialize the default config file
    if (defaultError) {
      Serial.printf("Error parsing default config: %s\n", defaultError.c_str());
      return false;
    }

    Serial.printf("Recovered from failed config file: %s\n", failedFilename.c_str());
    loadDeviceState();
  configLoaded = true;
    return true;
  }

  configLoaded = true;
  Serial.println("Station Config Json Loaded");
  Serial.printf("Final heap after loading: %u\n", ESP.getFreeHeap());
  Serial.println("======================================");
  return true;
}

//===============================================================
// Select First Available Options
//===============================================================
void selectFirstAvailableOptions() {
    String firstLine;
    JsonObject lines = stationConfig["lines"];
    for (JsonPair line : lines) {
        firstLine = String(line.key().c_str());
        break;
    }
    
    String firstStation;
    JsonObject stations = lines[firstLine]["stations"];
    for (JsonPair station : stations) {
        firstStation = String(station.key().c_str());
        break;
    }
    
    String firstTrack;
    JsonArray tracks = stations[firstStation]["t"];
    if (tracks.size() > 0) {
        firstTrack = tracks[0][0].as<String>();  // Keep as<String>() for array elements
    } else {
        return;
    }
    
    JsonObject stationData;
    JsonObject style;
    if (!getStationData(firstLine, firstStation, stationData, style)) {
        return;
    }
    
    // Create a temporary track object
    StaticJsonDocument<512> trackDoc;
    JsonObject trackData = trackDoc.to<JsonObject>();
    if (!findTrackData(stationData, firstTrack, trackData)) {
        return;
    }
    
    // Update currentStation with default values
    updateCurrentStation(stationData, trackData, style, firstLine.c_str(), firstStation.c_str(), firstTrack.c_str());
    saveDeviceState();
}

//===============================================================
// Create Default Config
//===============================================================
void createDefaultConfig() {
  Serial.println("Creating Default Station Config Json");
  // Default Config strings
  const char* defaultConfig = R"({"lines":{"JY":{"style":{"lineMarkerBgColor":"#000000","lineNumberBgColor":"#80c241","directionBarBgColor":"#006400"},"stations":{"Default Config":{"i":["AKB","秋葉原","あきはばら","System Restored","山"],"t":[[1,"JY","03",[1,1,1,1],["前の駅","Previous Station"],["次の駅","Next Station"]]]}}}}})";

  File file = SPIFFS.open("/station_config.json", "w");
  if (!file) {
    Serial.println("Failed to create default config");
    return;
  }
  file.print(defaultConfig);
  file.close();
}

// Add this function to save the filename
void saveConfigFilename(const String& filename) {
    File file = SPIFFS.open("/config_filename.txt", "w");
    if (file) {
        file.print(filename);
        file.close();
    }
}

//===============================================================
// Load Config Filename
//===============================================================
void loadConfigFilename() {
    if (SPIFFS.exists("/config_filename.txt")) {
        File file = SPIFFS.open("/config_filename.txt", "r");
        if (file) {
            currentConfigFilename = file.readString();//read the config filename
            file.close();
        }
    }
}

//===============================================================
// Sequence Play Function
//===============================================================
void handleSequencePlay() {
    if (sequenceLine.isEmpty()) {
        Serial.println(F("Error: No line selected for sequence"));
        return;
    }
    
    // Get all stations for the current line
    JsonObject stations = stationConfig["lines"][sequenceLine]["stations"];
    if (stations.isNull() || stations.size() == 0) {
        Serial.println(F("Error: No stations found for line"));
        return;
    }
    
    // Convert to array for indexed access
    int stationCount = stations.size();
    String stationKeys[stationCount];
    int currentIndex = 0;
    int i = 0;
    
    for (JsonPair station : stations) {
        stationKeys[i++] = station.key().c_str();
    }
    
    // Find current station index
    for (int i = 0; i < stationCount; i++) {
        if (stationKeys[i] == currentStation.stationNameEn) {
            currentIndex = i;
            break;
        }
    }
    
    // Get next station index (wrap around if at end)
    int nextIndex = (currentIndex + 1) % stationCount;
    String nextStation = stationKeys[nextIndex];
    
    // Get station data and validate
    JsonObject stationData;
    JsonObject style;
    if (!getStationData(sequenceLine, nextStation, stationData, style)) {
        Serial.println(F("Error: Invalid station data"));
        sequenceInProgress = false;
        return;
    }
    
    // Create a temporary track object
    StaticJsonDocument<512> trackDoc;
    JsonObject trackData = trackDoc.to<JsonObject>();
    String firstTrack;
    
    if (!getFirstTrackData(stationData, firstTrack, trackData)) {
        Serial.println(F("Error: No tracks found for station"));
        sequenceInProgress = false;
        return;
    }
    
    // Update station sign to the next station
    updateStationForSequence(sequenceLine.c_str(), nextStation.c_str(), firstTrack.c_str());
}

//===============================================================
// Update Station For Sequence Play
//===============================================================
void updateStationForSequence(const char* line, const char* station, const char* track) {
    if (!configLoaded) {
        Serial.println(F("Error: Config not loaded"));
        return;
    }
    
    JsonObject stationData;
    JsonObject style;
    if (!getStationData(line, station, stationData, style)) {
        Serial.println(F("Error: Invalid station data"));
        return;
    }
    
    // Create a temporary track object
    StaticJsonDocument<512> trackDoc;
    JsonObject trackData = trackDoc.to<JsonObject>();
    
    if (!findTrackData(stationData, track, trackData)) {
        Serial.println(F("Error: Track not found"));
        return;
    }
    
    // Update currentStation with all values
    updateCurrentStation(stationData, trackData, style, line, station, track);
    
    // Save state and notify UI
    saveDeviceState();
    notifyUIofStationChange();
    
    Serial.println(F("Station updated for sequence, UI notification sent"));
}

//===============================================================
// Notify UI of Station Changes
//===============================================================
void notifyUIofStationChange() {
  Serial.println(F("Sending UI updates"));
  
  // Create a JSON document with the updated station info
  StaticJsonDocument<512> doc;
  doc["action"] = "stationChanged";
  doc["line"] = currentStation.stationCodeLine;
  doc["station"] = currentStation.stationNameEn;
  doc["track"] = currentStation.trackKey;
  
  String debug;
  serializeJson(doc, debug);
  Serial.println("UI notification data: " + debug);
  
  // Increment the counter instead of setting a flag
  stationChangeCounter++;
  Serial.printf_P(PSTR("Station change counter: %d\n"), stationChangeCounter);
}

//===============================================================
// Sequence Handler Task
//===============================================================
void sequenceHandlerTask(void * parameter) {
  bool signal;
  
  for(;;) {
    // Wait for signal from the queue
    if (xQueueReceive(sequenceQueue, &signal, portMAX_DELAY) == pdTRUE) {
      if (signal) {
        Serial.println(F("==================Sequence Handler=================="));
        
        // Add a small delay to ensure audio has finished processing
        delay(100);
        
        // Process the sequence
        handleSequencePlay();
        
        // Reset the sequence in progress flag after a delay
        // This prevents multiple triggers in quick succession
        delay(500);
        sequenceInProgress = false;
        
        Serial.println(F("==================Sequence Complete================="));
      }
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
  }
}

//===============================================================
// WiFi Connection Monitoring and Recovery
//===============================================================
void checkWiFiConnection() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) {  // Check every 60 seconds
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi connection lost, reconnecting..."));
      WiFi.reconnect();
    }else {
      //Serial.println(F("Wifi health check passed"));
    }
  }
}



//===============================================================
// Update Current Station
//===============================================================
void updateCurrentStation(JsonObject stationData, JsonObject trackData, JsonObject style, 
                        const char* line, const char* station, const char* track) {
    // Use the new compact format (i array instead of sInfo object)
    JsonArray stationInfo = stationData["i"];
    
    currentStation.lineCode = stationInfo[0].as<const char*>();              // sC -> i[0]
    currentStation.stationCodeLine = trackData["Marker"]["LC"].as<const char*>();
    currentStation.stationNumber = trackData["Marker"]["sNum"].as<const char*>();
    currentStation.lineMarkerBgColor = style["lineMarkerBgColor"].as<const char*>();
    currentStation.lineNumberBgColor = style["lineNumberBgColor"].as<const char*>();
    currentStation.directionBarBgColor = style["directionBarBgColor"].as<const char*>();
    currentStation.stationNameJa = stationInfo[1].as<const char*>();         // Ja -> i[1]
    currentStation.stationNameHiragana = stationInfo[2].as<const char*>();   // Hi -> i[2]
    currentStation.stationNameKo = stationInfo[3].as<const char*>();         // Ko -> i[3]
    currentStation.stationNameEn = station;
    currentStation.prevStation = trackData["Dir"]["p"]["Ja"].as<const char*>();
    currentStation.prevStationEn = trackData["Dir"]["p"]["En"].as<const char*>();
    currentStation.nextStation = trackData["Dir"]["n"]["Ja"].as<const char*>();
    currentStation.nextStationEn = trackData["Dir"]["n"]["En"].as<const char*>();
    currentStation.wardBox = stationInfo[4].as<const char*>();               // wB -> i[4]
    currentStation.trackKey = track;
}

//===============================================================
// Check System Health
//===============================================================
void checkSysHealth() {
    SystemHealth health;
    
    // Memory metrics
    health.freeHeap = ESP.getFreeHeap();
    health.totalHeap = ESP.getHeapSize();
    health.heapUsagePercent = 100.0f * (1.0f - ((float)health.freeHeap / health.totalHeap));
    
    // SPIFFS metrics
    uint32_t totalBytes = SPIFFS.totalBytes();
    uint32_t usedBytes = SPIFFS.usedBytes();
    health.spiffsUsagePercent = 100.0f * ((float)usedBytes / totalBytes);
    
    // Flash metrics
    uint32_t flashSize = ESP.getFlashChipSize();
    uint32_t programSize = ESP.getSketchSize();
    health.flashUsagePercent = 100.0f * ((float)programSize / flashSize);
    
    // Temperature
    health.temperature = temperatureRead();
    
    // Print report in a consistent format
    Serial.println(F("\n======== System Health ========"));
    Serial.printf("Memory    : %.1f%% used (%u KB free)\n", 
                 health.heapUsagePercent, health.freeHeap / 1024);
    Serial.printf("Flash     : %.1f%% used (%u KB free)\n", 
                 health.flashUsagePercent, (flashSize - programSize) / 1024);
    Serial.printf("SPIFFS    : %.1f%% used (%u KB free)\n", 
                 health.spiffsUsagePercent, (totalBytes - usedBytes) / 1024);
    Serial.printf("CPU Temp  : %.1f°C\n", health.temperature);
    Serial.println(F("=============================\n"));
    
    // Optional: Log warnings for high usage
    if (health.heapUsagePercent > 80.0f) 
        Serial.println(F("WARNING: High memory usage"));
    if (health.spiffsUsagePercent > 90.0f) 
        Serial.println(F("WARNING: High SPIFFS usage"));
    if (health.flashUsagePercent > 90.0f) 
        Serial.println(F("WARNING: High flash usage"));
    if (health.temperature > 60.0f) 
        Serial.println(F("WARNING: High CPU temperature"));
}







//===============================================================
// Parse Track Data from Array Format helper function
//===============================================================
bool parseTrackData(JsonArray trackArray, JsonObject& trackData, const String& targetTrack = "") {
    if (!trackArray || trackArray.size() < 6) return false;
    
    String trackKey = trackArray[0].as<String>();
    if (targetTrack.length() > 0 && trackKey != targetTrack) return false;
    
    // Marker data
    trackData["Marker"]["LC"] = trackArray[1];
    trackData["Marker"]["sNum"] = trackArray[2];
    
    // Audio data
    JsonArray audioArray = trackArray[3];
    if (audioArray && audioArray.size() >= 4) {
        trackData["audio"]["melody"] = audioArray[0];
        trackData["audio"]["atos"] = audioArray[1];
        trackData["audio"]["dC"] = audioArray[2];
        trackData["audio"]["va"] = audioArray[3];
    }
    
    // Direction data
    JsonArray prevArray = trackArray[4];
    if (prevArray && prevArray.size() >= 2) {
        trackData["Dir"]["p"]["Ja"] = prevArray[0];
        trackData["Dir"]["p"]["En"] = prevArray[1];
    }
    
    JsonArray nextArray = trackArray[5];
    if (nextArray && nextArray.size() >= 2) {
        trackData["Dir"]["n"]["Ja"] = nextArray[0];
        trackData["Dir"]["n"]["En"] = nextArray[1];
    }
    
    return true;
}

// Helper function to find track data in station
bool findTrackData(JsonObject stationData, const String& track, JsonObject& trackData) {
    if (!stationData.containsKey("t")) return false;
    
    JsonArray tracks = stationData["t"];
    if (!tracks || tracks.size() == 0) return false;
    
    for (JsonArray trackArray : tracks) {
        if (parseTrackData(trackArray, trackData, track)) {
            return true;
        }
    }
    
    return false;
}

// Helper function to get first track from station
bool getFirstTrackData(JsonObject stationData, String& trackKey, JsonObject& trackData) {
    if (!stationData.containsKey("t")) return false;
    
    JsonArray tracks = stationData["t"];
    if (!tracks || tracks.size() == 0) return false;
    
    JsonArray firstTrack = tracks[0];
    if (!firstTrack || firstTrack.size() < 1) return false;
    
    trackKey = firstTrack[0].as<String>();
    return parseTrackData(firstTrack, trackData);
}

// Helper function to validate and get station data
bool getStationData(const String& line, const String& station, JsonObject& stationData, JsonObject& style) {
    if (!configLoaded || !stationConfig.containsKey("lines")) return false;
    
    JsonObject lines = stationConfig["lines"];
    if (!lines.containsKey(line)) return false;
    
    JsonObject lineObj = lines[line];
    if (!lineObj.containsKey("stations") || !lineObj.containsKey("style")) return false;
    
    if (!lineObj["stations"].containsKey(station)) return false;
    
    stationData = lineObj["stations"][station];
    style = lineObj["style"];
    return true;
}



//===============================================================
// JR-Beru Shell Command handler
//===============================================================
void processCommand() {
    serialCommand.trim();  // Remove leading/trailing whitespace
    
    // Always print a new line and prompt for empty commands
    if (serialCommand.length() == 0) {
        Serial.print(PROMPT);
        return;
    }
    
    // Convert command to lowercase for case-insensitive comparison
    String cmd = serialCommand;
    cmd.toLowerCase();
    
    // Split command into parts (for commands with parameters)
    int spaceIndex = cmd.indexOf(' ');
    String command = (spaceIndex == -1) ? cmd : cmd.substring(0, spaceIndex);
    String param = (spaceIndex == -1) ? "" : cmd.substring(spaceIndex + 1);
    
    if (command == "health") {
        checkSysHealth();
    } 
    else if (command == "help") {
        Serial.println(F("\n============= JR-Beru Shell Commands ============="));
        Serial.println(F("health             - Display system health information"));
        Serial.println(F("status             - Display current station and playback status"));
        Serial.println(F("volume [0-30]      - Get or set volume level"));
        Serial.println(F("play melody/atos/chime/va - Play current melody/atos/chime/va"));
        Serial.println(F("ls /               - List files on SPIFFS"));
        Serial.println(F("ls stations        - List available stations"));
        Serial.println(F("station [line] [station] [track] - Set current station"));
        Serial.println(F("reset wifi/player  - Reset the WiFi or DFPlayer"));
        Serial.println(F("reboot             - Restart ESP32"));
        Serial.println(F("help               - Display help"));
        Serial.println(F("================================================="));
    }
    else if (command == "status") {
        Serial.println(F("\n======== Current Status ========"));
        Serial.printf_P(PSTR("Current Line: %s\n"), currentStation.stationCodeLine);
        Serial.printf_P(PSTR("Current Station: %s (%s)\n"), currentStation.stationNameJa, currentStation.stationNameEn);
        Serial.printf_P(PSTR("Current Track: %s\n"), currentStation.trackKey);
        Serial.printf_P(PSTR("Volume: %d/30\n"), globalVolume);
        Serial.printf_P(PSTR("WiFi Status: %s\n"), WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf_P(PSTR("IP Address: %s\n"), WiFi.localIP().toString().c_str());
        }
        Serial.println(F("==============================="));
    }
    else if (command == "volume") {
        if (param.length() > 0) {
            // Set volume
            int newVolume = param.toInt();
            if (newVolume >= 0 && newVolume <= 30) {
                globalVolume = newVolume;
                myDFPlayer.volume(globalVolume);
                Serial.printf("Volume set to %d\n", globalVolume);
                saveDeviceState(); // Save the new volume setting
            } else {
                Serial.println(F("Invalid volume level. Use a value between 0-30."));
            }
        } else {
            // Get current volume
            Serial.printf("Current volume: %d/30\n", globalVolume);
        }
    }
    else if (command == "play") {
        if (param == "melody") {
            Serial.printf_P(PSTR("Playing melody %d\n"), currentMelody);
            myDFPlayer.playFolder(MelodyFolder, currentMelody);
        } 
        else if (param == "atos") {
            Serial.printf_P(PSTR("Playing ATOS announcement %d\n"), currentAtos);
            myDFPlayer.playFolder(AtosFolder, currentAtos);
        }
        else if (param == "chime") {
            Serial.printf_P(PSTR("Playing door chime %d\n"), currentDoorChime);
            myDFPlayer.playFolder(DoorChimeFolder, currentDoorChime);
        }
        else if (param == "va") {
            Serial.printf_P(PSTR("Playing platform announcement %d\n"), currentVA);
            myDFPlayer.playFolder(VAFolder, currentVA);
        }
        else {
            Serial.println(F("Invalid play parameter. Use: melody, atos, chime, or va"));
        }
    }
    else if (command == "ls") {
        if (param == "/") {
            Serial.println(F("\n======== SPIFFS Files ========"));
            File root = SPIFFS.open("/");
            File file = root.openNextFile();
            int fileCount = 0;
            
            while (file) {
                String fileName = file.name();
                size_t fileSize = file.size();
                Serial.printf_P(PSTR("%s (%d bytes)\n"), fileName.c_str(), fileSize);
                file = root.openNextFile();
                fileCount++;
            }
            
            if (fileCount == 0) {
                Serial.println(F("No files found"));
            }
            
            Serial.println(F("=============================="));
        }
        else if (param == "stations") {
            Serial.println(F("\n==== Available Stations ===="));
            
            for (JsonPair linePair : stationConfig["lines"].as<JsonObject>()) {
                String lineCode = linePair.key().c_str();
                Serial.printf_P(PSTR("Line: %s\n"), lineCode.c_str());
                
                // Iterate through stations in this line
                JsonObject stations = linePair.value()["stations"];
                for (JsonPair stationPair : stations) {
                    String stationName = stationPair.key().c_str();
                    JsonObject stationData = stationPair.value();
                    
                    // Get station info from i array
                    JsonArray stationInfo = stationData["i"];
                    String stationJa = stationInfo[1].as<String>();  // Ja is at index 1
                    String stationCode = stationInfo[0].as<String>(); // sC is at index 0
                    
                    // List tracks for this station from t array
                    JsonArray tracks = stationData["t"];
                    int trackCount = tracks.size();
                    String trackList = "";
                    
                    for (JsonArray trackArray : tracks) {
                        int trackNum = trackArray[0].as<int>();
                        if (trackList.length() > 0) trackList += ", ";
                        trackList += "track" + String(trackNum);
                    }
                    
                    Serial.printf_P(PSTR("  - %s (%s, %s) - Tracks: %s\n"), 
                        stationName.c_str(), 
                        stationJa.c_str(),
                        stationCode.c_str(),
                        trackList.c_str());
                }
                Serial.println();
            }
            
            Serial.println(F("=================================="));
        }
        else {
            Serial.println(F("Invalid ls parameter. Use: / or stations"));
        }
    }
    else if (command == "station") {
        // Parse parameters: line station track
        int firstSpace = param.indexOf(' ');
        if (firstSpace == -1) {
            Serial.println(F("Invalid format. Use: station [line] [station] [track]"));
            Serial.print(PROMPT);
            return;
        }
        
        String line = param.substring(0, firstSpace);
        param = param.substring(firstSpace + 1);
        
        int secondSpace = param.indexOf(' ');
        if (secondSpace == -1) {
            Serial.println(F("Invalid format. Use: station [line] [station] [track]"));
            Serial.print(PROMPT);
            return;
        }
        
        String station = param.substring(0, secondSpace);
        String track = param.substring(secondSpace + 1);
        
        // Get station data and validate
        JsonObject stationData;
        JsonObject style;
        if (!getStationData(line, station, stationData, style)) {
            Serial.printf_P(PSTR("Invalid station data for line '%s', station '%s'\n"), line.c_str(), station.c_str());
            Serial.print(PROMPT);
            return;
        }
        
        // Create a temporary track object
        StaticJsonDocument<512> trackDoc;
        JsonObject trackData = trackDoc.to<JsonObject>();
        
        if (!findTrackData(stationData, track, trackData)) {
            Serial.printf_P(PSTR("Track '%s' not found in station '%s'\n"), track.c_str(), station.c_str());
            Serial.print(PROMPT);
            return;
        }
        
        updateCurrentStation(stationData, trackData, style, line.c_str(), station.c_str(), track.c_str());
        saveDeviceState();
        
        Serial.printf_P(PSTR("Station set to: %s, %s, %s\n"), line.c_str(), station.c_str(), track.c_str());
    }
    else if (command == "reset") {
        if (param == "player") {
            Serial.println(F("Resetting DFPlayer Mini..."));
            myDFPlayer.reset();
            delay(1000);
            myDFPlayer.volume(globalVolume);
            Serial.println(F("DFPlayer reset complete."));
        }
        else if (param == "wifi") {
            Serial.println(F("Resetting WiFi settings..."));
            WiFiManager wm;
            wm.resetSettings();
            Serial.println(F("WiFi settings reset. Device will restart..."));
            delay(1000);
            ESP.restart();
        }
        else {
            Serial.println(F("Invalid reset parameter. Use: player or wifi"));
        }
    }
    else if (command == "reboot") {
        Serial.println(F("Rebooting ESP32..."));
        delay(1000);
        ESP.restart();
    }
    else {
        Serial.print(F("Unknown command: "));
        Serial.println(serialCommand);
        Serial.println(F("Type 'help' for available commands"));
    }
    
    // Reset for next command
    serialCommand = "";
    commandComplete = false;
    
    // Show prompt
    Serial.print(PROMPT);
}



