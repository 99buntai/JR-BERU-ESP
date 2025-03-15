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
#define FW_VERSION      "ESP32-MCU-R0.4.9 WebUI-R3.2.5"  // Current firmware version

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
    String lineCode;
    String stationCodeLine;
    String stationNumber;
    String lineMarkerBgColor;
    String lineNumberBgColor;
    String directionBarBgColor;
    String stationNameJa;
    String stationNameHiragana;
    String stationNameKo;
    String stationNameEn;
    String prevStation;
    String prevStationEn;
    String nextStation;
    String nextStationEn;
    String wardBox;
    String trackKey;
} currentStation;

// Add these near the top with other global variables
DynamicJsonDocument stationConfig(120768); // 120KB buffer for JSON
bool configLoaded = false;

// First, add a global variable to store the current config filename
String currentConfigFilename = "station_config.json";

// First, add a new global variable to track playback mode
String playbackMode = "selected"; // Options: "selected", "random", "sequence"
int sequenceCurrentIndex = 0; // For tracking current position in sequence
String sequenceLine = ""; // Current line for sequence play
uint32_t stationChangeCounter = 0; // Counter for tracking station changes

// Add this structure to hold system health data
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

// Add these global variables for CPU usage calculation
static uint32_t previousCpu0Time = 0;
static uint32_t previousCpu1Time = 0;
static uint32_t previousIdleTime0 = 0;
static uint32_t previousIdleTime1 = 0;

// System monitoring
unsigned long lastHealthCheck = 0;  // For tracking health check intervals

// Add this structure near the top with other global variables
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

// Add these functions to handle state management
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

void loadDeviceState() {
    Serial.println(F("\n=== Loading Device State ==="));
    
    if (!SPIFFS.exists("/device_state.json")) {
        Serial.println("No saved state found");
        // Set default playback mode
        playbackMode = "selected";
        // If no saved state but we have config, set defaults from first available options
        if (configLoaded) {
            // Get the first line
            String firstLine;
            JsonObject lines = stationConfig["lines"];
            for (JsonPair line : lines) {
                firstLine = line.key().c_str();
                break;
            }
            
            // Get the first station
            String firstStation;
            JsonObject stations = lines[firstLine]["stations"];
            for (JsonPair station : stations) {
                firstStation = station.key().c_str();
                break;
            }
            
            // Get the first track
            String firstTrack;
            JsonObject tracks = stations[firstStation]["Trk"];
            for (JsonPair track : tracks) {
                firstTrack = track.key().c_str();
                break;
            }
            
            JsonObject stationData = stationConfig["lines"][firstLine]["stations"][firstStation];
            JsonObject trackData = stationData["Trk"][firstTrack];
            JsonObject style = stationConfig["lines"][firstLine]["style"];
            
            // Update currentStation with default values
            updateCurrentStation(stationData, trackData, style, firstLine.c_str(), firstStation.c_str(), firstTrack.c_str());
        }
        return;
    }

    // Try to open the file
    File file = SPIFFS.open("/device_state.json", "r");
    if (!file) {
        Serial.println("Failed to open device state file");
        playbackMode = "selected"; // Default value
        return;
    }

    // Read the file and handle potential errors
    String jsonStr = file.readString();
    file.close();
    
    if (jsonStr.length() == 0) {
        Serial.println("Empty device state file");
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
        
        Serial.println(F("\nRestoring station selection:"));
        Serial.printf("Line: %s, Station: %s, Track: %s\n", line.c_str(), station.c_str(), track.c_str());
        
        // Safety check - verify all paths exist in the config before accessing
        if (configLoaded && line.length() > 0 && station.length() > 0 && track.length() > 0) {
            // Check if line exists
            if (!stationConfig["lines"].containsKey(line)) {
                Serial.println(F("Line not found in config, using defaults"));
                return;
            }
            
            // Check if station exists
            if (!stationConfig["lines"][line]["stations"].containsKey(station)) {
                Serial.println(F("Station not found in config, using defaults"));
                return;
            }
            
            // Check if track exists
            if (!stationConfig["lines"][line]["stations"][station]["Trk"].containsKey(track)) {
                Serial.println(F("Track not found in config, using defaults"));
                return;
            }
            
            // Now we know all paths exist, we can safely access them
            JsonObject stationData = stationConfig["lines"][line]["stations"][station];
            JsonObject trackData = stationData["Trk"][track];
            JsonObject style = stationConfig["lines"][line]["style"];
            
            // Use updateCurrentStation function for consistency
            updateCurrentStation(stationData, trackData, style, line.c_str(), station.c_str(), track.c_str());
            
            Serial.println(F("\nStation restored successfully"));
        } else {
            Serial.println(F("Config not loaded or invalid station data"));
        }
    } else {
        Serial.println("No station selection data found");
    }
    
    Serial.println(F("===========================\n"));
}

// Add this helper function to avoid code duplication
void updateCurrentStation(JsonObject stationData, JsonObject trackData, JsonObject style, 
                        const char* line, const char* station, const char* track) {
    currentStation.lineCode = stationData["sInfo"]["sC"].as<const char*>();
    currentStation.stationCodeLine = trackData["Marker"]["LC"].as<const char*>();
    currentStation.stationNumber = trackData["Marker"]["sNum"].as<const char*>();
    currentStation.lineMarkerBgColor = style["lineMarkerBgColor"].as<const char*>();
    currentStation.lineNumberBgColor = style["lineNumberBgColor"].as<const char*>();
    currentStation.directionBarBgColor = style["directionBarBgColor"].as<const char*>();
    currentStation.stationNameJa = stationData["sInfo"]["Ja"].as<const char*>();
    currentStation.stationNameHiragana = stationData["sInfo"]["Hi"].as<const char*>();
    currentStation.stationNameKo = stationData["sInfo"]["Ko"].as<const char*>();
    currentStation.stationNameEn = station;
    currentStation.prevStation = trackData["Dir"]["p"]["Ja"].as<const char*>();
    currentStation.prevStationEn = trackData["Dir"]["p"]["En"].as<const char*>();
    currentStation.nextStation = trackData["Dir"]["n"]["Ja"].as<const char*>();
    currentStation.nextStationEn = trackData["Dir"]["n"]["En"].as<const char*>();
    currentStation.wardBox = stationData["sInfo"]["wB"].as<const char*>();
    currentStation.trackKey = track;
}

void checkSysHealth() {
    // Memory
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    float heapUsage = 100.0f * (1.0f - ((float)freeHeap / totalHeap));
    
    // SPIFFS
    uint32_t totalBytes = SPIFFS.totalBytes();
    uint32_t usedBytes = SPIFFS.usedBytes();
    float spiffsUsage = 100.0f * ((float)usedBytes / totalBytes);
    
    // Program Storage
    uint32_t flashSize = ESP.getFlashChipSize();
    uint32_t programSize = ESP.getSketchSize();
    float programUsage = 100.0f * ((float)programSize / flashSize);

    // Print Report
    Serial.println(F("\n======== System Health ========"));
    Serial.printf("Heap: %.1f%% used (%u KB free)\n", heapUsage, freeHeap / 1024);
    Serial.printf("Flash: %.1f%% used (%u KB free)\n", programUsage, (flashSize - programSize) / 1024);
    Serial.printf("SPIFFS: %.1f%% used (%u KB free)\n", spiffsUsage, (totalBytes - usedBytes) / 1024);
    Serial.printf("CPU Temp: %.1f°C\n", temperatureRead());
    Serial.println(F("===============================\n"));
}

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

// 1. First, let's create a queue to handle sequence operations more safely
QueueHandle_t sequenceQueue;

void setup()
{
  dfPlayerSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(600);

  Serial.println(F(""));
  Serial.println(F("==================="));
  Serial.println(F(" JR-Beru Booting "));
  Serial.println(F("==================="));
  Serial.println(F("Firmware Version: " FW_VERSION));

  // Initialize MP3 player first
  Serial.println(F("Initializing MP3-Player ... (May take 3~5 seconds)"));
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

    // Initialize SPIFFS first
  if(!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // Load config and state in the correct order
  loadConfigFilename();
  loadStationConfig();
  loadDeviceState();  // This will now have both config and state available

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
   
  if (!configLoaded) {
    loadStationConfig();
  }

  // Initial system health check
  checkSysHealth();

  // Create a queue for sequence operations
  sequenceQueue = xQueueCreate(1, sizeof(bool));

  // Create a task for sequence handling with larger stack
  xTaskCreatePinnedToCore(
    sequenceHandlerTask,
    "sequenceHandler",
    8192,  // Double the normal stack size
    NULL,
    1,
    NULL,
    CONFIG_ASYNC_TCP_RUNNING_CORE
  );
}



//===============================================================
// Main Loop
//===============================================================

void loop()
{
  // Only handle web-related tasks if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    ElegantOTA.loop();

    checkWiFiConnection();
  
    
    /* for debug use only 
    // Check system health periodically
    if (millis() - lastHealthCheck > 5000) {  // Check every 30 seconds
        checkSysHealth();
        lastHealthCheck = millis();
    }*/
            
  }
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
                ".station-name-ja, .station-name-hiragana, .station-name-ko { text-align: center; }"
                ".station-name-ja { position: relative; display: inline-block; font-size: 42px; font-weight: bold; "
                "line-height: 1.2; font-family: 'MS Gothic', 'Yu Gothic', sans-serif; white-space: nowrap; }"
                ".station-name-hiragana { font-size: 16px; color: #333;font-weight: bold; }"
                ".station-name-ko { font-size: 14px; color: #666; margin-bottom: 6px; }"
                
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
                ".station-name-en { font-size: 16px; color: #333; }"
                ".station-name-en.current { font-weight: bold; }"
                ".station-name-en-left { width: 33%; text-align: left; }"
                ".station-name-en-center { width: 33%; text-align: center; }"
                ".station-name-en-right { width: 33%; text-align: right; }"
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
          "<div class='station-name-ja'>" + currentStation.stationNameJa + "</div>"
          "<div class='station-name-hiragana'>" + currentStation.stationNameHiragana + "</div>"
          "<div class='station-name-ko'>" + currentStation.stationNameKo + "</div>"
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
          "<span class='station-name-en station-name-en-left'>" + currentStation.prevStationEn + "</span>"
          "<span class='station-name-en station-name-en-center current'>" + currentStation.stationNameEn + "</span>"
          "<span class='station-name-en station-name-en-right'>" + currentStation.nextStationEn + "</span>"
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
          "  const stationNameJa = document.querySelector('.station-name-ja');"
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
          "    .then(() => loadConfig())"
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
          "      loadConfig();"
          "      alert('Configuration deleted successfully');"
          "    })"
          "    .catch(e => alert('Failed to delete configuration: ' + e));"
          "}"

          // Cached config and loading
          "let cachedConfig = null;"
          "function loadConfig() {"
          "  Promise.all(["
          "    api('/getCurrentConfig').then(r => r.json()),"
          "    api('/getStationConfig').then(r => r.json())"
          "  ]).then(([configInfo, config]) => {"
          "    $('configName').textContent = configInfo.filename;"
          "    cachedConfig = config;"
          "    $('playMode').value = configInfo.playbackMode;"
          "    if (configInfo.volume !== undefined) {"
          "      const volumeControl = $('volume');"
          "      const volumeDisplay = document.getElementById('volumeDisplay');"
          "      if (volumeControl) volumeControl.value = configInfo.volume;"
          "      if (volumeDisplay) volumeDisplay.textContent = configInfo.volume;"
          "    }"
          "    populateSelector('lineSelect', config.lines, null);"
          "    return api('/getCurrentSelections').then(r => r.json());"
          "  }).then(sel => {"
          "    if (!sel) return;"
          "    updateSelectors(sel, cachedConfig);"
          "    if (sel.line && sel.station && sel.track) {"
          "      updateStationSign();"
         // "      setTimeout(updateLineMarkerPosition, 200);"
          "    }"
          "  }).catch(e => console.error('Error:', e));"
          "}"

       // Populate selectors
          "function populateSelector(id, items, selected) {"
          "  const sel = $(id);"
          "  sel.innerHTML = '<option>' + sel.options[0].text + '</option>';"
          "  Object.keys(items || {}).forEach(item => {"
          "    const opt = document.createElement('option');"
          "    opt.value = opt.textContent = item;"
          "    opt.selected = item === selected;"
          "    sel.appendChild(opt);"
          "  });"
          "}"
          
          // Update selectors with selection data
          "function updateSelectors(sel, config) {"
          "  if (!config) return;"
          "  const line = config?.lines[sel.line];"
          "  const station = line?.stations[sel.station];"
          "  const lsel = $('lineSelect');"
          "  const lines = Array.from(lsel.options).find(o => o.value === sel.line);"
          "  if (lines) lines.selected = true;"
          "  if (line) populateSelector('stationSelect', line.stations, sel.station);"
          "  if (station) populateSelector('trackSelect', station.Trk, sel.track);"
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
          "    const tracks = cachedConfig.lines[line].stations[station].Trk;"
          "    if (tracks) {"
          "      populateSelector('trackSelect', tracks);"
          "      if ($('trackSelect').options.length > 1) {"
          "        $('trackSelect').selectedIndex = 1;"
          "        updateStationSign();"
          "      }"
          "    }"
          "  }"
          "}"

          // Update station sign
          "function updateStationSign() {"
          "  const lineSelect = document.getElementById('lineSelect');"
          "  const stationSelect = document.getElementById('stationSelect');"
          "  const trackSelect = document.getElementById('trackSelect');"
          "  const line = lineSelect.value;"
          "  const station = stationSelect.value;"
          "  const track = trackSelect.value;"

          "  if (!line || !station || !track || track === 'Select Track') return;"

          "  fetch(`/updateStationSign?line=${line}&station=${station}&track=${track}`)"
          "    .then(response => response.ok ? response : Promise.reject('Failed to update'))"
          "    .then(() => {"
          "      if (!cachedConfig) return;"

          "      const stationData = cachedConfig.lines[line].stations[station];"
          "      const trackData = stationData.Trk[track];"
          "      const style = cachedConfig.lines[line].style;"

          // Background color updates
          "      document.querySelector('.line-marker').style.backgroundColor = style.lineMarkerBgColor;"
          "      document.querySelector('.station-number-container').style.backgroundColor = style.lineNumberBgColor;"
          "      document.querySelector('.station-indicator').style.backgroundColor = style.lineNumberBgColor;"
          "      document.querySelector('.direction-bar').style.backgroundColor = style.directionBarBgColor;"

                // Update line marker position
          "      setTimeout(updateLineMarkerPosition, 10);"
                // Audio value updates
          "      document.getElementById('melody').value = trackData.audio.melody;"
          "      document.getElementById('atos').value = trackData.audio.atos;"
          "      document.getElementById('doorchime').value = trackData.audio.dC;"
          "      document.getElementById('platform').value = trackData.audio.va;"

                // Text updates
          "      const stInfo = stationData.sInfo || {};"
          "      const lineMarker = trackData.Marker || {};"
          "      const direction = trackData.Dir || {};"
          "      const prev = direction.p || {};"
          "      const next = direction.n || {};"
          
          "      document.querySelector('.station-name-ja').textContent = stInfo.Ja || '';"
          "      document.querySelector('.station-name-hiragana').textContent = stInfo.Hi || '';"
          "      document.querySelector('.station-name-ko').textContent = stInfo.Ko || '';"
          "      document.querySelector('.station-name-en-left').textContent = prev.En || '';"
          "      document.querySelector('.station-name-en-center').textContent = station;"
          "      document.querySelector('.station-name-en-right').textContent = next.En || '';"
          "      document.querySelector('.direction-left').textContent = prev.Ja || '';"
          "      document.querySelector('.direction-right').textContent = next.Ja || '';"
          "      document.querySelector('.line-code-akb').textContent = stInfo.sC || '';"
          "      document.querySelector('.line-code-jy').textContent = lineMarker.LC || '';"
          "      document.querySelector('.line-number').textContent = lineMarker.sNum || '';"
          "      document.querySelector('.ward-box').textContent = stInfo.wB || '';"

                // Update config values
          "      updateConfig();"
          "    })"
          "    .catch(error => console.error('Error:', error));"
          "}"

          "$('configFile').addEventListener('change', uploadConfig);"
          "loadConfig();"

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
"        const tracks = cachedConfig.lines[data.line].stations[data.station].Trk;"
"        populateSelector('trackSelect', tracks, data.track);"
"        $('trackSelect').value = data.track;"
"        updateStationSign();" // Update the display
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
   Serial.println(F("=====Audio File Count:====="));

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
  int fileCount;
  do {
    fileCount = myDFPlayer.readFileCountsInFolder(folder);
    fileCount = myDFPlayer.readFileCountsInFolder(folder); //bug fix! run it twice to get the correct value
  } while (fileCount == -1);
  return fileCount;
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
          Serial.println(F("==Button Pressed=="));
            if (!loopPlaying) {
               handlePlayMelody();
                myDFPlayer.enableLoop(); // Call only once
                loopPlaying = true;
                AtosLastPlayed = false;
                notifyUIofStationChange();
            }
        } else if (loopPlaying) {
          Serial.println(F("==Button Released=="));
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
    if (RandomPlayOn) {
        PlayRandomAudio(folder, totalTracks);
    } else {
        PlayCurrentAudio(folder, currentTrack);
    }
}

void handlePlayMelody() { playAudio(MelodyFolder, currentMelody, MelodyCount); }
void handlePlayAtos() { playAudio(AtosFolder, currentAtos, AtosCount); }
void handlePlayVA() { playAudio(VAFolder, currentVA, VACount); }

// Check if Atos has finished playing
void DoorChime() {
  // Restore original condition and behavior
  if (DonePlaying == true && AtosLastPlayed == true) {
    Serial.println("====Door Chime playing====");
    
    // Set the flags before playing the audio
    DonePlaying = false;
    AtosLastPlayed = false;
    
    // Play the door chime
    PlayCurrentAudio(DoorChimeFolder, currentDoorChime);
    
    // Check if we're in sequence mode to advance to next station
    if (playbackMode == "sequence") {
      Serial.println("## Sequence mode active ##");
      
      // Use the queue to schedule the advancement in the separate task
      bool signal = true;
      xQueueSend(sequenceQueue, &signal, 0);
    }
    }
  }





//================================MP3-Player-Sleep=============================
void DFPSleep(){if (DonePlaying == true){myDFPlayer.sleep();Serial.println(F("==MP3 Board going to sleep=="));}}

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
      
      if (playbackMode == "sequence" && value == currentAtos) {
        // Only advance to next station if the ATOS announcement finished
        Serial.println(F("Sequence mode: Advancing to next station"));
        handleSequencePlay();
      }
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
        29000,
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
    
    Serial.printf("Playing platform announcement with mode: %s\n", mode.c_str());
    
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
        
        // Get first available options from default config
        if (configLoaded) {
            String firstLine;
            JsonObject lines = stationConfig["lines"];
            for (JsonPair line : lines) {
                firstLine = line.key().c_str();
                break;
            }
            
            String firstStation;
            JsonObject stations = lines[firstLine]["stations"];
            for (JsonPair station : stations) {
                firstStation = station.key().c_str();
                break;
            }
            
            String firstTrack;
            JsonObject tracks = stations[firstStation]["Trk"];
            for (JsonPair track : tracks) {
                firstTrack = track.key().c_str();
                break;
            }
            
            // Update station sign with first available options
            JsonObject stationData = stationConfig["lines"][firstLine]["stations"][firstStation];
            JsonObject trackData = stationData["Trk"][firstTrack];
            JsonObject style = stationConfig["lines"][firstLine]["style"];
            
            updateCurrentStation(stationData, trackData, style, firstLine.c_str(), firstStation.c_str(), firstTrack.c_str());
            saveDeviceState();  // Save this as the current state
        }
        
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

    JsonObject stationData = stationConfig["lines"][line]["stations"][station];
    JsonObject trackData = stationData["Trk"][track];
    JsonObject style = stationConfig["lines"][line]["style"];
    
    if (!stationData || !trackData) {
        server.send(400, "text/plain", "Invalid station data");
        return;
    }

    // Update currentStation struct
    currentStation.lineCode = stationData["sInfo"]["sC"].as<const char*>();
    currentStation.stationCodeLine = trackData["Marker"]["LC"].as<const char*>();
    currentStation.stationNumber = trackData["Marker"]["sNum"].as<const char*>();
    currentStation.lineMarkerBgColor = style["lineMarkerBgColor"].as<const char*>();
    currentStation.lineNumberBgColor = style["lineNumberBgColor"].as<const char*>();
    currentStation.directionBarBgColor = style["directionBarBgColor"].as<const char*>();
    currentStation.stationNameJa = stationData["sInfo"]["Ja"].as<const char*>();
    currentStation.stationNameHiragana = stationData["sInfo"]["Hi"].as<const char*>();
    currentStation.stationNameKo = stationData["sInfo"]["Ko"].as<const char*>();
    currentStation.stationNameEn = station;
    currentStation.prevStation = trackData["Dir"]["p"]["Ja"].as<const char*>();
    currentStation.prevStationEn = trackData["Dir"]["p"]["En"].as<const char*>();
    currentStation.nextStation = trackData["Dir"]["n"]["Ja"].as<const char*>();
    currentStation.nextStationEn = trackData["Dir"]["n"]["En"].as<const char*>();
    currentStation.wardBox = stationData["sInfo"]["wB"].as<const char*>();
    currentStation.trackKey = track;
    
    // Update audio settings
    currentMelody = trackData["audio"]["melody"];
    currentAtos = trackData["audio"]["atos"];
    currentDoorChime = trackData["audio"]["dC"];
    currentVA = trackData["audio"]["va"];
    
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

  // Create a new endpoint to set playback mode
  // Add this in the setupWebServer function
  server.on("/setPlaybackMode", HTTP_GET, []() {
    String mode = server.arg("mode");
    if (!mode.isEmpty()) {
      // Add detailed logging
      Serial.printf("\n===== PLAYBACK MODE CHANGE ======\n");
      Serial.printf("Previous mode: %s\n", playbackMode.c_str());
      Serial.printf("Current mode: %s\n", mode.c_str());
      Serial.println("================================");
      
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
// Validate Json File
//===============================================================

bool validateJsonFile(const char* filename, String& errorMsg) {
  if (!SPIFFS.exists(filename)) { //if the file does not exist
    errorMsg = "File not found"; //set the error message
    return false; //return false
  }
  
  File file = SPIFFS.open(filename, "r");//open the file
  if (!file) {
    errorMsg = "Failed to open file";//set the error message
    return false; //return false
  }
  
  // Get file size for proper memory allocation
  size_t fileSize = file.size();
  Serial.printf("Validating file: %s (%u bytes)\n", filename, fileSize);
  
  // Read the entire file content
  String jsonStr;
  jsonStr.reserve(fileSize + 1);
  
  // Read file in chunks just like in loadStationConfig
  const size_t BUFFER_SIZE = 1024;//set the buffer size
  char buffer[BUFFER_SIZE];//set the buffer
  size_t totalRead = 0;//set the total read
  
  while (file.available()) { //while the file is available
    size_t bytesRead = file.readBytes(buffer, BUFFER_SIZE);//read the bytes
    jsonStr += String(buffer).substring(0, bytesRead);//add the read data to the json string
    
    totalRead += bytesRead;
    // Print validation progress for large files
    if (fileSize > 20000 && (totalRead % 20000) < BUFFER_SIZE) {
      Serial.printf("Validation progress: %u/%u bytes (%.1f%%)\n", 
                     totalRead, fileSize, (totalRead * 100.0) / fileSize);
    }
  }
  file.close();
  
  Serial.println("Checking JSON structure...");
  
  // Check if the file starts with a JSON object
  if (!jsonStr.startsWith("{")) { //if the json string is not a brace
    errorMsg = "Invalid format (not a JSON file)"; //set the error message
    return false; //return false
  }
  
  // Check for balanced braces
  int braceCount = 0;//set the brace count
  for (size_t i = 0; i < jsonStr.length(); i++) { //for the length of the json string
    if (jsonStr[i] == '{') { //if the json string is a brace
      braceCount++; //increment the brace count
    } else if (jsonStr[i] == '}') { //if the json string is a brace
      braceCount--; //decrement the brace count
    }
    
    // If braceCount goes negative, we have more closing braces than opening
    if (braceCount < 0) {
      errorMsg = "Invalid format (more closing braces than opening)";
      return false;
    }
  }
  
  // After counting all braces, if count isn't zero, we're missing some braces
  if (braceCount != 0) {
    errorMsg = "Invalid format (missing " + String(braceCount) + " closing braces)";
    return false;
  }
  
  // Check for required Line, style and stations properties to see if they exist
  if (jsonStr.indexOf("\"lines\"") == -1) {
    errorMsg = "Missing 'lines' property";
    return false;
  }
  if (jsonStr.indexOf("\"style\"") == -1) {
    errorMsg = "Missing 'style' in line config";
    return false;
  }
  if (jsonStr.indexOf("\"stations\"") == -1) {
    errorMsg = "Missing 'stations' in line config";
    return false;
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
    Serial.println("==========Config Upload Utility==========");
    Serial.printf("Upload Start: %s\n", filename.c_str());
    
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
    Serial.printf("Writing to temp file: %s\n", tempFilename.c_str());
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
      Serial.printf("ERROR: File too large, max size is %u bytes\n", MAX_FILE_SIZE);
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
      
      // Add debug every ~10KB to monitor the upload progress
      if (totalBytes % 10240 < upload.currentSize) {
        Serial.printf("Upload progress: %u bytes written\n", totalBytes);
        Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
      }
      
      if (bytesWritten != upload.currentSize) {
        Serial.printf("Write error: only %u of %u bytes written\n", 
                       bytesWritten, upload.currentSize);
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
      Serial.printf("Upload Complete: %u bytes in %lu ms\n", totalBytes, uploadDuration);
      
      // Process the JSON file as a station config
      if (isStationConfigFile) {
        // Validate the uploaded file before replacing existing configuration
        String validationError;
        if (!validateJsonFile(tempFilename.c_str(), validationError)) {
          Serial.printf("Validation failed: %s\n", validationError.c_str());
          Serial.println("Deleting invalid temp file");
          SPIFFS.remove(tempFilename);
          Serial.println("Keeping existing configuration");
          Serial.println("=========================================");
          // Send error response immediately
          StaticJsonDocument<256> errorResponse;
          errorResponse["error"] = "Validation failed: " + validationError;
          String jsonResponse;
          serializeJson(errorResponse, jsonResponse);
          server.send(400, "application/json", jsonResponse);
          return;
        }
        
        Serial.println("Uploaded file validated!");
        
        // If existing config exists, remove it
        if (SPIFFS.exists("/station_config.json")) {
          Serial.println("Removing existing station config");
          SPIFFS.remove("/station_config.json");
        }
        
        // Rename temp file to the actual config file
        Serial.printf("Renaming %s to /station_config.json\n", tempFilename.c_str());
        if (SPIFFS.rename(tempFilename, "/station_config.json")) {
          Serial.println("File replaced successfully");
        } else {
          Serial.println("Error replacing file");
          uploadError = "Error renaming temporary file";
        }
        
        // Update the current config filename to the original uploaded filename
        currentConfigFilename = upload.filename;
        Serial.printf("Updating config filename to: %s\n", currentConfigFilename.c_str());
        
        // Save this filename to SPIFFS so it persists across reboots
        saveConfigFilename(currentConfigFilename);
        
        configLoaded = false;
        Serial.println("Loading new station configuration");
        loadStationConfig();
        
        if (configLoaded) {
          Serial.println("New configuration loaded successfully");
          Serial.println("=========================================");
          // Select first available options from the new config
          selectFirstAvailableOptions();
        } else {
          Serial.println("Failed to load new configuration");
          Serial.println("=========================================");
          uploadError = "Failed to load new configuration";
        }
      }
      else {
        Serial.printf("File uploaded as: %s (not a station configuration)\n", tempFilename.c_str());
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
void loadStationConfig() {
  Serial.println("===== Loading Station Config Json=====");
  if (!SPIFFS.exists("/station_config.json")) {
    Serial.println("Config file not found, creating default Station config");
    createDefaultConfig();
  }

  File file = SPIFFS.open("/station_config.json", "r");
  if (!file) {
    Serial.println("Failed to open Station config file");
    return;
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
    Serial.printf("File read incomplete: expected %u bytes, got %u bytes\n", fileSize, totalBytesRead);
    Serial.println("Aborting JSON parsing due to incomplete file read");
    return;
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
    return;
  }

    String defaultJsonStr = file.readString();//read the default config file
    file.close();

    DeserializationError defaultError = deserializeJson(stationConfig, defaultJsonStr);//deserialize the default config file
    if (defaultError) {
      Serial.printf("Error parsing default config: %s\n", defaultError.c_str());
      return;
    }

    Serial.printf("Recovered from failed config file: %s\n", failedFilename.c_str());
    loadDeviceState();
  configLoaded = true;
    return;
  }

  configLoaded = true;
  Serial.println("Station Config Json Loaded");
  Serial.printf("Final heap after loading: %u\n", ESP.getFreeHeap());
  Serial.println("======================================");
}

//===============================================================
// Select First Available Options
//===============================================================
void selectFirstAvailableOptions() {
  String firstLine;
  JsonObject lines = stationConfig["lines"];
  for (JsonPair line : lines) {
    firstLine = line.key().c_str();
    break;
  }
  
  String firstStation;
  JsonObject stations = lines[firstLine]["stations"];
  for (JsonPair station : stations) {
    firstStation = station.key().c_str();
    break;
  }
  
  String firstTrack;
  JsonObject tracks = stations[firstStation]["Trk"];
  for (JsonPair track : tracks) {
    firstTrack = track.key().c_str();
    break;
  }
  
  // Update station sign with first available options
  JsonObject stationData = stationConfig["lines"][firstLine]["stations"][firstStation];
  JsonObject trackData = stationData["Trk"][firstTrack];
  JsonObject style = stationConfig["lines"][firstLine]["style"];
  
  updateCurrentStation(stationData, trackData, style, firstLine.c_str(), firstStation.c_str(), firstTrack.c_str());
  saveDeviceState();  // Save this as the current state
}

//===============================================================
// Create Default Config
//===============================================================
void createDefaultConfig() {
  Serial.println("Creating Default Station Config Json");
  const char* defaultConfig = R"({"lines":{"JY":{"style":{"lineMarkerBgColor":"#000000","lineNumberBgColor":"#80c241","directionBarBgColor":"#006400"},"stations":{"Akihabara":{"sInfo":{"sC":"AKB","Ja":"秋葉原","Hi":"あきはばら","Ko":"아키하바라","wB":"山"},"Trk":{"track1":{"Marker":{"LC":"JY","sNum":"03"},"audio":{"melody":1,"atos":2,"dC":1,"va":1},"Dir":{"p":{"Ja":"神田","En":"Kanda"},"n":{"Ja":"御徒町","En":"Okachimachi"}}}}}}}}})";

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
    sequenceLine = currentStation.stationCodeLine; // Always use the current line from currentStation
    // Get all stations in the current line
    JsonObject line = stationConfig["lines"][sequenceLine];
    if (!line.isNull()) {
        JsonObject stations = line["stations"];
        
        // Get array of station keys for sequencing
        int stationCount = 0;
        String stationKeys[80]; // Assuming max 80 stations per line
        
        for (JsonPair station : stations) {
            stationKeys[stationCount++] = String(station.key().c_str());
        }
        // Find current station index or start from 0
        int currentIndex = 0;
        for (int i = 0; i < stationCount; i++) {
            if (stationKeys[i] == currentStation.stationNameEn) {
                currentIndex = i;
                break;
            }
        }

        int nextIndex = (currentIndex + 1) % stationCount;  // Get next station index (wrap around if at end)
        String nextStation = stationKeys[nextIndex];        // Get next station data

        JsonObject stationData = stations[nextStation];     // Find first track for this station
        JsonObject tracks = stationData["Trk"];
        String firstTrack;
        
        for (JsonPair track : tracks) {
            firstTrack = String(track.key().c_str());
            break;
        }
        
        // Update station sign to the next station
        if (!nextStation.isEmpty() && !firstTrack.isEmpty()) {
            updateStationForSequence(sequenceLine.c_str(), nextStation.c_str(), firstTrack.c_str());
        }
        
        // Play the current audio for the new station
        PlayCurrentAudio(VAFolder, currentVA);
        
        // Store current sequence position
        sequenceCurrentIndex = nextIndex;
    }
}

//===============================================================
// Update Station For Sequence Play
//===============================================================
void updateStationForSequence(const char* line, const char* station, const char* track) {
    if (!configLoaded) return;
    
    Serial.printf("Updating station for sequence: %s - %s - %s\n", line, station, track);
    
    JsonObject stationData = stationConfig["lines"][line]["stations"][station];
    JsonObject trackData = stationData["Trk"][track];
    JsonObject style = stationConfig["lines"][line]["style"];
    
    // Update currentStation with all values
    updateCurrentStation(stationData, trackData, style, line, station, track);
    
    // Update audio settings
    currentMelody = trackData["audio"]["melody"];
    currentAtos = trackData["audio"]["atos"];
    currentDoorChime = trackData["audio"]["dC"];
    currentVA = trackData["audio"]["va"];
    
    saveDeviceState();
    
    // Signal UI to update
    notifyUIofStationChange();
    
    Serial.println("Station updated for sequence, UI notification sent");
    Serial.println("====================================================");
}

//===============================================================
// Notify UI of Station Changes
//===============================================================
void notifyUIofStationChange() {
  Serial.println("Sending UI updates");
  
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
  Serial.printf("Station change counter: %d\n", stationChangeCounter);
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
        Serial.println("==================Sequence Handler==================");
        // Now it's safe to call the sequence handling function
        handleSequencePlay();
      }
    }
    delay(10); // Small delay
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
      Serial.println("WiFi connection lost, reconnecting...");
      WiFi.reconnect();
    }else {
      Serial.println("Wifi health check passed");
    }
  }
}



