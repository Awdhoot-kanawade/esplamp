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
#include "WiFiProv.h"

//Ble provisioning
String password = "";
String ssid = "";

const char *pop = "7507212399";                 // Proof of possession - otherwise called a PIN - string provided by the device, entered by the user in the phone app
const char *service_name = "Vaishu's Speaker";  // Name of your device (the Espressif apps expects by default device name starting with "Prov_")
const char *service_key = NULL;                 // Password used for SofAP method (NULL = no password needed)
bool reset_provisioned = true;                  // When true the library will automatically delete previously provisioned data.

// Audio Switch

const int Audio_SW = 26;

// Alarm stop

int Alarm_btn_flag = 0;


// Pin definitions for TM1637
#define CLK_PIN 18  // Clock pin (DIO)
#define DIO_PIN 19  // Data pin (CLK)
TM1637Display display(CLK_PIN, DIO_PIN);

// Pin definitions for Physical Buttons
#define ALARM_OFF_PIN 23  // GPIO for alarm off button


// Pin definitions for NeoPixel and Bluetooth
#define LED_PIN 5
#define BLUETOOTH_PIN 16  // GPIO for Bluetooth module control


// NeoPixel setup
#define NUM_PIXELS 78
Adafruit_NeoPixel strip(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// RTC setup
RTC_DS3231 rtc;

// NTP setup


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
int sunriseDuration = 20;    // in minutes
int sunsetDuration = 10;     // in minutes
int ledColor = 0xFFFFFF;     // Default color (white)
String ledEffect = "solid";  // Default LED effect (solid)
int Audio_Number = 1;

// Days of the week flags (true if alarm is active on that day)
bool alarmDays[7] = { false, false, false, false, false, false, false };  // Sun, Mon, Tue, Wed, Thu, Fri, Sat

// Bluetooth control
bool bluetoothEnabled = false;        // Control the external Bluetooth module
bool bluetoothPreviousState = false;  // To store the Bluetooth state before the alarm

void IRAM_ATTR isr() {
  Alarm_btn_flag = 1;
}

void SysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("\nConnected IP address : ");
      Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: Serial.println("\nDisconnected. Connecting to the AP again... "); break;
    case ARDUINO_EVENT_PROV_START: Serial.println("\nProvisioning started\nGive Credentials of your access point using smartphone app"); break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      {
        Serial.println("\nReceived Wi-Fi credentials");
        Serial.print("\tSSID : ");
        ///Serial.println((const char *)sys_event->event_info.prov_cred_recv.ssid);
        Serial.print("\tPassword : ");
        //Serial.println((char const *)sys_event->event_info.prov_cred_recv.password);
        password = ((char const *)sys_event->event_info.prov_cred_recv.password);
        ssid = ((const char *)sys_event->event_info.prov_cred_recv.ssid);

        Serial.print("\tSSID : ");
        Serial.print(ssid);
        Serial.print("\tPassword : ");
        Serial.print(password);
        preferences.putString("wifi_ssid", ssid);
        preferences.putString("wifi_password", password);


        break;
      }
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      {
        Serial.println("\nProvisioning failed!\nPlease reset to factory and retry provisioning\n");
        if (sys_event->event_info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) {
          Serial.println("\nWi-Fi AP password incorrect");
        } else {
          Serial.println("\nWi-Fi AP not found....Add API \" nvs_flash_erase() \" before beginProvision()");
        }
        break;
      }
    case ARDUINO_EVENT_PROV_CRED_SUCCESS: Serial.println("\nProvisioning Successful"); break;
    case ARDUINO_EVENT_PROV_END: Serial.println("\nProvisioning Ends"); break;
    default: break;
  }
}

// Declare functions to avoid "not declared in this scope" error
void handleRoot();
void handleUpdate();
void handleCommand();
void show_time();
void stopAlarm();
void checkAlarms(String currentTime, int currentDay);
void applyColorToLEDs(int color);
void setBrightness(int brightness);
void startSunrise();
void startSunset();
String formatTime(int hour, int minute);

// Rest of your code including the setup function


void setup() {
  Serial.begin(115200);
  attachInterrupt(ALARM_OFF_PIN, isr, RISING);

  pinMode(Audio_SW, OUTPUT);
  digitalWrite(Audio_SW, LOW);

  // Initialize preferences
  preferences.begin("my-app", false);

  ssid = preferences.getString("wifi_ssid", "ssid");
  password = preferences.getString("wifi_password", "pass@123456");

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
    while (1)
      ;
  }

  // Set up DFPlayer Mini for SD card playback (serial communication)

  FPSerial.begin(9600, SERIAL_8N1, 16, 17);  // Start serial communication for ESP32 with 9600 baud rate, 8 data bits, no parity, and 1 stop bit
  if (!dfPlayer.begin(FPSerial)) {           // Initialize the DFPlayer Mini with the defined serial interface
    Serial.println(F("Unable to begin:"));   // If initialization fails, print an error message
    while (1)
      ;
  }
  //dfPlayer.volume(currentVolume);  // Set initial volume for DFPlayer
  dfPlayer.volume(30);

  // Initialize GPIO for controlling Bluetooth module
  pinMode(BLUETOOTH_PIN, OUTPUT);
  digitalWrite(BLUETOOTH_PIN, LOW);  // Turn off Bluetooth module initially

  // Initialize physical buttons
  pinMode(ALARM_OFF_PIN, INPUT_PULLUP);


  // Initialize TM1637 display for showing the current time
  display.setBrightness(0x0f);  // Maximum brightness

  // Connect to Wi-Fi in AP+STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Vaishu's AlarmClock", "7507212399");
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi!");
  } else {
    WiFi.onEvent(SysProvEvent);
    Serial.println("Begin Provisioning using BLE");
    uint8_t uuid[16] = { 0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf, 0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02 };
    WiFiProv.beginProvision(NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BLE, NETWORK_PROV_SECURITY_1, pop, service_name, service_key, uuid, reset_provisioned);
    log_d("ble qr");
    WiFiProv.printQR(service_name, pop, "ble");
  }

  // Initialize NTP client


  // Start web server
  server.on("/", handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();

  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();


  if (Alarm_btn_flag == 1) {
    strip.clear();
    strip.show();
    Alarm_btn_flag = 0;
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


  // Get current day of the week (0 = Sunday, 1 = Monday, ..., 6 = Saturday)
  int currentDay = now.dayOfTheWeek();
  checkAlarms(currentTime, currentDay);
}

void show_time() {
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

  display.showNumberDecEx(hour * 100 + now.minute(), 0b01000000, true);  // Display in HH:MM format with colon
}

// Handle root HTML page
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE HTML>
<html>
<head>
  <title>Smart Alarm Clock</title>
  <style>
    /* Base styles */
    body {
      font-family: Arial, sans-serif;
      background-color: #f4f4f9;
      color: #333;
      text-align: center;
      padding: 10px;
      margin: 0;
    }
    h1 {
      color: #444;
      font-size: 1.5em;
      margin: 0.5em 0;
    }
    .container {
      display: inline-block;
      background: #fff;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0px 0px 10px rgba(0, 0, 0, 0.1);
      width: 90%;
      max-width: 400px;  /* Limits the max width for larger screens */
    }
    input[type="time"], input[type="color"], input[type="range"] {
      width: 100%;
      padding: 8px;
      margin: 10px 0;
      border-radius: 5px;
      border: 1px solid #ccc;
      box-sizing: border-box;
    }
    /* Specific styling for color input */
    input[type="color"] {
      height: 40px; /* Increased height for better visibility */
      cursor: pointer;
    }
    /* Make only specific labels bold */
    .label-bold {
      font-weight: bold;
    }
    .alarm-days label {
      font-weight: normal; /* Keeps weekday labels unbolded */
    }
    .spacer {
      margin-bottom: 10px; /* Adds space between elements */
    }
    .input-group {
      margin-bottom: 15px; /* Larger spacing for groups */
    }
    button {
      background-color: #4CAF50;
      color: white;
      border: none;
      padding: 12px 20px;
      border-radius: 5px;
      cursor: pointer;
      width: 100%;
      margin-top: 15px;
      font-size: 1em;
    }
    button:hover {
      background-color: #45a049;
    }

    /* Responsive adjustments */
    @media (max-width: 600px) {
      body {
        padding: 5px;
      }
      h1 {
        font-size: 1.2em;
      }
      .container {
        padding: 15px;
      }
      input[type="range"] {
        width: 100%;
      }
    }
  </style>
</head>
<body>
  <h1>Vaishu's Smart Alarm Clock</h1>
  <div class="container">
    <label for="wakeUpTime" class="label-bold">Wake-Up Time:</label><br>
    <input type="time" id="wakeUpTime" value=")" + wakeUpTime + R"(" onchange="updateValue('wakeUpTime')"><br>

    <input type="checkbox" id="wakeUpAlarmEnabled" onchange="updateValue('wakeUpAlarmEnabled')")" + String(wakeUpAlarmEnabled ? " checked" : "") + R"("> Enable Wake-Up Alarm<br>
    <div class="spacer"></div> <!-- Adds space below Enable Wake-Up Alarm -->

    <label for="bedTime" class="label-bold">Bed Time:</label><br>
    <input type="time" id="bedTime" value=")" + bedTime + R"(" onchange="updateValue('bedTime')"><br>

    <input type="checkbox" id="bedTimeAlarmEnabled" onchange="updateValue('bedTimeAlarmEnabled')")" + String(bedTimeAlarmEnabled ? " checked" : "") + R"("> Enable Bedtime Alarm<br>

    <h3>Days for Alarm:</h3>
    <div class="alarm-days">
      <label for="alarmDay0">Sunday</label><input type="checkbox" id="alarmDay0" onchange="updateValue('alarmDay0')" )" + String(alarmDays[0] ? " checked" : "") + R"("><br>
      <label for="alarmDay1">Monday</label><input type="checkbox" id="alarmDay1" onchange="updateValue('alarmDay1')" )" + String(alarmDays[1] ? " checked" : "") + R"("><br>
      <label for="alarmDay2">Tuesday</label><input type="checkbox" id="alarmDay2" onchange="updateValue('alarmDay2')" )" + String(alarmDays[2] ? " checked" : "") + R"("><br>
      <label for="alarmDay3">Wednesday</label><input type="checkbox" id="alarmDay3" onchange="updateValue('alarmDay3')" )" + String(alarmDays[3] ? " checked" : "") + R"("><br>
      <label for="alarmDay4">Thursday</label><input type="checkbox" id="alarmDay4" onchange="updateValue('alarmDay4')" )" + String(alarmDays[4] ? " checked" : "") + R"("><br>
      <label for="alarmDay5">Friday</label><input type="checkbox" id="alarmDay5" onchange="updateValue('alarmDay5')" )" + String(alarmDays[5] ? " checked" : "") + R"("><br>
      <label for="alarmDay6">Saturday</label><input type="checkbox" id="alarmDay6" onchange="updateValue('alarmDay6')" )" + String(alarmDays[6] ? " checked" : "") + R"("><br>
    </div>

    <div class="spacer"></div> <!-- Adds space between Saturday and Lamp Brightness -->

    <label for="brightness" class="label-bold">Lamp Brightness:</label><br>
    <input type="range" id="brightness" min="0" max="255" value=")" + String(currentBrightness) + R"(" onchange="updateValue('brightness')"><br>

    <label for="ledColor" class="label-bold">LED Color:</label><br>
    <input type="color" id="ledColor" value="#ffffff" onchange="updateValue('ledColor')"><br>

    <button onclick="sendCommand('stopAlarm')">Clear Light</button><br>
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

    // Update brightnessValue display without showing default value in UI
    if (type === 'brightness') {
      document.getElementById('brightnessValue').innerText = value;
    }
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
    startSunrise();
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
      WiFiUDP ntpUDP;
      NTPClient timeClient(ntpUDP, "time1.google.com", 19800, 60000);  // GMT+5:30
      timeClient.begin();
      timeClient.update();
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
  if (Alarm_btn_flag == 0) {
    for (int step = 0; step <= steps; step++) {
      if (Alarm_btn_flag == 1) {
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
      //Serial.println(step);
    }
    Serial.print("Loop ended in gradient");
  }
}

// Sunrise simulation function
void startSunrise() {
  Serial.println("Starting Sunrise...");
  digitalWrite(Audio_SW, HIGH);
  delay(1000);
  Audio_Number = random(1, 4);
  dfPlayer.play(Audio_Number);


  int white[3] = { 255, 255, 255 };  // White (Daylight)
  int yellow[3] = { 255, 200, 0 };   // Yellow (Evening)
  int orange[3] = { 255, 60, 0 };    // Orange (Dusk)
  int dark[3] = { 0, 0, 0 };

  int steps = 100;                                              // Number of steps for transition
  int delayTime = (sunriseDuration * 60 * 1000) / (steps * 9);  // Smooth transition time based on duration

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
  digitalWrite(Audio_SW, LOW);
}

// Sunset simulation function
void startSunset() {
  Serial.println("Starting Sunset...");
  digitalWrite(Audio_SW, HIGH);
  delay(1000);
  Audio_Number = random(4, 8);
  dfPlayer.play(Audio_Number);


  // Define color stages for sunset (reverse of sunrise)
  int white[3] = { 255, 255, 255 };  // White (Daylight)
  int yellow[3] = { 255, 200, 0 };   // Yellow (Evening)
  int orange[3] = { 255, 60, 0 };    // Orange (Dusk)
  int dark[3] = { 0, 0, 0 };         // Dark (Night)

  int steps = 100;                                             // Number of steps for transition
  int delayTime = (sunsetDuration * 60 * 1000) / (steps * 3);  // Smooth transition time based on duration

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
  digitalWrite(Audio_SW, LOW);
  Alarm_btn_flag = 0;
}


void setBrightness_inc(int brightness) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(brightness, brightness, brightness));  // Set all pixels to white
  }
  strip.show();
  Serial.println("Brightness set to: " + String(brightness));
}
