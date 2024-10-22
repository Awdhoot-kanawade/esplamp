#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DFRobotDFPlayerMini.h>
#include <TM1637Display.h>  // TM1637 display library
#include "SoftwareSerial.h"

// Pin definitions for TM1637
#define CLK_PIN 18     // Clock pin (DIO)
#define DIO_PIN 19     // Data pin (CLK)
TM1637Display display(CLK_PIN, DIO_PIN);

// Pin definitions for Physical Buttons
#define ALARM_OFF_PIN 25  // GPIO for alarm off button
#define VOLUME_UP_PIN 26  // GPIO for volume up button
#define VOLUME_DOWN_PIN 27  // GPIO for volume down button

// Pin definitions for NeoPixel and Bluetooth
#define LED_PIN 5
#define BLUETOOTH_PIN 16  // GPIO for Bluetooth module control
static const uint8_t PIN_MP3_TX = 26; // Connects to module's RX 
static const uint8_t PIN_MP3_RX = 27; // Connects to module's TX 
SoftwareSerial softwareSerial(PIN_MP3_RX, PIN_MP3_TX);

// NeoPixel setup
#define NUM_PIXELS 30
Adafruit_NeoPixel strip(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// RTC setup
RTC_DS3231 rtc;

// NTP setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "192.168.31.117", 19800, 60000); // GMT+5:30

// Web server
WebServer server;

// Preferences for persistent storage
Preferences preferences;

// DFPlayer for alarm sound playback
DFRobotDFPlayerMini dfPlayer;

// Variables
String wakeUpTime = "06:30";
String bedTime = "22:30";
bool wakeUpAlarmEnabled = true;
bool bedTimeAlarmEnabled = true;
int currentVolume = 10;
int currentBrightness = 255;
int sunriseDuration = 30; // in minutes
int sunsetDuration = 30;  // in minutes
int ledColor = 0xFFFFFF;  // Default color (white)
String ledEffect = "solid";  // Default LED effect (solid)

// Days of the week flags (true if alarm is active on that day)
bool alarmDays[7] = {false, false, false, false, false, false, false}; // Sun, Mon, Tue, Wed, Thu, Fri, Sat

// Bluetooth control
bool bluetoothEnabled = false;  // Control the external Bluetooth module
bool bluetoothPreviousState = false;  // To store the Bluetooth state before the alarm

void setup() {
  Serial.begin(115200);

  // Initialize preferences
  preferences.begin("my-app", false);
  
  // Load saved preferences
  currentVolume = preferences.getInt("volume", 10);
  currentBrightness = preferences.getInt("brightness", 255);
  wakeUpTime = preferences.getString("wakeUpTime", "06:30");
  bedTime = preferences.getString("bedTime", "22:30");
  wakeUpAlarmEnabled = preferences.getBool("wakeUpAlarmEnabled", true);
  bedTimeAlarmEnabled = preferences.getBool("bedTimeAlarmEnabled", true);
  
  // Load alarm days preferences correctly using c_str()
  for (int i = 0; i < 7; i++) {
    String key = "alarmDay" + String(i);
    alarmDays[i] = preferences.getBool(key.c_str(), false);
  }

  sunriseDuration = preferences.getInt("sunriseDuration", 30);
  sunsetDuration = preferences.getInt("sunsetDuration", 30);

  // Set up NeoPixel strip
  strip.begin();
  strip.show();

  // Set up RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  // Set up DFPlayer Mini for SD card playback (serial communication)
 
  softwareSerial.begin(9600);
  if (!dfPlayer.begin(softwareSerial)) {  // Start communication with DFPlayer
    Serial.println("DFPlayer Mini not found!");
    while (1);
  }
  dfPlayer.volume(currentVolume);  // Set initial volume for DFPlayer

  // Initialize GPIO for controlling Bluetooth module
  pinMode(BLUETOOTH_PIN, OUTPUT);
  digitalWrite(BLUETOOTH_PIN, LOW);  // Turn off Bluetooth module initially

  // Initialize physical buttons
  pinMode(ALARM_OFF_PIN, INPUT_PULLUP);
  pinMode(VOLUME_UP_PIN, INPUT_PULLUP);
  pinMode(VOLUME_DOWN_PIN, INPUT_PULLUP);

  // Initialize TM1637 display for showing the current time
  display.setBrightness(0x0f);  // Maximum brightness

  // Connect to Wi-Fi in AP+STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32AlarmClock");
  WiFi.begin("AirFiber-8bLTLv5.0", "Pass@123");

  // Initialize NTP client
  timeClient.begin();
  
  // Start web server
  server.on("/", handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();
  
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
  timeClient.update();
  
  // Check current time from RTC
  DateTime now = rtc.now();
  String currentTime = formatTime(now.hour(), now.minute());
  
  // Display the current time on TM1637 display
  display.showNumberDecEx(now.hour() * 100 + now.minute(), 0x80, true);  // Display in HH:MM format with colon
  
  // Check for button presses
  if (digitalRead(ALARM_OFF_PIN) == LOW) {
    stopAlarm();  // Turn off alarm if button is pressed
  }
  if (digitalRead(VOLUME_UP_PIN) == LOW) {
    increaseVolume();  // Increase volume if button is pressed
  }
  if (digitalRead(VOLUME_DOWN_PIN) == LOW) {
    decreaseVolume();  // Decrease volume if button is pressed
  }

  // Get current day of the week (0 = Sunday, 1 = Monday, ..., 6 = Saturday)
  int currentDay = now.dayOfTheWeek();
  checkAlarms(currentTime, currentDay);
}

// Increase volume function (physical button)
void increaseVolume() {
  currentVolume = min(currentVolume + 1, 30);  // DFPlayer supports volume levels from 0 to 30
  dfPlayer.volume(currentVolume);
  preferences.putInt("volume", currentVolume);
  Serial.println("Volume increased: " + String(currentVolume));
}

// Decrease volume function (physical button)
void decreaseVolume() {
  currentVolume = max(currentVolume - 1, 0);  // DFPlayer supports volume levels from 0 to 30
  dfPlayer.volume(currentVolume);
  preferences.putInt("volume", currentVolume);
  Serial.println("Volume decreased: " + String(currentVolume));
}

// Handle root HTML page
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE HTML>
<html>
<head>
  <title>ESP32 Alarm Clock</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f4f4f9;
      color: #333;
      text-align: center;
      padding: 20px;
    }
    h1 {
      color: #444;
    }
    .container {
      display: inline-block;
      background: #fff;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0px 0px 10px rgba(0, 0, 0, 0.1);
    }
    input[type="time"], input[type="color"], input[type="range"], select {
      width: 70%;
      padding: 8px;
      margin: 15px 0; /* Increased margin for better spacing */
      border-radius: 5px;
      border: 1px solid #ccc;
    }
    button {
      background-color: #4CAF50;
      color: white;
      border: none;
      padding: 10px 20px;
      border-radius: 5px;
      cursor: pointer;
      margin-top: 20px; /* Added margin for spacing */
    }
    button:hover {
      background-color: #45a049;
    }
    .slider {
      position: relative;
      width: 80px; /* Adjust width as needed */
      height: 34px;
      margin: 5px 0; /* Added margin */
      
    }
    .slider input { display: none; }
    .slider .bar {
      position: absolute;
      cursor: pointer;
      background-color: #d3d3d3; /* Light gray background */
      border-radius: 30px;
      width: 60px;
      height: 34px;
      
      
    }
    .slider .toggle {
      position: absolute;
      background-color: green; /* Keep toggle color white */
      border-radius: 50%;
      height: 30px; /* Increased height */
      width: 30px;  /* Increased width */
      top: 2px;     /* Adjusted top position */
      left: 2px;    /* Adjusted left position */
      transition: .4s;
    }
    input:checked + .bar {
      background-color: #4CAF50; /* Change bar color when checked */
    }
    input:checked + .bar .toggle {
      transform: translateX(26px); /* Move toggle when checked */
    }
  </style>
</head>
<body>
  <h1>ESP32 Alarm Clock</h1>
  <div class="container">
    <label for="wakeUpTime">Wake-Up Time:</label><br>
    <input type="time" id="wakeUpTime" value=")" + wakeUpTime + R"(" onchange="updateValue('wakeUpTime')"><br>

    <input type="checkbox" id="wakeUpAlarmEnabled" onchange="updateValue('wakeUpAlarmEnabled')")" + String(wakeUpAlarmEnabled ? " checked" : "") + R"("> Enable Wake-Up Alarm<br>

    <label for="bedTime">Bed Time:</label><br>
    <input type="time" id="bedTime" value=")" + bedTime + R"(" onchange="updateValue('bedTime')"><br>

    <input type="checkbox" id="bedTimeAlarmEnabled" onchange="updateValue('bedTimeAlarmEnabled')")" + String(bedTimeAlarmEnabled ? " checked" : "") + R"("> Enable Bedtime Alarm<br>

    <h3>Days for Alarm:</h3>
    <label for="alarmDay0">Sunday</label><input type="checkbox" id="alarmDay0" onchange="updateValue('alarmDay0')" )" + String(alarmDays[0] ? " checked" : "") + R"("><br>
    <label for="alarmDay1">Monday</label><input type="checkbox" id="alarmDay1" onchange="updateValue('alarmDay1')" )" + String(alarmDays[1] ? " checked" : "") + R"("><br>
    <label for="alarmDay2">Tuesday</label><input type="checkbox" id="alarmDay2" onchange="updateValue('alarmDay2')" )" + String(alarmDays[2] ? " checked" : "") + R"("><br>
    <label for="alarmDay3">Wednesday</label><input type="checkbox" id="alarmDay3" onchange="updateValue('alarmDay3')" )" + String(alarmDays[3] ? " checked" : "") + R"("><br>
    <label for="alarmDay4">Thursday</label><input type="checkbox" id="alarmDay4" onchange="updateValue('alarmDay4')" )" + String(alarmDays[4] ? " checked" : "") + R"("><br>
    <label for="alarmDay5">Friday</label><input type="checkbox" id="alarmDay5" onchange="updateValue('alarmDay5')" )" + String(alarmDays[5] ? " checked" : "") + R"("><br>
    <label for="alarmDay6">Saturday</label><input type="checkbox" id="alarmDay6" onchange="updateValue('alarmDay6')" )" + String(alarmDays[6] ? " checked" : "") + R"("><br>

    <label for="volume">Volume:</label><br>
    <input type="range" id="volume" min="0" max="100" value=")" + String(currentVolume) + R"(" onchange="updateValue('volume')"><span id="volumeValue">)" + String(currentVolume) + R"(</span><br>

    <label for="brightness">Brightness:</label><br>
    <input type="range" id="brightness" min="0" max="255" value=")" + String(currentBrightness) + R"(" onchange="updateValue('brightness')"><span id="brightnessValue">)" + String(currentBrightness) + R"(</span><br>

    <label for="ledColor">LED Color:</label><br>
    <input type="color" id="ledColor" value="#ffffff" onchange="updateValue('ledColor')"><br>

    <label for="ledEffect">LED Effect:</label><br>
    <select id="ledEffect" onchange="updateValue('ledEffect')">
      <option value="solid" )" + (ledEffect == "solid" ? "selected" : "") + R"(>Solid</option>
      <option value="blink" )" + (ledEffect == "blink" ? "selected" : "") + R"(>Blink</option>
      <option value="fade" )" + (ledEffect == "fade" ? "selected" : "") + R"(>Fade</option>
    </select><br>

    <h3>Bluetooth Module:</h3>
    <label class="slider">
      <input type="checkbox" id="bluetoothEnabled" onchange="updateValue('bluetoothEnabled')" )" + String(bluetoothEnabled ? " checked" : "") + R"(">
      <span class="bar"><span class="toggle"></span></span>
    </label><br>

    <button onclick="sendCommand('stopAlarm')">Stop Alarm</button><br>
    <button onclick="sendCommand('syncTime')">Sync Time</button><br>
  </div>
</body>
<script>
  function updateValue(type) {
    const value = document.getElementById(type).type === 'checkbox' ? document.getElementById(type).checked : document.getElementById(type).value;
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/update', true);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.send(type + '=' + value);
  }

  function sendCommand(command) {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/command', true);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.send('command=' + command);
  }
</script>
</html>

  )rawliteral";
  server.send(200, "text/html", html);
}

// Handle updates from sliders, checkboxes, and Bluetooth toggle
void handleUpdate() {
  if (server.hasArg("wakeUpTime")) {
    wakeUpTime = server.arg("wakeUpTime");
    preferences.putString("wakeUpTime", wakeUpTime);
    Serial.println("Wake-Up Time set to: " + wakeUpTime);
  }
  if (server.hasArg("wakeUpAlarmEnabled")) {
    wakeUpAlarmEnabled = (server.arg("wakeUpAlarmEnabled") == "true");
    preferences.putBool("wakeUpAlarmEnabled", wakeUpAlarmEnabled);
    Serial.println("Wake-Up Alarm Enabled: " + String(wakeUpAlarmEnabled));
  }
  if (server.hasArg("bedTime")) {
    bedTime = server.arg("bedTime");
    preferences.putString("bedTime", bedTime);
    Serial.println("Bedtime set to: " + bedTime);
  }
  if (server.hasArg("bedTimeAlarmEnabled")) {
    bedTimeAlarmEnabled = (server.arg("bedTimeAlarmEnabled") == "true");
    preferences.putBool("bedTimeAlarmEnabled", bedTimeAlarmEnabled);
    Serial.println("Bedtime Alarm Enabled: " + String(bedTimeAlarmEnabled));
  }
  
  // Update alarm days and store the boolean values in preferences using c_str()
  for (int i = 0; i < 7; i++) {
    String arg = "alarmDay" + String(i);
    if (server.hasArg(arg)) {
      alarmDays[i] = (server.arg(arg) == "true");
      preferences.putBool(arg.c_str(), alarmDays[i]);  // Use c_str() for C-style string
    }
  }

  if (server.hasArg("volume")) {
    currentVolume = server.arg("volume").toInt();
    Serial.println("Volume set to: " + String(currentVolume));
    preferences.putInt("volume", currentVolume);
    dfPlayer.volume(currentVolume);  // Set DFPlayer volume
  }
  if (server.hasArg("brightness")) {
    currentBrightness = server.arg("brightness").toInt();
    setBrightness(currentBrightness);
    preferences.putInt("brightness", currentBrightness);
  }
  if (server.hasArg("ledColor")) {
    ledColor = strtoul(server.arg("ledColor").substring(1).c_str(), NULL, 16);  // Convert hex color to int
    Serial.println("LED Color set to: #" + server.arg("ledColor"));
  }
  if (server.hasArg("ledEffect")) {
    ledEffect = server.arg("ledEffect");
    Serial.println("LED Effect set to: " + ledEffect);
  }
  
  // Bluetooth module control
  if (server.hasArg("bluetoothEnabled")) {
    bluetoothEnabled = (server.arg("bluetoothEnabled") == "true");
    digitalWrite(BLUETOOTH_PIN, bluetoothEnabled ? HIGH : LOW);  // Toggle Bluetooth module
    Serial.println("Bluetooth Module " + String(bluetoothEnabled ? "Enabled" : "Disabled"));
    dfPlayer.play(1);
    startSunrise();
    
  }

  server.send(200, "text/plain", "Updated");
}

// Handle commands such as stop alarm and sync RTC
void handleCommand() {
  if (server.hasArg("command")) {
    String command = server.arg("command");
    if (command == "stopAlarm") {
      stopAlarm();
    } else if (command == "syncTime") {
      rtc.adjust(DateTime(timeClient.getEpochTime()));  // Sync RTC with NTP time
      Serial.println("RTC time synced with NTP");
    }
  }
  server.send(200, "text/plain", "Command executed");
}

// Stop alarm - stop audio and turn off LEDs
void stopAlarm() {
  Serial.println("Alarm stopped");
  strip.clear();
  strip.show();
  dfPlayer.stop();  // Stop DFPlayer audio
  
  // Revert Bluetooth to previous state
  digitalWrite(BLUETOOTH_PIN, bluetoothPreviousState ? HIGH : LOW);
  bluetoothEnabled = bluetoothPreviousState;
  Serial.println("Reverting Bluetooth to previous state: " + String(bluetoothPreviousState ? "ON" : "OFF"));
}

// Set brightness for the LED strip
void setBrightness(int brightness) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(brightness, brightness, brightness));  // Set all pixels to white
  }
  strip.show();
  Serial.println("Brightness set to: " + String(brightness));
}

// Check for alarm triggers based on current time and selected day
void checkAlarms(String currentTime, int currentDay) {
  if (wakeUpAlarmEnabled && alarmDays[currentDay] && currentTime == wakeUpTime) {
    Serial.println("Wake-Up Alarm triggered!");
    
    // Store Bluetooth state and turn it off if it's on
    bluetoothPreviousState = bluetoothEnabled;
    if (bluetoothEnabled) {
      digitalWrite(BLUETOOTH_PIN, LOW);  // Turn off Bluetooth module during the alarm
      bluetoothEnabled = false;
      Serial.println("Bluetooth turned off during the alarm");
    }
    
    startSunrise();
    dfPlayer.play(1);  // Play track 1 from SD card (change track number as needed)
  }
  if (bedTimeAlarmEnabled && alarmDays[currentDay] && currentTime == bedTime) {
    Serial.println("Bedtime Alarm triggered!");
    
    // Store Bluetooth state and turn it off if it's on
    bluetoothPreviousState = bluetoothEnabled;
    if (bluetoothEnabled) {
      digitalWrite(BLUETOOTH_PIN, LOW);  // Turn off Bluetooth module during the alarm
      bluetoothEnabled = false;
      Serial.println("Bluetooth turned off during the alarm");
    }
    
    startSunset();
    dfPlayer.play(2);  // Play track 2 from SD card (change track number as needed)
  }
}

// Helper function to format time for comparison
String formatTime(int hour, int minute) {
  String h = (hour < 10) ? "0" + String(hour) : String(hour);
  String m = (minute < 10) ? "0" + String(minute) : String(minute);
  return h + ":" + m;
}

// Simulate sunrise effect with increasing brightness
void startSunrise() {
  for (int i = 0; i < 100; i++) {
    int brightness = map(i, 0, 100, 0, currentBrightness);
    setBrightness(brightness);
    delay(sunriseDuration * 600);  // Convert minutes to milliseconds
  }
  Serial.println("Sunrise simulation completed");
}

// Simulate sunset effect with decreasing brightness
void startSunset() {
  for (int i = 100; i > 0; i--) {
    int brightness = map(i, 0, 100, 0, currentBrightness);
    setBrightness(brightness);
    delay(sunsetDuration * 600);  // Convert minutes to milliseconds
  }
  Serial.println("Sunset simulation completed");
}
