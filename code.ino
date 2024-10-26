#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DFPlayerMini_Fast.h>
#include <TM1637Display.h>  // TM1637 display library

// Audio Switch

const int Audio_SW = 26;

// Alarm stop 

int Alarm_btn_flag = 0;


// Pin definitions for TM1637
#define CLK_PIN 18     // Clock pin (DIO)
#define DIO_PIN 19     // Data pin (CLK)
TM1637Display display(CLK_PIN, DIO_PIN);

// Pin definitions for Physical Buttons
#define ALARM_OFF_PIN 23  // GPIO for alarm off button
#define VOLUME_UP_PIN 28 // GPIO for volume up button
#define VOLUME_DOWN_PIN 27  // GPIO for volume down button

// Pin definitions for NeoPixel and Bluetooth
#define LED_PIN 5
#define BLUETOOTH_PIN 16  // GPIO for Bluetooth module control


// NeoPixel setup
#define NUM_PIXELS 78
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
#define FPSerial Serial1
DFPlayerMini_Fast dfPlayer;

// Variables
String wakeUpTime = "06:30";
String bedTime = "22:30";
bool wakeUpAlarmEnabled = true;
bool bedTimeAlarmEnabled = true;
int currentVolume = 10;
int currentBrightness = 255;
int sunriseDuration = 10; // in minutes
int sunsetDuration = 10;  // in minutes
int ledColor = 0xFFFFFF;  // Default color (white)
String ledEffect = "solid";  // Default LED effect (solid)

// Days of the week flags (true if alarm is active on that day)
bool alarmDays[7] = {false, false, false, false, false, false, false}; // Sun, Mon, Tue, Wed, Thu, Fri, Sat

// Bluetooth control
bool bluetoothEnabled = false;  // Control the external Bluetooth module
bool bluetoothPreviousState = false;  // To store the Bluetooth state before the alarm

void IRAM_ATTR isr() 
{
	Alarm_btn_flag=1;
}

void setup() {
  Serial.begin(115200);
  attachInterrupt(ALARM_OFF_PIN, isr, RISING);

  pinMode (Audio_SW, OUTPUT);
  digitalWrite (Audio_SW, LOW);

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
 
  FPSerial.begin(9600, SERIAL_8N1, 16, 17); // Start serial communication for ESP32 with 9600 baud rate, 8 data bits, no parity, and 1 stop bit
  if (!dfPlayer.begin(FPSerial)) { // Initialize the DFPlayer Mini with the defined serial interface
    Serial.println(F("Unable to begin:")); // If initialization fails, print an error message
    while(1); 
  }
  //dfPlayer.volume(currentVolume);  // Set initial volume for DFPlayer
   dfPlayer.volume(30);

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

  if(Alarm_btn_flag==1)
  {
    strip.clear();
    strip.show();
    Alarm_btn_flag =0;
  }
  
  
  // Check current time from RTC
  DateTime now = rtc.now();
  String currentTime = formatTime(now.hour(), now.minute());
  
  // Display the current time on TM1637 display
  // Display the time in HH:MM format with colon
  show_time();
  
  
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

void show_time()
{ 
  DateTime now = rtc.now();
  int hour = now.hour();
  bool isPM = false;
  
  
  
  if (hour >= 12) {
    isPM = true;  // PM time
    if (hour > 12) {
      hour -= 12;  // Convert 13-23 hours to 1-11 PM
    }
  } else if (hour == 0) {
    hour = 12;  // Midnight is 12 AM in 12-hour format
  }

  display.showNumberDecEx(hour * 100 + now.minute(), 0b01000000, true);// Display in HH:MM format with colon
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

  // Immediately apply the selected color to the LED strip
  if (server.hasArg("ledColor")) {
    ledColor = strtoul(server.arg("ledColor").substring(1).c_str(), NULL, 16);  // Convert hex color to int
    Serial.println("LED Color set to: " + server.arg("ledColor"));
    applyColorToLEDs(ledColor);  // Apply the selected color to the LED strip
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
  }

  server.send(200, "text/plain", "Updated");
}

// Function to apply the selected color to the LED strip immediately
void applyColorToLEDs(int color) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);  // Set each pixel to the selected color
  }
  strip.show();  // Refresh the LED strip to display the new color
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
  // Extract the red, green, and blue components from the color
  int red = (ledColor >> 16) & 0xFF;   // Get red component
  int green = (ledColor >> 8) & 0xFF;  // Get green component
  int blue = ledColor & 0xFF;          // Get blue component

  // Scale each color component based on the brightness level
  red = (red * brightness) / 255;
  green = (green * brightness) / 255;
  blue = (blue * brightness) / 255;

  // Apply the adjusted color to each pixel in the LED strip
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(red, green, blue));  // Set pixel with new brightness
  }
  strip.show();  // Refresh the LED strip
  Serial.println("Brightness adjusted with color preserved.");
}

// Check for alarm triggers based on current time and selected day
void checkAlarms(String currentTime, int currentDay) {
  if (wakeUpAlarmEnabled && alarmDays[currentDay] && currentTime == wakeUpTime) {
    Serial.println("Wake-Up Alarm triggered!");
    startSunrise();
     
  }
  if (bedTimeAlarmEnabled && alarmDays[currentDay] && currentTime == bedTime) {
    Serial.println("Bedtime Alarm triggered!");
    startSunset();
    
  }

    
}

// Helper function to format time for comparison
String formatTime(int hour, int minute) {
  String h = (hour < 10) ? "0" + String(hour) : String(hour);
  String m = (minute < 10) ? "0" + String(minute) : String(minute);
  return h + ":" + m;
}

// Function to interpolate between two colors
int interpolateColor(int color1, int color2, float fraction) {
  return color1 + (color2 - color1) * fraction;
}

// Function to gradually transition between two RGB colors
void applyGradient(int startColor[3], int endColor[3], int steps, int delayTime) {
  if (Alarm_btn_flag == 0)
  {
  for (int step = 0; step <= steps; step++) {
    if(Alarm_btn_flag ==1)
    {
      break;
    }
    float fraction = (float)step / (float)steps;

    int red = interpolateColor(startColor[0], endColor[0], fraction);
    int green = interpolateColor(startColor[1], endColor[1], fraction);
    int blue = interpolateColor(startColor[2], endColor[2], fraction);

    // Set color to all LEDs on the strip
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(red, green, blue));
    }
    strip.show();
    delay(delayTime);
    show_time();
    Serial.println(step);
    
  }
  Serial.print("Loop ended in gradient");
  }
}

// Sunrise simulation function
void startSunrise() {
  Serial.println("Starting Sunrise...");
  digitalWrite (Audio_SW, HIGH);
  delay(1000);
  dfPlayer.play(1);

  // Define color stages for sunrise
  /*int dark[3] = {0, 0, 0};          // Dark
  int orange[3] = {208, 72, 33};    // Orange (Dawn)
  int yellow[3] = {241, 244, 62};    // Yellow (Morning)
  int white[3] = {255, 255, 255};   // White (Daylight)*/

   int white[3] = {255, 255, 255};   // White (Daylight)
  int yellow[3] = {255, 255, 0};    // Yellow (Evening)
  int orange[3] = {255, 165, 0};    // Orange (Dusk)
  int dark[3] = {0, 0, 0};

  int steps = 100;  // Number of steps for transition
  int delayTime = (sunriseDuration * 60 * 1000) / (steps * 9); // Smooth transition time based on duration

  // Transition from dark to orange
  applyGradient(dark, orange, steps, delayTime);

  // Transition from orange to yellow
  applyGradient(orange, yellow, steps, delayTime);

  // Transition from yellow to white
  applyGradient(yellow, white, steps, delayTime);

  Serial.print("Sunrise loop ended");

  strip.clear();
  strip.show();
  dfPlayer.stop();
  Alarm_btn_flag = 0;

  Serial.println("Sunrise completed.");
  digitalWrite (Audio_SW, LOW);

}

// Sunset simulation function
void startSunset() {
  Serial.println("Starting Sunset...");
  digitalWrite (Audio_SW, HIGH);
  delay(1000);
  dfPlayer.play(2);

  // Define color stages for sunset (reverse of sunrise)
  int white[3] = {255, 255, 255};   // White (Daylight)
  int yellow[3] = {255, 255, 0};    // Yellow (Evening)
  int orange[3] = {255, 165, 0};    // Orange (Dusk)
  int dark[3] = {0, 0, 0};          // Dark (Night)

  int steps = 100;  // Number of steps for transition
  int delayTime = (sunsetDuration * 60 * 1000) / (steps * 3); // Smooth transition time based on duration

  // Transition from white to yellow
  applyGradient(white, yellow, steps, delayTime);

  // Transition from yellow to orange
  applyGradient(yellow, orange, steps, delayTime);

  // Transition from orange to dark
  applyGradient(orange, dark, steps, delayTime);

  strip.clear();
  strip.show();
  dfPlayer.stop();

  Serial.println("Sunset completed.");
  digitalWrite (Audio_SW, LOW);
  Alarm_btn_flag = 0;
  
}


void setBrightness_inc(int brightness) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(brightness, brightness, brightness));  // Set all pixels to white
  }
  strip.show();
  Serial.println("Brightness set to: " + String(brightness));
}
