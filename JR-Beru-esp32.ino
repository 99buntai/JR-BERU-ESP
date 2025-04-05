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
#define BUTTON_PIN 4  // GPIO4 for the switch button
HardwareSerial dfPlayerSerial(1); // Use ESP32's second UART
#define FPSerial dfPlayerSerial

DFRobotDFPlayerMini myDFPlayer;
void printDetail(uint8_t type, int value);

#define CONFIG_ASYNC_TCP_RUNNING_CORE 0 // Use core 0 for AsyncTCP
#define CONFIG_ASYNC_TCP_USE_WDT 0 // Disable WDT for AsyncTCP

//================================
// Global Configuration Constants
//================================
#define FW_VERSION      "ESP32-MCU-R0.4.16 WebUI-R3.2.8"  // Current firmware version

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
//current audio config
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

//HTTP Logins unused for now
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

String currentConfigFilename = "station_config.json";//set default config filename

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


//===============================================================
// WiFi and Server Setup
//===============================================================
void saveConfigCallback() {shouldSaveConfig = true;}
void setupWiFiManager() {
  WiFiManager wifiManager;

  // Configure timeouts
  wifiManager.setConfigPortalTimeout(1800); // Portal timeout in seconds
  wifiManager.setConnectTimeout(30); // Connection attempt timeout
  
  // Configure AP settings
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Set WiFi mode to station mode
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


//===============================================================
//  void setup()
//===============================================================

void setup() {
  dfPlayerSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(600);
  Serial.println(F("\n\n\n\n\n\n\n\n\n"));
  Serial.println(F("================================="));
  Serial.println(F("|        JR-Beru Booting        |"));
  Serial.println(F("================================="));
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

  // Create button handler task on Core 1
  xTaskCreatePinnedToCore(
    buttonHandlerTask,
    "buttonHandler",
    8192,  // Stack size
    NULL,
    22,     // Priority
    NULL,
    1  // runs on core
  );

  // Start WiFi setup
  setupWiFiManager();

  // Only initialize web server if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    ElegantOTA.setAuth("JR", "BERU");
    setupOTA();
    setupWebServer();
  }

    // Play random melody as boot-up sound
  PlayRandomAudio(MelodyFolder,MelodyCount);delay(900);myDFPlayer.disableLoop();
  
  //print boot up completed in serial monitor
  Serial.println(F("\n========Boot up Completed!========\n"));

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
      1,//
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
  // ---- Handle serial input for the shell ----
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
//------------------------------------------------


  // Only handle web-related tasks if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    ElegantOTA.loop();
    checkWiFiConnection();
  }
  
  // Small delay to prevent watchdog issues
  delay(1);
}

//===============================================================
//  Construct the HTML Page
//===============================================================
void handleRoot() {
  String html = "<html><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "* {box-sizing:border-box;margin:0;padding:0}"
                "body {font:400 1em/1.6 'Segoe UI',Roboto,Arial,sans-serif;background:#f5f5f5;color:#333}"
                
                // Layout elements
                ".navbar {position:fixed;top:0;left:0;right:0;z-index:1000;background:#2c3e50;color:#fff;padding:8px;text-align:center}"
                ".navbar h1 {font-size:1.5rem;font-weight:500;margin:0}"
                ".container {max-width:800px;margin:80px auto 100px;padding:20px}"
                ".panel {background:#fff;border-radius:10px;padding:15px;margin-bottom:15px;box-shadow:0 2px 4px rgba(0,0,0,.05)}"
                ".panel-header {font-size:1.2rem;color:#2c3e50;margin-bottom:12px;font-weight:700}"
                ".footer {position:fixed;bottom:0;left:0;right:0;background:#2c3e50;color:#fff;text-align:center;padding:1rem;font-size:.8rem}"
                ".version-info {text-align:center;color:#666;font-size:.8rem;margin-top:20px}"
                
                // Controls
                ".control-group {display:flex;align-items:center;margin-bottom:12px;gap:10px}"
                ".control-label {flex:1;font-size:.95rem;color:#555}"
                ".control-item {display:flex;align-items:center;gap:10px}"
                ".control-item.right {justify-content:flex-end}"
                ".control-item label {font-size:.95rem;color:#555}"
                
                // Form elements
                "select {flex:0 0 80px;padding:8px;border:1px solid #ddd;border-radius:6px;background:#fff;font-size:.9rem;color:#333}"
                ".btn {background:#2196F3;color:#fff;border:none;padding:10px 15px;border-radius:6px;cursor:pointer;font-size:.9rem;transition:all .3s;display:flex;align-items:center;justify-content:center}"
                ".btn:hover {filter:brightness(1.1)}"
                ".btn-compact {padding:6px 12px;font-size:.85rem}"
                ".btn-primary {background:#2196F3}"
                ".btn-secondary {background:#757575}"
                ".btn-warning {background:#e84636}"
                
                // Toggle and volume controls
                ".toggle-switch {position:relative;display:inline-block;width:40px;height:22px}"
                ".toggle-switch input {opacity:0;width:0;height:0}"
                ".slider {position:absolute;cursor:pointer;inset:0;background:#ccc;transition:.4s;border-radius:34px}"
                ".slider:before {position:absolute;content:'';height:16px;width:16px;left:3px;bottom:3px;background:#fff;transition:.4s;border-radius:50%}"
                "input:checked+.slider {background:#2196F3}"
                "input:checked+.slider:before {transform:translateX(18px)}"
                ".volume-slider {flex:1;-webkit-appearance:none;height:5px;border-radius:5px;background:#ddd;outline:0}"
                ".volume-slider::-webkit-slider-thumb {-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:#2196F3;cursor:pointer}"
                ".volume-container {grid-column:1/-1;display:flex;align-items:center;gap:15px}"
                
                // Station sign
                ".station-sign {background:linear-gradient(180deg,#fff 0%,#f0f0f0 100%);margin:0 0 20px;padding:15px 15px 0;position:relative;border-radius:10px;box-shadow:0 2px 4px rgba(0,0,0,.05);overflow:hidden}"
                ".station-content {position:relative;width:100%;display:flex;justify-content:center;max-width:800px;margin:0 auto;padding:0 60px}"
                
                // Line marker
                ".line-marker {position:absolute;top:0;transform:translateX(calc(-100% - 10px));width:48px;height:66px;background:" + currentStation.lineMarkerBgColor + ";border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,.2)}"
                ".markerStationCode,.markerLineCode {font-weight:700;font-size:14px}"
                ".markerStationCode {position:absolute;top:0;left:0;right:0;color:#fff;text-align:center}"
                ".markerLineCode {color:#000;line-height:1;margin-bottom:2px}"
                
                // Station number
                ".station-number-container {position:absolute;top:20px;inset-inline:3px;bottom:3px;background:" + currentStation.lineNumberBgColor + ";border-radius:5px;padding:3.7px}"
                ".station-number-inner {background:#fff;border-radius:2px;height:100%;width:100%;display:flex;flex-direction:column;justify-content:center;align-items:center}"
                ".line-number {color:#000;font-weight:700;font-size:19px;line-height:.7}"
                
                // Station info
                ".station-info {position:relative;display:flex;flex-direction:column;align-items:center;width:auto;min-width:200px;margin:0 auto}"
                ".staName-ja,.staName-hiragana,.staName-ko {text-align:center}"
                ".staName-ja {position:relative;display:inline-block;font-size:42px;font-weight:700;line-height:1.2;font-family:'MS Gothic','Yu Gothic',sans-serif;letter-spacing:6px}"
                ".staName-hiragana {font-size:16px;color:#333;font-weight:700}"
                ".staName-ko {font-size:14px;color:#666;margin-bottom:6px}"
                
                // Ward label
                ".ward-label {position:absolute;top:15px;right:37px;display:flex;gap:1px}"
                ".ward-box,.ward-text {width:16px;height:16px;border-radius:2px;display:flex;align-items:center;justify-content:center;font-size:12px}"
                ".ward-box {background:#fff;color:#000;border:1px solid #000}"
                ".ward-text {background:#000;color:#fff}"
                
                // Direction elements
                ".direction-bar {background:" + currentStation.directionBarBgColor + ";height:32px;display:flex;justify-content:space-between;align-items:center;color:#fff;position:relative;clip-path:polygon(0 0,calc(100% - 15px) 0,100% 50%,calc(100% - 15px) 100%,0 100%);overflow:visible}"
                ".station-indicator {position:absolute;top:0;left:50%;transform:translateX(-50%);width:32px;height:32px;background:" + currentStation.lineNumberBgColor + "}"
                
                // Direction stations
                ".direction-left,.direction-right {width:33%}"
                ".direction-left {font-weight:700;padding-left:15px;text-align:left}"
                ".direction-right {font-weight:700;padding-right:20px;text-align:right}"
                ".direction-station {font-size:20px;font-weight:700}"
                
                // English station names
                ".staName-en {font-size:16px;color:#333}"
                ".staName-en.current {font-weight:700}"
                ".staName-en-left {width:33%;text-align:left}"
                ".staName-en-center {width:33%;text-align:center}"
                ".staName-en-right {width:33%;text-align:right}"
                ".station-names-container {display:flex;justify-content:space-between;padding:5px 15px}"
                
                // Shared elements
                ".direction-bar,.station-names-container {width:100%;max-width:700px;margin:0 auto}"
                
                // Grid layouts
                ".controls-grid,.button-group,.station-selectors {display:grid;gap:12px}"
                ".controls-grid {margin:0}"
                ".button-group {grid-template-columns:repeat(3,1fr);gap:8px}"
                ".station-selectors {grid-template-columns:repeat(3,1fr);gap:8px}"
                
                // Miscellaneous
                ".config-info {font-size:.85rem;color:#666;background:#f5f5f5;border-radius:4px}"
                ".sequence-controls {display:grid;grid-template-columns:2fr 1fr;gap:8px}"
                ".sequence-buttons {display:flex;justify-content:flex-end}"
                ".play-button-container {display:flex;justify-content:flex-end;grid-column:2/3}"
                
                // Media queries
                "@media (max-width:800px){"
                ".container{padding:10px;margin-top:70px}"
                ".panel{padding:12px}"
                ".control-group{margin-bottom:8px;gap:8px}"
                ".control-label{font-size:.9rem;flex:0 0 110px}"
                ".btn-compact,select{width:100%}"
                ".btn-compact{padding:8px;font-size:.8rem}"
                ".controls-grid{gap:8px}"
                ".station-selectors{grid-template-columns:1fr}"
                "}"
                
                "</style></head><body>"
                
                "<div class='navbar'><h1>" + 
                String("発車ベル") + // 発車ベル
                "</h1></div>"
                "<div class='container'>";

  // Add Station Sign
  html += "<div class='station-sign'>"
          "<div class='station-content'>"
          "<div class='line-marker'>"
          "<div class='markerStationCode'>" + currentStation.lineCode + "</div>"
          "<div class='station-number-container'>"
          "<div class='station-number-inner'>"
          "<div class='markerLineCode'>" + currentStation.stationCodeLine + "</div>"
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

  // Sequence Control panel
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

  // System Controls Panel
  html += "<div class='panel'>"
          "<div class='panel-header'>System Controls</div>"
          "<div class='controls-grid'>"
          
          // Buttons in one row
          "<div class='control-item right' style='display:flex;gap:10px;'>"
          "<button class='btn btn-warning' onclick='reinitDFPlayer()'>Reinit DFPlayer</button>"
          "<button class='btn btn-primary' onclick='showSystemInfo()'>System Info</button>"
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
          String("発車ベル") + // 発車ベル
          "</div>";

  // Reusable modal
  html += "<div id='appModal' class='modal' style='display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;overflow:auto;background-color:rgba(0,0,0,0.4);'>"
          "<div class='modal-content' style='background-color:#fff;margin:10% auto;padding:20px;border-radius:10px;width:80%;max-width:600px;box-shadow:0 4px 8px rgba(0,0,0,0.2);'>"
          "<span class='close-button' style='color:#aaa;float:right;font-size:28px;font-weight:bold;cursor:pointer;' onclick='closeModal()'>&times;</span>"
          "<h2 id='modalTitle' style='margin-top:0;color:#2c3e50;'></h2>"
          "<div id='modalContent' style='max-height:400px;overflow-y:auto;'></div>"
          "</div>"
          "</div>";

  // JavaScript
  html += "<script>"
          // Core utilities and DOM helpers
          "const $ = id => document.getElementById(id), $q = (sel, p) => (p || document).querySelector(sel), $all = (sel, p) => (p || document).querySelectorAll(sel);"
          "const setEl = (sel, prop, val) => { const el = typeof sel === 'string' ? $q(sel) : sel; if(el) el[prop] = val || ''; };"
          "const setStyle = (sel, prop, val) => { const el = $q(sel); if(el) el.style[prop] = val; };"
          
          // Modal functions
          "const showModal = (title, content) => {"
          "  document.getElementById('modalTitle').textContent = title;"
          "  document.getElementById('modalContent').innerHTML = content;"
          "  document.getElementById('appModal').style.display = 'block';"
          "};"

          // Close Modal
          "const closeModal = () => {"
          "  document.getElementById('appModal').style.display = 'none';"
          "};"
          
          "const msgModal = (type, msg) => showModal(type, `<div style=\"color:${type === 'Error' || type === 'Upload Failed' ? 'red' : 'green'};text-align:center;padding:10px;\">${msg}</div>`);"
          
          // Enhanced API with error handling
          "const api = (url, method = 'GET', errorTitle) => fetch(url, {method})"
          "  .then(r => (r.ok || !errorTitle) ? r : Promise.reject(`Status: ${r.status}`))"
          "  .catch(e => { if(errorTitle) { console.error(e); msgModal('Error', `${errorTitle}: ${e}`); } throw e; });"
          
          // State management
          "let cachedConfig = null, updateInProgress = false, lastStationCounter = 0;"
          
          // Core functions
          "const updateConfig = () => api(`/updateConfig?${['melody','atos','doorchime','platform'].map(id => `${id}=${$(id).value}`).join('&')}`);"
          "const reinitDFPlayer = () => { api('/reinitDFPlayer').then(() => showModal('DFPlayer', '<div style=\"text-align:center;padding:10px;\">DFPlayer reinitialization requested</div>')); };"
          "const setVolume = v => { api(`/setVolume?value=${v}`); $('volumeValue').textContent = v; };"
          "const playVA = () => api(`/playVA?mode=${$('playMode').value}`);"
          "const downloadConfig = () => { location.href = '/downloadConfig'; };"
          
          // Config file management
          "const uploadConfig = () => {"
          "  const file = $('configFile').files[0];"
          "  if(!file) return;"
          "  if(file.size > 102400) return showModal('Error', '<div style=\"color:red;text-align:center;padding:10px;\">File over max size: 100KB</div>');"
          "  const fd = new FormData();"
          "  fd.append('config', file);"
          "  fetch('/upload', {method: 'POST', body: fd})"
          "    .then(r => r.json())"
          "    .then(data => {"
          "      if(data.error) throw new Error(data.error);"
          "      showModal('Success', '<div style=\"color:green;text-align:center;padding:10px;\">Config file uploaded successfully</div>');"
          "      return forceConfigReload();"
          "    })"
          "    .catch(e => { console.error('Error:', e); showModal('Upload Failed', '<div style=\"color:red;text-align:center;padding:10px;\">' + (e.message || 'Upload failed') + '</div>'); });"
          "};"

          // Delete Config
          "const deleteConfig = () => {"
          "  if(!confirm('Delete config file?')) return;"
          "  api('/deleteConfig')"
          "    .then(() => forceConfigReload())"
          "    .then(() => showModal('Success', '<div style=\"color:green;text-align:center;padding:10px;\">Config deleted successfully</div>'))"
          "    .catch(e => showModal('Error', '<div style=\"color:red;text-align:center;padding:10px;\">Failed to delete configuration: ' + e + '</div>'));"
          "};"
          
          "const forceConfigReload = () => { cachedConfig = null; return loadConfig(); };"
          
          // Selectors management
          "const populateOptions = (items, selected) => items ? Object.keys(items).map(item => "
          "  `<option value=\"${item}\" ${item === selected ? 'selected' : ''}>${item}</option>`).join('') : '';"
          
          "const populateSelector = (id, items, selected) => {"
          "  const sel = $(id); if(!sel) return;"
          "  sel.innerHTML = `<option>${sel.options[0].text}</option>${populateOptions(items, selected)}`;"
          "};"
          
          // Station Select
          "const updateStationSelect = () => {"
          "  const line = $('lineSelect').value;"
          "  const stations = cachedConfig?.lines?.[line]?.stations;"
          "  ['stationSelect', 'trackSelect'].forEach(id => $(id).innerHTML = `<option>${id === 'stationSelect' ? 'Select Station' : 'Select Track'}</option>`);"
          "  if(line && stations) {"
          "    populateSelector('stationSelect', stations);"
          "    if($('stationSelect').options.length > 1) {"
          "      $('stationSelect').selectedIndex = 1;"
          "      updateTrackSelect();"
          "    }"
          "  }"
          "};"
          // Track Select
          "const updateTrackSelect = () => {"
          "  const line = $('lineSelect').value, station = $('stationSelect').value;"
          "  const stationData = cachedConfig?.lines?.[line]?.stations?.[station];"
          "  $('trackSelect').innerHTML = '<option>Select Track</option>';"
          "  if(stationData?.t?.length > 0) {"
          "    const tracks = {};"
          "    stationData.t.forEach(trackArray => {"
          "      if(trackArray?.length > 0) tracks[String(trackArray[0])] = String(trackArray[0]);"
          "    });"
          "    populateSelector('trackSelect', tracks);"
          "    if($('trackSelect').options.length > 1) {"
          "      $('trackSelect').selectedIndex = 1;"
          "      updateStationSign();"
          "    }"
          "  }"
          "};"
          
          // Station sign update
          "const updateLineMarkerPosition = () => {"
          "  const stationNameJa = $q('.staName-ja'), lineMarker = $q('.line-marker'), stationContent = $q('.station-content');"
          "  if(!stationNameJa || !lineMarker || !stationContent) return;"
          "  const stationCode = $q('.markerStationCode');"
          "  if(stationCode && (!stationCode.textContent || stationCode.textContent.trim() === '')) {"
          "    lineMarker.style.backgroundColor = 'transparent';"
          "    lineMarker.style.boxShadow = 'none';"
          "    lineMarker.style.transform = 'translate(-55px, -16px)';"
          "  } else {"
          "    lineMarker.style.backgroundColor = lineMarker.style.boxShadow = lineMarker.style.transform = '';"
          "  }"
          "  lineMarker.style.left = (stationNameJa.getBoundingClientRect().left - stationContent.getBoundingClientRect().left) + 'px';"
          "};"

          // Update Station Sign
          "const updateStationSign = () => {"
          "  if(updateInProgress) return;"
          "  updateInProgress = true;"
          "  const line = $('lineSelect').value, station = $('stationSelect').value, track = $('trackSelect').value;"
          "  if(!line || !station || !track || track === 'Select Track') { updateInProgress = false; return; }"
          
          "  const selectors = ['.line-marker','.station-number-container','.station-indicator','.direction-bar',"
          "    '.staName-ja','.staName-hiragana','.staName-ko','.ward-box','.markerStationCode','.markerLineCode','.line-number',"
          "    '.direction-left .direction-station','.direction-right .direction-station',"
          "    '.staName-en-left','.staName-en-center','.staName-en-right'];"
          
          "  if(!selectors.every(sel => $q(sel))) {"
          "    setTimeout(() => { updateInProgress = false; updateStationSign(); }, 100);"
          "    return;"
          "  }"

          // Update Station Sign
          "  api(`/updateStationSign?line=${line}&station=${station}&track=${track}`)"
          "    .then(() => {"
          "      if(!cachedConfig) return;"
          "      const stationData = cachedConfig.lines[line].stations[station];"
          "      if(!stationData) return;"
          "      const style = cachedConfig.lines[line].style;"
          "      const trackData = stationData.t?.find(t => t?.length > 0 && String(t[0]) === String(track));"
          "      if(!trackData) return;"
          
          "      ['.line-marker', '.station-number-container', '.station-indicator', '.direction-bar'].forEach((sel, i) => "
          "        setStyle(sel, 'backgroundColor', [style.lineMarkerBgColor, style.lineNumberBgColor, style.lineNumberBgColor, style.directionBarBgColor][i]));"
          // Update Line Marker Position
          "      setTimeout(updateLineMarkerPosition, 10);"
          // Update Audio Selection 
          "      ['melody', 'atos', 'doorchime', 'platform'].forEach((id, i) => "
          "        setEl($(id), 'value', trackData[3]?.[i] || 1));"
          // Update Station Info
          "      const stInfo = stationData.i || [];"
          "      ['.staName-ja','.staName-hiragana','.staName-ko','.ward-box']"
          "        .forEach((sel, i) => setEl(sel, 'textContent', stInfo[i+1] || ''));"
          "      setEl('.markerStationCode', 'textContent', stInfo[0] || '');"
          
          "      setEl('.markerLineCode', 'textContent', trackData[1]);"
          "      setEl('.line-number', 'textContent', trackData[2]);"
          "      setEl('.direction-left .direction-station', 'textContent', trackData[4]?.[0] || '');"
          "      setEl('.direction-right .direction-station', 'textContent', trackData[5]?.[0] || '');"
          "      setEl('.staName-en-left', 'textContent', trackData[4]?.[1] || '');"
          "      setEl('.staName-en-center', 'textContent', station);"
          "      setEl('.staName-en-right', 'textContent', trackData[5]?.[1] || '');"
          
          "      updateConfig();"
          "    })"
          "    .catch(() => setTimeout(updateStationSign, 500))"
          "    .finally(() => { updateInProgress = false; });"
          "};"
          
          // Data loading
          "const loadConfig = () => {"
          "  cachedConfig = null;"
          "  ['lineSelect','stationSelect','trackSelect'].forEach(id => "
          "    $(id).innerHTML = `<option>${id === 'lineSelect' ? 'Select Line' : id === 'stationSelect' ? 'Select Station' : 'Select Track'}</option>`);"
          // Load Config
          "  Promise.all([api('/getCurrentConfig').then(r => r.json()), api('/getStationConfig').then(r => r.json())])"
          "    .then(([configInfo, config]) => {"
          "      $('configName').textContent = configInfo.filename;"
          "      cachedConfig = JSON.parse(JSON.stringify(config));"
          "      $('playMode').value = configInfo.playbackMode;"
          // Update Volume
          "      if(configInfo.volume !== undefined) {"
          "        $('volumeControl').value = $('volumeValue').textContent = configInfo.volume;"
          "      }"
          // Populate Line Select
          "      populateSelector('lineSelect', config.lines, null);"
          "      return api('/getCurrentSelections').then(r => r.json());"
          "    })"
          "    .then(sel => {"
          "      if(!sel?.line || !sel?.station || !sel?.track || !cachedConfig?.lines?.[sel.line]) return;"
          "      setEl($('lineSelect'), 'value', sel.line);"
          "      populateSelector('stationSelect', cachedConfig.lines[sel.line].stations, sel.station);"
          "      setEl($('stationSelect'), 'value', sel.station);"
          // Populate Station Select
          "      const stationData = cachedConfig.lines[sel.line].stations[sel.station];"
          "      if(stationData?.t?.length > 0) {"
          "        const tracks = {};"
          "        stationData.t.forEach(trackArray => {"
          "          if(trackArray?.length > 0) tracks[trackArray[0]] = trackArray[0];"
          "        });"
          "        populateSelector('trackSelect', tracks, sel.track);"
          "        setEl($('trackSelect'), 'value', sel.track);"
          "        updateStationSign();"
          "      }"
          "    })"
          "    .catch(e => { console.error('Error loading config:', e); cachedConfig = null; showModal('Error', '<div style=\"color:red;text-align:center;padding:10px;\">Failed to load configuration</div>'); });"
          "};"
          
          // Station change polling
          "const pollStationChanges = () => {"
          "  api(`/checkStationChanged?counter=${lastStationCounter}`)"
          "    .then(r => r.json())"
          "    .then(data => {"
          "      lastStationCounter = data.counter;"
          "      if(!data.changed) return;"
          // Update Line Select
          "      if($('lineSelect').value !== data.line) {"
          "        setEl($('lineSelect'), 'value', data.line);"
          "        populateSelector('stationSelect', cachedConfig.lines[data.line].stations, data.station);"
          "      }"
          // Populate Station Select
          "      setEl($('stationSelect'), 'value', data.station);"
          "      const stationData = cachedConfig.lines[data.line].stations[data.station];"
          // Populate Track Select
          "      if(stationData?.t?.length > 0) {"
          "        const tracks = {};"
          "        stationData.t.forEach(trackArray => {"
          "          if(trackArray?.length > 0) tracks[trackArray[0]] = trackArray[0];"
          "        });"
          "        populateSelector('trackSelect', tracks, data.track);"
          "        setEl($('trackSelect'), 'value', data.track);"
          
          "        api(`/updateStationSign?line=${data.line}&station=${data.station}&track=${data.track}`)"
          "          .then(() => updateStationSign())"
          "          .catch(error => console.error('Error updating station sign:', error));"
          "      }"
          "    })"
          "    .catch(e => console.error('Error checking station changes:', e));"
          "};"
          
          // Initialize everything
          "window.onclick = (event) => {"
          "  const modal = document.getElementById('appModal');"
          "  if (event.target === modal) {"
          "    closeModal();"
          "  }"
          "};"
          
          "const init = () => {"
            // Set up event listeners
          "  $('configFile').addEventListener('change', uploadConfig);"
          "  $('playMode').addEventListener('change', () => {"
          "    api(`/setPlaybackMode?mode=${$('playMode').value}`)"
          "      .then(r => r.ok ? r.text() : Promise.reject('Failed to set mode'))"
          "      .catch(e => { console.error('Error:', e); msgModal('Error', 'Failed to set playback mode'); });"
          "  });"
          // Update Station Select
          "  $('lineSelect').addEventListener('change', updateStationSelect);"
          "  $('stationSelect').addEventListener('change', () => { updateTrackSelect(); updateStationSign(); });"
          "  $('trackSelect').addEventListener('change', updateStationSign);"
          "  window.addEventListener('load', () => setTimeout(updateLineMarkerPosition, 10));"
          
            // Initialize data and polling
          "  loadConfig();"
          "  setInterval(pollStationChanges, 5000);"
          "};"

          "init();"

          // Show System Info Modal
          "const showSystemInfo = () => {"
          "  api('/getSystemInfo')"
          "    .then(r => r.text())"
          "    .then(html => showModal('System Information', html))"
          "    .catch(e => { console.error('Error fetching system info:', e); msgModal('Error', 'Failed to fetch system information'); });"
          "};"
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
  Serial.println(F("\n=== Indexing Audio File ==="));

  struct FolderInfo {
    const char* name;
    int folder;
    int* countVar;
  };
  
  const FolderInfo folders[] = {
    {"Melody", MelodyFolder, &MelodyCount},
    {"Atos", AtosFolder, &AtosCount},
    {"DoorChime", DoorChimeFolder, &DoorChimeCount},
    {"VA", VAFolder, &VACount}
  };
  
  for (const auto& folder : folders) {
    *folder.countVar = indexFileInFolder(folder.folder);
    Serial.printf("%s Files: %d\n", folder.name, *folder.countVar);
  }
  
  Serial.println(F("===========================\n"));
}

//===============================================================
// Play Audio - Consolidated function for both random and specific tracks
//===============================================================

void PlayAudio(int folder, int audioIndex, bool randomPlay, int totalCount) {
  int trackToPlay = randomPlay ? generateRandomNumber(1, totalCount) : audioIndex;
  myDFPlayer.playFolder(folder, trackToPlay);
  
  Serial.printf_P(PSTR("PlayingFolder: %d\n"), folder);
  if (randomPlay) {
    Serial.printf_P(PSTR("RandomPlay: %d/%d\n"), trackToPlay, totalCount);
  } else {
    Serial.printf_P(PSTR("Playing Audio: %d\n"), trackToPlay);
  }
  
  delay(300); // Waiting for mp3 board
}

// Legacy wrapper functions that call the consolidated function
void PlayRandomAudio(int folder, int totalCount) {
  PlayAudio(folder, 0, true, totalCount);
}

//===============================================================
// Generate Random Number
//===============================================================
int generateRandomNumber(int min, int max) {
  return random(min, max + 1); // The +1 ensures that the upper bound is inclusive
}



  //===============================================================
  // Index File In Folder (with error correction because of DFPlayer bug)
  //===============================================================

int indexFileInFolder(int folder) {
    int fileCount = 0;
    const int MAX_RETRIES = 3;

    for (int i = 0; i < MAX_RETRIES; i++) {
        int first = myDFPlayer.readFileCountsInFolder(folder);
        int second = myDFPlayer.readFileCountsInFolder(folder);
        if (first > 0 && first == second) return first;  // Return if both match
        fileCount = first > 0 ? first : second;  // Store the last valid value
    }
    return fileCount > 0 ? fileCount : 1;  // Return last valid count or 1
}



//===============================================================
// Main Button Handler
//===============================================================

void handleButton() {
    unsigned long currentTime = millis();
    int reading = digitalRead(BUTTON_PIN);

    // Update debounce timer if button state changed
    if (reading != lastMainButtonState) {lastDebounceTime = currentTime;}

    // Process button state change after debounce period
    if ((currentTime - lastDebounceTime) > debounceDelay && reading != MainButtonState) {
      MainButtonState = reading;

      if (MainButtonState == LOW) {
        Serial.println(F("\n==Button Pressed=="));
        if (!loopPlaying) {
          Serial.printf("Playback Mode: %s\n", playbackMode.c_str());
          handlePlayMelody(); delay(1000);myDFPlayer.enableLoop();
          loopPlaying = true;
          AtosLastPlayed = false;
        }
      } else if (loopPlaying) {
        Serial.println(F("\n==Button Released=="));
        //myDFPlayer.disableLoop();// bug fix, disable loop twice because DFPlayer is unreliable
        loopPlaying = false;
        handlePlayAtos();delay(1000);myDFPlayer.disableLoop();
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
// Play Audio Handlers
//===============================================================

// Simplified audio handlers using the consolidated PlayAudio function
void handlePlayMelody() { 
  DonePlaying = false;
  PlayAudio(MelodyFolder, currentMelody, RandomPlayOn, MelodyCount); 
}

void handlePlayAtos() { 
  DonePlaying = false;
  PlayAudio(AtosFolder, currentAtos, RandomPlayOn, AtosCount); 
}

void handlePlayVA() { 
  DonePlaying = false;
  PlayAudio(VAFolder, currentVA, RandomPlayOn, VACount); 
}

// Optimized door chime function
void DoorChime() {
  if (DonePlaying && AtosLastPlayed) {
    Serial.println(F("\n====Door Chime playing===="));
    DonePlaying = AtosLastPlayed = false;
    PlayAudio(DoorChimeFolder, currentDoorChime, RandomPlayOn, DoorChimeCount);
    if (playbackMode == "sequence" && !sequenceInProgress) {
      triggerSequencePlay();
    }
  }
}


//Trigger sequence play via the queue
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

//Put the DFPlayer to sleep function
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
  static unsigned long lastCheck = 0;
  unsigned long currentMillis = millis();
  // Only check status every 150ms to reduce overhead
  if (currentMillis - lastCheck >= 150) {
    lastCheck = currentMillis; 
    if (myDFPlayer.available()) {printDetail(myDFPlayer.readType(), myDFPlayer.read());}}
    }

void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      Serial.println(F("\nTime Out!"));
      break;
    case WrongStack:
      Serial.println(F("\nStack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("\nCard Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("\nCard Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("\nCard Online!"));
      break;
    case DFPlayerUSBInserted:
      Serial.println(F("\nUSB Inserted!"));
      break;
    case DFPlayerUSBRemoved:
      Serial.println(F("\nUSB Removed!"));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("\nNumber:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      DonePlaying = true;
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
          Serial.println(F("\nFile Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("\nCannot Find File"));
          break;
        case Advertise:
          Serial.println(F("\nIn Advertise"));
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
        30000,// stack size
        NULL,
        10,// priority
        NULL,
        CONFIG_ASYNC_TCP_RUNNING_CORE
    );
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
  
// Play VA server endpoint
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
      PlayAudio(VAFolder, randomTrack, false, 0);
    }
    else { 
      // "selected" mode or any other mode - just play current selection
      // Don't advance sequences from the UI button
      RandomPlayOn = false;
      PlayAudio(VAFolder, currentVA, false, 0);
    }
    
    server.send(200, "text/plain", "Playing announcement");
  });
// Set Volume server endpoint
  server.on("/setVolume", HTTP_GET, []() {
    String value = server.arg("value");
    if (value != "") {
      globalVolume = value.toInt();
      myDFPlayer.volume(globalVolume);
      Serial.print(F("Updated Volume:"));Serial.println(globalVolume);
    }
    server.send(200, "text/plain", "Volume set to " + String(globalVolume));
  });

// Update Config server endpoint
  server.on("/updateConfig", HTTP_GET, []() {
    int melodyValue = server.arg("melody").toInt();
    int atosValue = server.arg("atos").toInt();
    int doorchimeValue = server.arg("doorchime").toInt();
    int platformValue = server.arg("platform").toInt();

    currentMelody = melodyValue;
    currentAtos = atosValue;
    currentDoorChime = doorchimeValue;
    currentVA = platformValue;

    Serial.println(F("\n===Audio Selection Updated!===="));
    Serial.print(F("Melody: ")); Serial.println(currentMelody);
    Serial.print(F("Atos: ")); Serial.println(currentAtos);
    Serial.print(F("DoorChime: ")); Serial.println(currentDoorChime);
    Serial.print(F("VA: ")); Serial.println(currentVA);
    Serial.println(F("==============================="));

    server.send(200, "text/plain", "Configuration updated");
  });

// Reinitialize DFPlayer server endpoint
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
    server.send(200, "application/json", "{\"success\": true}");
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

// Delete Config server endpoint
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

// Get Station Config server endpoint
  server.on("/getStationConfig", HTTP_GET, []() {
    if (!configLoaded) loadStationConfig();
    String jsonStr;
    serializeJson(stationConfig, jsonStr);
    server.send(200, "application/json", jsonStr);
  });

  // Update Station Sign server endpoint
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

  // Get Current Config server endpoint
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

  // Get Current Selections server endpoint
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

  // Get Current Station server endpoint
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

// Set Playback Mode server endpoint
  server.on("/setPlaybackMode", HTTP_GET, []() {
    String mode = server.arg("mode");
    if (!mode.isEmpty()) {
      // Add detailed logging
      Serial.printf_P(PSTR("\n\n== PLAYBACK MODE CHANGE ==\n"));
      Serial.printf_P(PSTR("Previous mode: %s\n"), playbackMode.c_str());
      Serial.printf_P(PSTR("Current mode: %s\n"), mode.c_str());
      for (int i = 0; i < 26; i++) Serial.print("=");Serial.println("\n");// print 26 equal signs
      
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


  // Updated Get System Info endpoint that returns formatted HTML
  server.on("/getSystemInfo", HTTP_GET, []() {
    // Create formatted HTML string with monospace styling
    String html = "<div style=\"font-family:monospace;white-space:pre-wrap;\">";
    html += "Platform: " + String(ESP.getChipModel()) + "\n";
    // Memory metrics
    size_t freeHeap = ESP.getFreeHeap();
    size_t totalHeap = ESP.getHeapSize();
    float heapUsagePercent = 100.0f * (1.0f - ((float)freeHeap / totalHeap));
    html += "Memory: " + String(heapUsagePercent, 1) + "% used (" + String(freeHeap / 1024) + " KB free)\n";
    
    // Flash metrics
    uint32_t flashSize = ESP.getFlashChipSize();
    uint32_t programSize = ESP.getSketchSize();
    float flashUsagePercent = 100.0f * ((float)programSize / flashSize);
    html += "Flash: " + String(flashUsagePercent, 1) + "% used (" + String((flashSize - programSize) / 1024) + " KB free)\n";
    
    // SPIFFS metrics
    uint32_t totalBytes = SPIFFS.totalBytes();
    uint32_t usedBytes = SPIFFS.usedBytes();
    float spiffsUsagePercent = 100.0f * ((float)usedBytes / totalBytes);
    html += "SPIFFS: " + String(spiffsUsagePercent, 1) + "% used (" + String((totalBytes - usedBytes) / 1024) + " KB free)\n";
    
    // Temperature
    html += "CPU Temp: " + String(temperatureRead(), 1) + "°C\n";

    //current state
    html += "Line: " + currentStation.stationCodeLine + "\n";
    html += "Station: " + currentStation.stationNameJa + " (" + currentStation.stationNameEn + ")\n";
    html += "Track: " + currentStation.trackKey + "\n";
    html += "Audio: Melody " + String(currentMelody) + 
            ", ATOS " + String(currentAtos) + 
            ", DoorChime " + String(currentDoorChime) + 
            ", VA " + String(currentVA) + "\n";
    html += "Volume: " + String(globalVolume) + "/30\n";
    html += "WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "\n";
    
    if (WiFi.status() == WL_CONNECTED) {
      html += "IP: " + WiFi.localIP().toString() + "\n";
    }
    

    
    html += "</div>";
    
    server.send(200, "text/html", html);
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
          //check if first line is not empty
            if (!firstLine.isEmpty()) {
                // Get the first station
          String firstStation;
          JsonObject stations = lines[firstLine]["stations"];
          for (JsonPair station : stations) {
              firstStation = station.key().c_str();
              break;
          }
          //check if first station is not empty
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

    // Parse JSON directly from file stream
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
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

    // Restore state with careful error handling
    RandomPlayOn = doc["randomPlay"].isNull() ? false : doc["randomPlay"].as<bool>();
    globalVolume = doc["volume"].isNull() ? 22 : doc["volume"].as<int>();
    currentMelody = doc["melody"].isNull() ? 1 : doc["melody"].as<int>();
    currentAtos = doc["atos"].isNull() ? 1 : doc["atos"].as<int>();
    currentDoorChime = doc["doorChime"].isNull() ? 1 : doc["doorChime"].as<int>();
    currentVA = doc["va"].isNull() ? 1 : doc["va"].as<int>();

    // Apply volume
    myDFPlayer.volume(globalVolume);

    Serial.println(F("\nStored values:"));
    Serial.printf("Random Play: %s\n", doc["randomPlay"].as<bool>() ? "ON" : "OFF");
    Serial.printf("Volume: %d\n", doc["volume"].as<int>());
    Serial.printf_P(PSTR("Current Melody: %d, Current Atos: %d, Current DoorChime: %d, Current VA: %d\n"), 
                currentMelody, currentAtos, currentDoorChime, currentVA);

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
    
    for (int i = 0; i < 28; i++) Serial.print("=");Serial.println("\n");// print 28 equal signs
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
    
    // Instead of reading the entire file, we'll do a quick structure check
    // and scan through the file looking for required properties
    
    // Check for valid JSON start
    char firstChar = file.read();
    if (firstChar != '{') {
        errorMsg = "Invalid format (not a JSON file)";
        file.close();
        return false;
    }
    
    // Count braces while scanning through file
    int braceCount = 1; // We already read the first one
    bool propertyFound[sizeof(requiredProps)/sizeof(requiredProps[0])] = {false};
    
    // Read in chunks to check brace balance and look for required properties
    const size_t BUFFER_SIZE = 256;
    char buffer[BUFFER_SIZE + 1]; // +1 for null termination
    buffer[BUFFER_SIZE] = '\0';
    
    size_t bytesRead;
    size_t totalRead = 1; // We've already read one character
    
    while ((bytesRead = file.read((uint8_t*)buffer, BUFFER_SIZE)) > 0) {
        // Scan chunk for braces and required properties
        for (size_t i = 0; i < bytesRead; i++) {
            if (buffer[i] == '{') braceCount++;
            else if (buffer[i] == '}') braceCount--;
            
            // Check for negative brace count (invalid JSON)
            if (braceCount < 0) {
                errorMsg = "Invalid format (more closing braces than opening)";
                file.close();
                return false;
            }
        }
        
        // Null-terminate the buffer for string operations
        buffer[min(bytesRead, BUFFER_SIZE)] = '\0';
        
        // Check for required properties in this chunk
        for (size_t i = 0; i < sizeof(requiredProps)/sizeof(requiredProps[0]); i++) {
            if (!propertyFound[i] && strstr(buffer, requiredProps[i])) {
                propertyFound[i] = true;
            }
        }
        
        totalRead += bytesRead;
        
        // Progress reporting for large files
        if (fileSize > 20000 && (totalRead % 20000) < BUFFER_SIZE) {
            Serial.printf("Validation progress: %u/%u bytes (%.1f%%)\n", 
                        totalRead, fileSize, (totalRead * 100.0) / fileSize);
        }
    }
    
    file.close();
    
    // Check if the braces are balanced
    if (braceCount != 0) {
        errorMsg = "Invalid format (missing " + String(braceCount) + " closing braces)";
        return false;
    }
    
    // Check all required properties were found
    for (size_t i = 0; i < sizeof(requiredProps)/sizeof(requiredProps[0]); i++) {
        if (!propertyFound[i]) {
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
    Serial.println(F("\n=============== Config Upload Utility ==============="));
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
      //check if the bytes written are not equal to the upload current size
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
          for (int i = 0; i < 41; i++) Serial.print("=");Serial.println();// print 41 equal signs
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
          for (int i = 0; i < 53; i++) Serial.print("=");Serial.println();// print closing line for config upload utility
          // Select first available options from the new config
          selectFirstAvailableOptions();
        } else {
          Serial.println(F("Failed to load new configuration"));
          for (int i = 0; i < 53; i++) Serial.print("=");Serial.println();// print closing line for config upload utility
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
    server.send(200, "application/json", "{\"success\": true}");
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

  size_t fileSize = file.size();
  Serial.printf("Config file size: %u bytes\n", fileSize);
  Serial.printf("Free heap before reading: %u\n", ESP.getFreeHeap());

  // Clear any previous configuration
  stationConfig.clear();

  // Stream directly from file instead of loading into memory
  DeserializationError error = deserializeJson(stationConfig, file);
  file.close();

  Serial.printf("Free heap after parsing: %u\n", ESP.getFreeHeap());

  if (error) {
    Serial.println("Failed to parse config file");
    Serial.printf("Parsing error: %s\n", error.c_str());

    String failedFilename = currentConfigFilename;
    Serial.printf("Deleting corrupted config file: %s\n", failedFilename.c_str());
    SPIFFS.remove("/station_config.json");
    SPIFFS.remove("/config_filename.txt");

    currentConfigFilename = "error_recovered.json";
    saveConfigFilename(currentConfigFilename);

    stationConfig.clear();
    configLoaded = false;

    createDefaultConfig();

    file = SPIFFS.open("/station_config.json", "r");
    if (!file) {
      Serial.println("Failed to open default config");
      return false;
    }

    // Stream directly from file again for the default config
    DeserializationError defaultError = deserializeJson(stationConfig, file);
    file.close();

    if (defaultError) {
      Serial.printf("Error parsing default config: %s\n", defaultError.c_str());
      return false;
    }

    Serial.printf("Recovered from failed config file: %s\n", failedFilename.c_str());
    loadDeviceState();
    server.send(400, "application/json", String("{\"error\":\"") + error.c_str() + "! Recovered from corrupted config file..\"}");
    configLoaded = true;
    return true;
  }

  configLoaded = true;
  Serial.println("Station Config Json Loaded");
  Serial.printf("Final heap after loading: %u\n", ESP.getFreeHeap());
  for (int i = 0; i < 38; i++) Serial.print("=");Serial.println("\n");
  return true;
}

//===============================================================
// Select First Available Options
//===============================================================
void selectFirstAvailableOptions() {//select the first available options
    String firstLine;//set the first line
    JsonObject lines = stationConfig["lines"];//get the lines
    for (JsonPair line : lines) {//loop through the lines
        firstLine = String(line.key().c_str());//set the first line
        break;//break the loop
    }
  
    String firstStation;//set the first station
    JsonObject stations = lines[firstLine]["stations"];//get the stations
    for (JsonPair station : stations) {//loop through the stations
        firstStation = String(station.key().c_str());//set the first station
        break;//break the loop
    }
    
    String firstTrack;//set the first track
    JsonArray tracks = stations[firstStation]["t"];//get the tracks
    if (tracks.size() > 0) {//if the tracks size is greater than 0
        firstTrack = tracks[0][0].as<String>();//set the first track
    } else {//if the tracks size is not greater than 0
        return;//return
    }
    
    JsonObject stationData;//set the station data
    JsonObject style;//set the style
    if (!getStationData(firstLine, firstStation, stationData, style)) {//if the station data is not found
        return;//return
    }
    
    // Create a temporary track object
    StaticJsonDocument<512> trackDoc;//set the track doc
    JsonObject trackData = trackDoc.to<JsonObject>();//set the track data
    if (!findTrackData(stationData, firstTrack, trackData)) {//if the track data is not found
        return;//return
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
        Serial.println(F("================Sequence Handler================"));
        
        // Add a small delay to ensure audio has finished processing
        delay(100);
        // Process the sequence
        handleSequencePlay();
        // Reset the sequence in progress flag after a delay
        // This prevents multiple triggers in quick succession
        delay(500);
        sequenceInProgress = false;
        for (int i = 0; i < 48; i++) Serial.print("=");Serial.println("\n");// print 48 equal signs

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
    // read the station info array (i array)
    JsonArray stationInfo = stationData["i"];
    
    currentStation.lineCode = stationInfo[0].as<const char*>();              // Line Code -> i[0]
    currentStation.stationCodeLine = trackData["Marker"]["lineCode"].as<const char*>();//
    currentStation.stationNumber = trackData["Marker"]["stationNumber"].as<const char*>();//
    currentStation.lineMarkerBgColor = style["lineMarkerBgColor"].as<const char*>();
    currentStation.lineNumberBgColor = style["lineNumberBgColor"].as<const char*>();
    currentStation.directionBarBgColor = style["directionBarBgColor"].as<const char*>();
    currentStation.stationNameJa = stationInfo[1].as<const char*>();         // Station Name Ja -> i[1]
    currentStation.stationNameHiragana = stationInfo[2].as<const char*>();   // Station Name Hiragana  -> i[2]
    currentStation.stationNameKo = stationInfo[3].as<const char*>();         // Station Name Korean -> i[3]
    currentStation.stationNameEn = station;
    currentStation.prevStation = trackData["Direction"]["PreviousStation"]["Ja"].as<const char*>();
    currentStation.prevStationEn = trackData["Direction"]["PreviousStation"]["En"].as<const char*>();
    currentStation.nextStation = trackData["Direction"]["NextStation"]["Ja"].as<const char*>();
    currentStation.nextStationEn = trackData["Direction"]["NextStation"]["En"].as<const char*>();
    currentStation.wardBox = stationInfo[4].as<const char*>();               // Ward Box -> i[4]
    currentStation.trackKey = track;
    // Update the sequenceLine variable for sequence mode
    sequenceLine = line;
}

//===============================================================
//  System Health function
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
    Serial.println(F("===============================\n"));
    
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
    trackData["Marker"]["lineCode"] = trackArray[1];
    trackData["Marker"]["stationNumber"] = trackArray[2];
    
    // Audio data
    JsonArray audioArray = trackArray[3];
    if (audioArray && audioArray.size() >= 4) {
        trackData["audio"]["melody"] = audioArray[0];
        trackData["audio"]["atos"] = audioArray[1];
        trackData["audio"]["doorChime"] = audioArray[2];
        trackData["audio"]["va"] = audioArray[3];
    }
    
    // Direction data
    JsonArray prevArray = trackArray[4];
    if (prevArray && prevArray.size() >= 2) {
        trackData["Direction"]["PreviousStation"]["Ja"] = prevArray[0];
        trackData["Direction"]["PreviousStation"]["En"] = prevArray[1];
    }
    
    JsonArray nextArray = trackArray[5];
    if (nextArray && nextArray.size() >= 2) {
        trackData["Direction"]["NextStation"]["Ja"] = nextArray[0];
        trackData["Direction"]["NextStation"]["En"] = nextArray[1];
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

// Command handler function typedefs and command registry
typedef void (*CommandHandlerFunction)(const String&);

struct CommandEntry {
    const char* command;
    CommandHandlerFunction handler;
    const char* description;
};

// Forward declarations for command handlers
void handleHelp(const String& param);
void handleHealth(const String& param);
void handleStatus(const String& param);
void handleVolume(const String& param);
void handlePlay(const String& param);
void handleIndex(const String& param);
void handleList(const String& param);
void handleStation(const String& param);
void handleReset(const String& param);
void handleReboot(const String& param);

// Registry of all available commands
const CommandEntry COMMAND_REGISTRY[] = {
    {"help",    handleHelp,    "Display help"},
    {"health",  handleHealth,  "Display system health information"},
    {"status",  handleStatus,  "Display current station and playback status"},
    {"volume",  handleVolume,  "Get or set volume level [0-30]"},
    {"play",    handlePlay,    "Play current melody/atos/chime/va"},
    {"index",   handleIndex,   "Reindex the file Count of the DFPlayer"},
    {"ls",      handleList,    "List files on SPIFFS or available stations"},
    {"station", handleStation, "Set current station [line] [station] [track]"},
    {"reset",   handleReset,   "Reset the WiFi or DFPlayer"},
    {"reboot",  handleReboot,  "Restart ESP32"},
};

const int COMMAND_COUNT = sizeof(COMMAND_REGISTRY) / sizeof(CommandEntry);

void processCommand() {
    serialCommand.trim();  // Remove leading/trailing whitespace
    
    // Always print a new line and prompt for empty commands
    if (serialCommand.length() == 0) {
        Serial.print(PROMPT);
        return;
    }
    
    // Split into command and parameters
    int spaceIndex = serialCommand.indexOf(' ');
    String command = (spaceIndex == -1) ? serialCommand : serialCommand.substring(0, spaceIndex);
    String param = (spaceIndex == -1) ? "" : serialCommand.substring(spaceIndex + 1);
    
    // Convert only the command to lowercase for case-insensitive comparison
    command.toLowerCase();
    
    // Find and execute the command
    bool commandFound = false;
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (command == COMMAND_REGISTRY[i].command) {
            COMMAND_REGISTRY[i].handler(param);
            commandFound = true;
            break;
        }
    }
    
    if (!commandFound) {
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

// Command handler implementations
void handleHelp(const String& param) {
    Serial.println(F("\n============= JR-Beru Shell Commands ============="));
    for (int i = 0; i < COMMAND_COUNT; i++) {
        char buffer[80];
        snprintf(buffer, sizeof(buffer), "%-18s - %s", 
                 COMMAND_REGISTRY[i].command, 
                 COMMAND_REGISTRY[i].description);
        Serial.println(buffer);
        
        // Skip duplicate entries (like 'ls' and 'stations')
        if (i < COMMAND_COUNT - 1 && 
            strcmp(COMMAND_REGISTRY[i].description, COMMAND_REGISTRY[i+1].description) == 0) {
            i++;
        }
    }
    for (int i = 0; i < 51; i++) Serial.print("=");Serial.println("\n");
}

void handleHealth(const String& param) {
    checkSysHealth();
}

void handleStatus(const String& param) {
    Serial.println(F("\n======== Current Status ========"));
    Serial.printf_P(PSTR("Current Line: %s\n"), currentStation.stationCodeLine);
    Serial.printf_P(PSTR("Current Station: %s (%s)\n"), currentStation.stationNameJa, currentStation.stationNameEn);
    Serial.printf_P(PSTR("Current Track: %s\n"), currentStation.trackKey);
    Serial.printf_P(PSTR("Current Melody: %d, Current Atos: %d, Current DoorChime: %d, Current VA: %d\n"), 
            currentMelody, currentAtos, currentDoorChime, currentVA);
    Serial.printf_P(PSTR("Volume: %d/30\n"), globalVolume);
    Serial.printf_P(PSTR("WiFi Status: %s\n"), WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf_P(PSTR("IP Address: %s\n"), WiFi.localIP().toString().c_str());
    }
    for (int i = 0; i < 32; i++) Serial.print("=");Serial.println("\n");
}

void handleVolume(const String& param) {
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

void handlePlay(const String& param) {
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

void handleIndex(const String& param) {
    Serial.println(F("Indexing DFPlayer files..."));
    UpdateFileCount();
    Serial.println(F("Indexing complete."));
}

void handleList(const String& param) {
    if (param == "/" || param.length() == 0) {
        Serial.println(F("\n======== SPIFFS Files System ========\n"));
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
        Serial.println("");
        for (int i = 0; i < 37; i++) Serial.print("=");Serial.println("\n");
    }
    else if (param == "stations") {
        Serial.println(F("\n=================== Available Stations ==================="));
        
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
                    String trackNum = trackArray[0].as<String>();
                    if (trackList.length() > 0) trackList += ", ";
                    trackList += trackNum;
                }
                
                Serial.printf_P(PSTR("  - %s (%s, %s) - Tracks: %s\n"), 
                    stationName.c_str(), 
                    stationJa.c_str(),
                    stationCode.c_str(),
                    trackList.c_str());
            }
            Serial.println();
        }
        
        for (int i = 0; i < 58; i++) Serial.print("=");Serial.println("\n");
    }
    else {
        Serial.println(F("Invalid ls parameter. Use: / or stations"));
    }
}

void handleStation(const String& param) {
    // Split parameters into array: [line, station, track]
    String params[3];
    int paramCount = 0;
    int startPos = 0;
    int spacePos;
    
    while ((spacePos = param.indexOf(' ', startPos)) != -1 && paramCount < 2) {
        params[paramCount++] = param.substring(startPos, spacePos);
        startPos = spacePos + 1;
    }
    if (startPos < param.length()) {
        params[paramCount++] = param.substring(startPos);
    }
    
    if (paramCount != 3) {
        Serial.println(F("Invalid format. Use: station [line] [station] [track]"));
        return;
    }
    
    // Get station data and validate
    JsonObject stationData;
    JsonObject style;
   
    if (!getStationData(params[0], params[1], stationData, style)) {
        Serial.printf_P(PSTR("Invalid station data for line '%s', station '%s'\n"), params[0].c_str(), params[1].c_str());
        return;
    }
    
    // Validate track data
    StaticJsonDocument<512> trackDoc;
    JsonObject trackData = trackDoc.to<JsonObject>();
    if (!findTrackData(stationData, params[2], trackData)) {
        Serial.printf_P(PSTR("Track '%s' not found in station '%s'\n"), params[2].c_str(), params[1].c_str());
        return;
    }
    
    updateCurrentStation(stationData, trackData, style, params[0].c_str(), params[1].c_str(), params[2].c_str());
    notifyUIofStationChange();
    saveDeviceState();
    
    Serial.printf_P(PSTR("Station set to: %s, %s, %s\n"), params[0].c_str(), params[1].c_str(), params[2].c_str());
}

void handleReset(const String& param) {
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
        delay(500);
        ESP.restart();
    }
    else {
        Serial.println(F("Invalid reset parameter. Use: player or wifi"));
    }
}

void handleReboot(const String& param) {
    Serial.println(F("Rebooting ESP32..."));
    delay(500);
    ESP.restart();
}



