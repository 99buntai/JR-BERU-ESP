#include <DFRobotDFPlayerMini.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <SoftwareSerial.h>
#include <ElegantOTA.h>

ESP8266WebServer server(80);

SoftwareSerial softSerial(/*rx =*/D5, /*tx =*/D6);
#define FPSerial softSerial

DFRobotDFPlayerMini myDFPlayer;
void printDetail(uint8_t type, int value);


//================================
// Global Configuration Constants
//================================
#define FW_VERSION      "MCU-R0.3.7 WebUI-R2.0.5"  // Current firmware version

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

int currentMelody = 1;
int currentAtos = 1;
int currentDoorChime = 1;
int currentVA = 1;

int MainButtonPin = D4;  // The pin where the push button is connected
int MainButtonState = HIGH;  // Current state of the button
int lastMainButtonState = HIGH;  // Previous state of the button
int loopPlaying = false;
int DonePlaying = false;
int AtosLastPlayed = false;
bool atosPlayed = false;
bool shouldSaveConfig = false;
bool RandomPlayOn = true;

unsigned long lastDebounceTime = 0;  // Last time the button state changed
unsigned long debounceDelay = 50;  // Debounce time; increase if the output flickers


//HTTP Logins
const char* HTTP_USERNAME = "JR";
const char* HTTP_PASSWORD = "beru";

char ssid[32];  // Stores SSID of the WiFi network
char pass[32];  // Stores password of the WiFi network



//=============================Station Sign Configuration===========================
// Line and Station Code
const char* lineCode = "AKB";          // Top text in black area
const char* stationCodeLine = "JY";     // Line code in green box
const char* stationNumber = "03";       // Station number in green box

// Colors
const char* lineMarkerBgColor = "#000000";      // Line marker background color
const char* lineNumberBgColor = "#80C241";      // Station number container background color
const char* directionBarBgColor = "#006400";    // Direction bar background color
const char* stationIndicatorColor = "#82c03b";  // Current station indicator color

// Station Names
// Japanese text stored as UTF-8 encoded hex values
const char* stationNameJa = "\xE7\xA7\x8B\xE8\x91\x89\xE5\x8E\x9F";         // 秋葉原
const char* stationNameHiragana = "\xE3\x81\x82\xE3\x81\x8D\xE3\x81\xAF\xE3\x81\xB0\xE3\x82\x89"; // あきはばら
const char* stationNameKo = "\xEC\x95\x84\xED\x82\xA4\xED\x95\x98\xEB\xB0\x94\xEB\x9D\xBC";       // 아키하바라
const char* stationNameEn = "Akihabara"; // English name

// Direction Information
const char* prevStation = "\xE7\xA5\x9E\xE7\x94\xB0";           // 神田 (Previous station)
const char* prevStationEn = "Kanda";                            // Previous station English
const char* nextStation = "\xE5\xBE\xA1\xE5\xBE\x92\xE7\x94\xBA"; // 御徒町 (Next station)
const char* nextStationEn = "Okachimachi";                      // Next station English

// Ward Label
const char* wardLabel = "\xE5\xB1\xB1\xE5\x8C\xBA";            // 山区
const char* wardBox = "\xE5\xB1\xB1";                          // 山 (Character in box)
//==================================================================================





//================================WiFi and Server Setup=============================
void saveConfigCallback() {shouldSaveConfig = true;}
void setupWiFiManager() {
  WiFiManager wifiManager;

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // WiFi will be connected if available, else, it will continue in AP mode
  wifiManager.autoConnect("JR-BERU-AP");

  if (shouldSaveConfig) {
    strncpy(ssid, WiFi.SSID().c_str(), sizeof(ssid));
    strncpy(pass, WiFi.psk().c_str(), sizeof(pass));
  }
}


void setupOTA() {
  ElegantOTA.begin(&server); // Start ElegantOTA with the server object
//  ElegantOTA.setFormAuthentication("admin", "admin"); // Set authentication credentials if needed
}
//==================================================================================



void setup()
{

  FPSerial.begin(9600);
  Serial.begin(115200);

  pinMode(MainButtonPin, INPUT);
  delay(1000); // Waiting for mp3 board to boot

  Serial.println(F("==================="));
  Serial.println(F(" JR-Beru Booting "));
  Serial.println(F("==================="));
  Serial.println(F("Firmware Version: " FW_VERSION));
  Serial.println(F("Initializing MP3-Player ... (May take 3~5 seconds)"));
  if (!myDFPlayer.begin(FPSerial, /*isACK = */true, /*doReset = */false)) {  //Use serial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    while(true){
      delay(0); // Code to compatible with ESP8266 watch dog.
    }
  }
  Serial.println(F("MP3-Player online."));

  myDFPlayer.volume(globalVolume);  //Set volume value. From 0 to 30


  Serial.println(F("Updating Files..."));
  UpdateFileCount();

  Serial.println(F("Playing Bootup Sound..."));
  PlayRandomAudio(MelodyFolder,MelodyCount);

  Serial.println("Starting Wifi Manager...");
  setupWiFiManager();
  Serial.println("Wifi Manager Started!");
  
  WiFi.begin(ssid, pass);

   ElegantOTA.setAuth("JR", "BERU");
   setupOTA(); // Initialize ElegantOTA

    server.on("/", HTTP_GET, handleRoot);

    server.on("/toggleRandomPlay", HTTP_GET, []() {
      toggleRandomPlay();
      server.send(200, "text/plain", "Toggling RandomPlay");
    });

    server.on("/playVA", HTTP_GET,[](){
      handlePlayVA();
      server.send(200, "text/plain", "playVA");
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

    // Update currentMelody, currentAtos, currentDoorChime, and currentVA with the selected values
    currentMelody = melodyValue;
    currentAtos = atosValue;
    currentDoorChime = doorchimeValue;
    currentVA = platformValue;

    Serial.println(F("==============Web Config Updated!=============="));
    Serial.print(F("Melody: ")); Serial.println(currentMelody);
    Serial.print(F("Atos: ")); Serial.println(currentAtos);
    Serial.print(F("DoorChime: ")); Serial.println(currentDoorChime);
    Serial.print(F("VA: ")); Serial.println(currentVA);
    Serial.println(F("==============================================="));

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

  server.begin();
  Serial.println(F("Web server started!"));
  Serial.println(F("========Boot up Completed!========"));
   
}





void loop()
{
  server.handleClient();//Handle WebClient
  ElegantOTA.loop(); // Handle ElegantOTA updates

  
  handleButton();
  DoorChime();
  //DFPSleep();


 if (myDFPlayer.available()) {printDetail(myDFPlayer.readType(), myDFPlayer.read()); } //Print the detail message from DFPlayer to handle different errors and states. 
  
  }






//================================HTML Page=============================
void handleRoot() {
  String html = "<html><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "* { box-sizing: border-box; margin: 0; padding: 0; }"
                "body { font-family: 'Segoe UI', Roboto, Arial, sans-serif; background: #f5f5f5; color: #333; line-height: 1.6; }"
                
                // Navigation bar
                ".navbar { position: fixed; top: 0; left: 0; right: 0; background: #2c3e50; color: white; "
                "padding: 1rem; text-align: center; box-shadow: 0 2px 4px rgba(0,0,0,0.1); z-index: 1000; }"
                ".navbar h1 { font-size: 1.5rem; font-weight: 500; margin: 0; }"
                
                // Main container
                ".container { max-width: 800px; margin: 80px auto 100px; padding: 20px; }"
                
                // Control panels
                ".panel { background: white; border-radius: 10px; padding: 15px; margin-bottom: 15px; "
                "box-shadow: 0 2px 4px rgba(0,0,0,0.05); }"
                ".panel-header { font-size: 1.2rem; color: #2c3e50; margin-bottom: 12px; font-weight: bold; }"
                
                // Control groups
                ".control-group { display: flex; align-items: center; margin-bottom: 12px; gap: 10px; }"
                ".control-label { flex: 1; font-size: 0.95rem; color: #555; }"
                
                // Selectors
                "select { flex: 0 0 80px; padding: 8px; border: 1px solid #ddd; border-radius: 6px; "
                "background: #fff; font-size: 0.9rem; color: #333; }"
                
                // Toggle switch
                ".toggle-switch { position: relative; width: 50px; height: 26px; }"
                ".toggle-switch input { display: none; }"
                ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; "
                "background-color: #ccc; transition: .4s; border-radius: 34px; }"
                ".slider:before { position: absolute; content: ''; height: 20px; width: 20px; left: 3px; "
                "bottom: 3px; background-color: white; transition: .4s; border-radius: 50%; }"
                "input:checked + .slider { background-color: #2196F3; }"
                "input:checked + .slider:before { transform: translateX(24px); }"
                
                // Volume slider
                ".volume-slider { flex: 1; -webkit-appearance: none; height: 5px; border-radius: 5px; "
                "background: #ddd; outline: none; }"
                ".volume-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; "
                "border-radius: 50%; background: #2196F3; cursor: pointer; }"
                
                // Buttons
                ".btn { background: #2196F3; color: white; border: none; padding: 10px 20px; border-radius: 6px; "
                "cursor: pointer; font-size: 0.9rem; transition: background 0.3s; }"
                ".btn:hover { background: #1976D2; }"
                
                // Footer
                ".footer { position: fixed; bottom: 0; left: 0; right: 0; background: #2c3e50; color: #fff; "
                "text-align: center; padding: 1rem; font-size: 0.8rem; }"
                
                // Version info
                ".version-info { text-align: center; color: #666; font-size: 0.8rem; margin-top: 20px; }"
                
                // Mobile responsiveness
                "@media (max-width: 600px) {"
                "  .container { padding: 10px; margin-top: 70px; }"
                "  .panel { padding: 12px; }"
                "  .control-group { margin-bottom: 8px; gap: 8px; }"
                "  .control-label { font-size: 0.9rem; flex: 0 0 110px; }"
                "  select { flex: 1; min-width: 0; padding: 6px; }"
                "  .btn { padding: 8px 15px; }"
                "  .volume-slider { height: 4px; }"
                "  .panel-header { font-size: 1.1rem; margin-bottom: 10px; }"
                "}"
                
                // Station sign container
                ".station-sign { background: linear-gradient(180deg, #ffffff 0%, #f0f0f0 100%); "
                "margin: 0 0 20px 0; padding: 15px 15px 0; position: relative; border-radius: 10px; "
                "box-shadow: 0 2px 4px rgba(0,0,0,0.05); overflow: hidden; }"
                
                // Content container
                ".station-content { position: relative; width: 100%; display: flex; justify-content: center; "
                "max-width: 800px; margin: 0 auto; }"
                
                // Line marker (AKB/JY03)
                ".line-marker { position: absolute; top: 0; transform: translateX(-100%) translateX(-40px); "
                "width: 48px; height: 65px; background: " + String(lineMarkerBgColor) + "; border-radius: 8px; "
                "box-shadow: 0 2px 4px rgba(0,0,0,0.2); }"
                
                ".line-code-akb { position: absolute; top: 0; left: 0; right: 0; color: white; "
                "font-weight: bold; font-size: 14px; text-align: center; }"
                
                ".station-number-container { position: absolute; top: 23px; left: 4px; right: 4px; bottom: 4px; "
                "background: " + String(lineNumberBgColor) + "; border-radius: 6px; padding: 3px; }"
                
                ".station-number-inner { background: white; border-radius: 4px; height: 100%; width: 100%; "
                "display: flex; flex-direction: column; justify-content: center; align-items: center; }"
                
                ".line-code-jy { color: #000; font-weight: bold; font-size: 14px; line-height: 1; margin-bottom: 2px; }"
                ".line-number { color: #000; font-weight: bold; font-size: 15px; line-height: 1; }"
                
                // Station info
                ".station-info { position: relative; display: flex; flex-direction: column; align-items: center; }"
                
                ".station-name-ja { position: relative; display: inline-block; font-size: 38px; font-weight: bold; "
                "line-height: 1.2; font-family: 'MS Gothic', 'Yu Gothic', sans-serif; margin-bottom: 3px; }"
                
                ".station-name-hiragana { font-size: 16px; color: #333; margin-bottom: 3px; text-align: center; }"
                ".station-name-ko { font-size: 14px; color: #666; margin-bottom: 12px; text-align: center; }"
                
                // Ward label
                ".ward-label { position: absolute; top: 15px; right: 50px; display: flex; gap: 1px; }"
                
                // Box with white background - made square with border radius
                ".ward-box { background: white; color: black; width: 16px; height: 16px; "
                "border: 1px solid black; border-radius: 2px; display: flex; align-items: center; "
                "justify-content: center; font-size: 12px; }"
                
                // Black background text - matched to box dimensions
                ".ward-text { background: black; color: white; width: 16px; height: 16px; "
                "border-radius: 2px; display: flex; align-items: center; justify-content: center; "
                "font-size: 12px; }"
                
                // Direction bar
                ".direction-bar { background: " + String(directionBarBgColor) + "; margin: 0 -15px; height: 32px; "
                "display: flex; justify-content: space-between; align-items: center; color: white; position: relative; "
                "clip-path: polygon(0 0, calc(100% - 15px) 0, 100% 50%, calc(100% - 15px) 100%, 0 100%); "
                "overflow: visible; }"
                
                // Station indicator
                ".station-indicator { position: absolute; top: 0; left: 50%; transform: translateX(-50%); "
                "width: 32px; height: 32px; background: " + String(stationIndicatorColor) + "; }"
                
                // Direction stations
                ".direction-left { padding-left: 15px; width: 33%; text-align: left; }"
                ".direction-right { padding-right: 20px; width: 33%; text-align: right; }"
                ".direction-station { font-size: 16px; font-weight: bold; }"
                
                // English station names - consistent styling for all
                ".station-name-en { font-size: 16px; color: #333; }"
                ".station-name-en.current { font-weight: bold; }"
                ".station-name-en-left { width: 33%; text-align: left; }"
                ".station-name-en-center { width: 33%; text-align: center; }"
                ".station-name-en-right { width: 33%; text-align: right; }"
                ".station-names-container { display: flex; justify-content: space-between; padding: 5px 15px; }"
                
                // Direction bar and names container - maintain center alignment
                ".direction-bar, .station-names-container { width: 100%; max-width: 700px; margin: 0 auto; }"
                
                // Control layout
                ".controls-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; margin-bottom: 15px; }"

                // Control items
                ".control-item { display: flex; align-items: center; gap: 10px; }"
                ".control-item.right { justify-content: flex-end; }"
                ".control-item label { font-size: 0.95rem; color: #555; }"

                // Volume container
                ".volume-container { grid-column: 1 / -1; display: flex; align-items: center; gap: 15px; }"

                // Button styles
                ".btn-primary { background: #2196F3; color: white; }"
                ".btn-secondary { background: #757575; color: white; }"
                ".btn-warning { background: #ff9800; color: white; }"
                ".btn { border: none; padding: 10px 15px; border-radius: 6px; cursor: pointer; "
                "font-size: 0.9rem; transition: all 0.3s; display: flex; align-items: center; justify-content: center; }"
                ".btn:hover { filter: brightness(1.1); }"
                
                "</style></head><body>"
                
                "<div class='navbar'><h1>" + 
                String("\xE7\x99\xBA\xE8\xBB\x8A\xE3\x83\x99\xE3\x83\xAB") + // 発車ベル
                "</h1></div>"
                "<div class='container'>";

  // Add Station Sign
  html += "<div class='station-sign'>"
          "<div class='station-content'>"
          "<div class='line-marker'>"
          "<div class='line-code-akb'>" + String(lineCode) + "</div>"
          "<div class='station-number-container'>"
          "<div class='station-number-inner'>"
          "<div class='line-code-jy'>" + String(stationCodeLine) + "</div>"
          "<div class='line-number'>" + String(stationNumber) + "</div>"
          "</div>"
          "</div>"
          "</div>"
          "<div class='station-info'>"
          "<div class='station-name-ja'>" + String(stationNameJa) + "</div>"
          "<div class='station-name-hiragana'>" + String(stationNameHiragana) + "</div>"
          "<div class='station-name-ko'>" + String(stationNameKo) + "</div>"
          "</div>"
          "</div>"
          "<div class='ward-label'>"
          "<div class='ward-box'>" + String(wardBox) + "</div>"
          "<div class='ward-text'>" + String(wardLabel).substring(3) + "</div>"
          "</div>"
          "<div class='direction-bar'>"
          "<div class='station-indicator'></div>"
          "<div class='direction-left'>"
          "<span class='direction-station'>" + String(prevStation) + "</span>"
          "</div>"
          "<div class='direction-right'>"
          "<span class='direction-station'>" + String(nextStation) + "</span>"
          "</div>"
          "</div>"
          "<div class='station-names-container'>"
          "<span class='station-name-en station-name-en-left'>" + String(prevStationEn) + "</span>"
          "<span class='station-name-en station-name-en-center current'>" + String(stationNameEn) + "</span>"
          "<span class='station-name-en station-name-en-right'>" + String(nextStationEn) + "</span>"
          "</div>";

  // Audio Controls Panel
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
          
          // Random Play Toggle
          "<div class='control-item'>"
          "<label>Random Play</label>"
          "<div class='toggle-switch' onclick='toggleSwitch(\"toggleRandomPlay\")'>"
          "<input type='checkbox' id='toggleRandomPlay' " + String(RandomPlayOn ? "checked" : "") + ">"
          "<span class='slider'></span>"
          "</div>"
          "</div>"
          
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
          
          // Platform Announcement Button
          "<div class='control-item' style='grid-column: 1 / -1;'>"
          "<button class='btn btn-primary' style='width: 100%;' onclick='playVA()'>"
          "Play Platform Announcement</button>"
          "</div>"
          
          "</div>"
          "</div>";

  // Version info
  html += "<div class='version-info'>Firmware Version: " FW_VERSION "</div>";

  // Footer
  html += "<div class='footer'>&copy; 2024 " + 
          String("\xE7\x99\xBA\xE8\xBB\x8A\xE3\x83\x99\xE3\x83\xAB") + // 発車ベル
          "</div>";

  // JavaScript (keep existing JavaScript)
  html += "<script>"
          "function updateConfig() {"
          "  var melodyValue = document.getElementById('melody').value;"
          "  var atosValue = document.getElementById('atos').value;"
          "  var doorchimeValue = document.getElementById('doorchime').value;"
          "  var platformValue = document.getElementById('platform').value;"
          "  fetch('/updateConfig?melody=' + melodyValue + '&atos=' + atosValue + "
          "'&doorchime=' + doorchimeValue + '&platform=' + platformValue, { method: 'GET' });"
          "}"

          "function toggleSwitch(endpoint) {"
          "  var checkbox = document.getElementById(endpoint);"
          "  checkbox.checked = !checkbox.checked;"
          "  fetch('/' + endpoint + '?state=' + (checkbox.checked ? 'on' : 'off'), { method: 'GET' });"
          "  setTimeout(function() { location.reload(); }, 500);"
          "}"

          "function playVA() {"
          "  fetch('/playVA', { method: 'GET' });"
          "}"

          "function setVolume(value) {"
          "  fetch('/setVolume?value=' + value, { method: 'GET' });"
          "  document.getElementById('volumeValue').textContent = value;"
          "}"

          "function reinitDFPlayer() {"
          "  fetch('/reinitDFPlayer', { method: 'GET' });"
          "  alert('DFPlayer reinitialization requested');"
          "}"
          "</script></body></html>";

  server.send(200, "text/html", html);
}




String populateOptions(const char* folderName, int count, int selectedOption) {
  String options = "";
  for (int i = 1; i <= count; ++i) {
    options += "<option value='" + String(i) + "' " + (i == selectedOption ? "selected" : "") + ">" + String(i) + "</option>";
  }
  return options;
}




//=========================UpdateFileCount==========================

void UpdateFileCount() {
   Serial.println(F("=======Audio File Count:======="));

   MelodyCount = CheckFileInFolder(MelodyFolder);
    Serial.printf_P(PSTR("Melody Files: %d\n"), MelodyCount);

   AtosCount = CheckFileInFolder(AtosFolder);
    Serial.printf_P(PSTR("Atos Files: %d\n"), AtosCount);

    DoorChimeCount = CheckFileInFolder(DoorChimeFolder);
    Serial.printf_P(PSTR("DoorChime Files: %d\n"), DoorChimeCount);

   VACount = CheckFileInFolder(VAFolder);
    Serial.printf_P(PSTR("VA Files: %d\n"), VACount);
    
   Serial.println(F("==============================="));
}

//=========================PlayCurrentAudio==========================


void PlayCurrentAudio(int folder, int audioIndex) {
  Serial.printf_P(PSTR("PlayingFolder: %d\n"), folder);
  Serial.printf_P(PSTR("Playing Audio: %d\n"), audioIndex);
  myDFPlayer.playFolder(folder, audioIndex);
  delay(1000);
}


//=========================PlayRandomAudio==========================

void PlayRandomAudio(int folder, int TotalAudioCount) {

   int RandomAudio = generateRandomNumber(1, TotalAudioCount);
    Serial.printf_P(PSTR("PlayingFolder: %d\n"), folder);
    Serial.printf_P(PSTR("RandomPlay: %d/%d\n"), RandomAudio, TotalAudioCount);
    myDFPlayer.playFolder(folder, RandomAudio);
    delay(1000); // Waiting for mp3 board
}

//=========GenerateRandomNumber=========
int generateRandomNumber(int min, int max) {
  return random(min, max + 1); // The +1 ensures that the upper bound is inclusive
}



//=========================CheckFileInFolder==========================



int CheckFileInFolder(int folder) {
  int fileCount;
  do {
    fileCount = myDFPlayer.readFileCountsInFolder(folder);
    fileCount = myDFPlayer.readFileCountsInFolder(folder); //bug fix! run it twice to get the correct value
  } while (fileCount == -1);
  return fileCount;
}



//================================Main Button Func=============================

void handleButton() {
    // Use millis() once instead of multiple calls
    unsigned long currentTime = millis();
  int reading = digitalRead(MainButtonPin);

  if (reading != lastMainButtonState) {
        lastDebounceTime = currentTime;
  }

    if ((currentTime - lastDebounceTime) > debounceDelay && reading != MainButtonState) {
      MainButtonState = reading;

        if (MainButtonState == LOW) {
            if (!loopPlaying) {
          handlePlayMelody();
                myDFPlayer.enableLoop(); // Call only once
                loopPlaying = true;
                AtosLastPlayed = false;
            }
        } else if (loopPlaying) {
            myDFPlayer.disableLoop();
            myDFPlayer.stop();
          loopPlaying = false;
            handlePlayAtos();
          AtosLastPlayed = true;
    }
  }

  lastMainButtonState = reading;
}



//================================toggleRandomPlay=============================

void toggleRandomPlay() {
  Serial.print("Toggling RandomPlay to ");
  Serial.println(RandomPlayOn ? "off" : "on");
  RandomPlayOn = !RandomPlayOn;
}

//================================Handle Audio=============================

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
    if (DonePlaying == true && AtosLastPlayed == true) {// Atos has finished playing, set the flag
      Serial.println("====Door Chime playing====");
       PlayCurrentAudio(DoorChimeFolder, 1);
       DonePlaying = false; AtosLastPlayed = false;
    }
  }





//================================MP3-Player-Sleep=============================
void DFPSleep(){if (DonePlaying == true){myDFPlayer.sleep();Serial.println(F("==MP3 Board going to sleep=="));}}


//================================MP3-Player-details=============================
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

// Add reconnection logic
void ensureWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(ssid, pass);
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && 
               millis() - startAttemptTime < 10000) {
            delay(100);
        }
    }
}
