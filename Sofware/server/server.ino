#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <HTTPClient.h>
#include <SPIFFS.h>

// -------- CONFIG --------
#define CONFIG_PIN       D1  // D1 GPIO_2
#define MOTOR_ACTIVE_PIN D8  // D8
#define MOTOR_1_PIN      D9  // D9
#define MOTOR_2_PIN      D10 // D10

// WiFi AP (config mode)
const char* ssid = "XXX";
const char* password = "XXX";

// NTP
const char* ntpServer = "pool.ntp.org";

// -----------------------

WebServer server(80);
Preferences prefs;

int wakeHour = 12;
int wakeMinute = 0;
int wakeDuration = 30;

enum ConfigMode {
  noBoot = 0,
  firstBoot = 1,
  buttonBoot = 2,
  scheduledBoot = 3,
};

ConfigMode configMode = noBoot;

int startTime = 0;

const char* indexURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/index.html";
const char* styleURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/style.css";
const char* scriptURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/script.js";

// -------- TIME --------
void initTime() {
  configTime(0, 0, ntpServer);

  setenv("TZ", "CET-1CEST-2,M3.5.0/2,M10.5.0/3", 1);
  tzset();

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }

  time_t now = time(nullptr);
  Serial.println(ctime(&now));
}

// Calculate microseconds until next scheduled wake
uint64_t getSleepDuration() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  int nowSec = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
  int targetSec = wakeHour * 3600 + wakeMinute * 60;

  int diff = targetSec - nowSec;
  if (diff <= 0) diff += 24 * 3600;

  return (uint64_t)diff * 1000000ULL;
}

// -------- SLEEP --------
void goToSleep() {
  uint64_t sleepTime = getSleepDuration();

  Serial.print("Sleeping for (seconds): ");
  Serial.println(sleepTime / 1000000ULL);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 0); // button
  esp_sleep_enable_timer_wakeup(sleepTime);    // timer

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  delay(100);

  esp_deep_sleep_start();
}

// -------- WEB HANDLERS --------
void sendFile(String fileName, const char * fileType) {
  File file = SPIFFS.open(fileName, "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  size_t fileSize = file.size();

  server.setContentLength(fileSize);
  server.send(200, fileType);

  WiFiClient client = server.client();

  uint8_t buffer[512];
  while (file.available()) {
    size_t len = file.read(buffer, sizeof(buffer));
    client.write(buffer, len);
  }

  file.close();
}

void handleRoot() {
  sendFile("/index.html", "text/html");
}

void handleStyle() {
  sendFile("/style.css", "text/css");
}

void handleScript() {
  sendFile("/script.js", "text/js");
}

void handleSet() {
  wakeHour = server.arg("hour").toInt();
  wakeMinute = server.arg("minute").toInt();
  wakeDuration = server.arg("duration").toInt();

  prefs.begin("settings", false);
  prefs.putInt("hour", wakeHour);
  prefs.putInt("minute", wakeMinute);
  prefs.putInt("duration", wakeDuration);
  prefs.end();

  Serial.printf("Saved time: %02d:%02d for %02d s\n", wakeHour, wakeMinute, wakeDuration);

  server.send(200, "application/json", "{\"success\":true}");

  delay(1000);
  goToSleep();
}

void handleWater() {
  server.send(200, "application/json", "{\"success\":true}");

  water(server.arg("duration").toInt());
}

void handleGetHour() {
  server.send(200, "text/plain", String(wakeHour));
}

void handleGetMinute() {
  server.send(200, "text/plain", String(wakeMinute));
}

void handleGetDuration() {
  server.send(200, "text/plain", String(wakeDuration));
}

void handleHealth() {
  server.send(200, "application/json", "{\"success\":true}");
}

void handleUpdate() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");

    server.send(200, "application/json", "{\"success\":false}");

    return;
  }

  downloadFile(indexURL, "/index.html");
  downloadFile(styleURL, "/style.css");
  downloadFile(scriptURL, "/script.js");

  server.send(200, "application/json", "{\"success\":true}");
}

void water(int duration) {
    // Enable Motor Driver
    digitalWrite(MOTOR_ACTIVE_PIN, HIGH);

    // Drive Motor
    digitalWrite(MOTOR_1_PIN, HIGH);
    digitalWrite(MOTOR_2_PIN, LOW);

    delay(duration * 1000);

    // Stop Motor
    digitalWrite(MOTOR_1_PIN, LOW);
    digitalWrite(MOTOR_2_PIN, LOW);

    // Disable Motor Driver
    digitalWrite(MOTOR_ACTIVE_PIN, LOW);
}

bool connectToWifiDefault() {
    WiFi.begin(ssid, password);

    // Wait for wifi to be connected
    Serial.println("Connect to Wifi");
    unsigned long timeoutStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - timeoutStart < 10000) {
      delay(100);
      Serial.print(".");
    }
    Serial.println("");

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Connection Failed");
      return false;
    }

    Serial.println("Connected to Wifi");

    // Record Wifi credentials for faster reconnect
    uint8_t channel = WiFi.channel();
    const uint8_t* bssid = WiFi.BSSID();
  
    // Store Wifi credentials
    prefs.begin("wifi", false);
    prefs.putUChar("channel", channel);
    prefs.putBytes("bssid", bssid, 6);
    prefs.end();

    return true;
}

bool connectToWifiFast() {
    // Load wifi credentials
    Preferences prefs;
    prefs.begin("wifi", true);

    uint8_t channel = prefs.getUChar("channel", 0);
    uint8_t bssid[6];
    prefs.getBytes("bssid", bssid, 6);

    prefs.end();
  
    // Connect to Wifi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password, channel, bssid, true);

    // Wait for wifi to be connected
    Serial.println("Connect to Wifi");
    unsigned long timeoutStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - timeoutStart < 10000) {
      delay(100);
      Serial.print(".");
    }
    Serial.println("");

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Fast Connection Failed");
      return connectToWifiDefault();
    }

    Serial.println("Connected to Wifi");

    return true;
}

bool connectMDNS() {
    if (!MDNS.begin("pflanze")) {
      Serial.println("Error setting up MDNS responder!");
      return false;
    }
    Serial.println("mDNS responder started");
    return true;
}

void downloadFile(String fileURL, String fileName) {
  HTTPClient http;
  http.begin(fileURL);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();

    File file = SPIFFS.open(fileName, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file");
      return;
    }

    uint8_t buffer[128];
    int bytesRead;

    while (http.connected() && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0) {
      file.write(buffer, bytesRead);
    }

    file.close();
    Serial.println("File downloaded and saved!");
  } else {
    Serial.printf("HTTP GET failed, error: %d\n", httpCode);
  }

  http.end();
}

bool updateWebsite() {
    // Mount Spiffs
    if (!SPIFFS.begin(true)) {
      Serial.println("SPIFFS Mount Failed");
      return false;
    }

    downloadFile(indexURL, "/index.html");
    downloadFile(styleURL, "/style.css");
    downloadFile(scriptURL, "/script.js");

    return true;
}

bool startServer() {
    // Init web routes
    server.on("/", handleRoot);
    server.on("/style.css", handleStyle);
    server.on("/script.js", handleScript);
    server.on("/set", handleSet);
    server.on("/getHour", handleGetHour);
    server.on("/getMinute", handleGetMinute);
    server.on("/getDuration", handleGetDuration);
    server.on("/water", handleWater);
    server.on("/health", handleHealth);
    server.on("/update", handleUpdate);

    // Start Server
    server.begin();

    return true;
}

// -------- SETUP --------
void setup() {
  Serial.begin(115200);

  pinMode(MOTOR_ACTIVE_PIN, OUTPUT);
  pinMode(MOTOR_1_PIN, OUTPUT);
  pinMode(MOTOR_2_PIN, OUTPUT);

  // Load saved schedule
  prefs.begin("settings", true);
  wakeHour = prefs.getInt("hour", 12);
  wakeMinute = prefs.getInt("minute", 0);
  wakeDuration = prefs.getInt("duration", 30);
  prefs.end();

  // Detect wake reason
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Button boot");
    configMode = buttonBoot;
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Scheduled boot");
    configMode = scheduledBoot;
  } else {
    Serial.println("First boot");
    configMode = firstBoot;
  }

  if (configMode == firstBoot) {
    connectToWifiDefault();
    connectMDNS();

    // Sync time
    initTime();

    updateWebsite();
    startServer();

    startTime = millis();
  } else if(configMode == buttonBoot) {
    connectToWifiFast();
    connectMDNS();

    // Sync time
    initTime();

    startServer();

    startTime = millis();
  } else if(configMode == scheduledBoot) {
    Serial.println("Timer wake task running...");

    water(wakeDuration);

    goToSleep();
  }
}

// -------- LOOP --------
void loop() {
  if (configMode) {
    server.handleClient();

    if (millis() - startTime >= 5 * 60 * 1000) {
      Serial.println("10 minutes passed -> shutting down");

      // Timeout reached, shutting down
      goToSleep();
    }

  }
}
