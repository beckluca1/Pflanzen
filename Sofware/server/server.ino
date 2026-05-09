#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "mbedtls/md.h"

// -------- CONFIG --------
#define CONFIG_PIN       D1  // D1 GPIO_2
#define MOTOR_ACTIVE_PIN D8  // D8
#define MOTOR_1_PIN      D9  // D9
#define MOTOR_2_PIN      D10 // D10

// WiFi AP (config mode)
const char* ssid = "XXX";
const char* password = "XXX";
const char* url = "pflanze";

// NTP
const char* ntpServer = "pool.ntp.org";

// -----------------------

WebServer server(80);
Preferences prefs;

int wakeHour = 12;
int wakeMinute = 0;
int wakeDuration = 30;

enum ConfigMode {
  noBootMode = 0,
  firstBootMode = 1,
  buttonBootMode = 2,
  scheduledBootMode = 3,
};

ConfigMode configMode = noBootMode;

int startTime = 0;

const char* indexURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/index.html";
const char* styleURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/style.css";
const char* scriptURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/script.js";

const char* indexKeyURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/indexKey.txt";
const char* styleKeyURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/styleKey.txt";
const char* scriptKeyURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Sofware/server/data/scriptKey.txt";

const char* privateKey = "XXX";

// -------- TIME --------
bool initTime() {
  configTime(0, 0, ntpServer);

  setenv("TZ", "CET-1CEST-2,M3.5.0/2,M10.5.0/3", 1);
  tzset();

  struct tm timeinfo;

  unsigned long timeoutStart = millis();
  const unsigned long timeout = 20000;
  Serial.print("Trying init time");

  while (true) {
    if(getLocalTime(&timeinfo)) {
      Serial.println("");
      break;
    }

    if(millis() - timeoutStart > timeout) {
      Serial.println("");
      Serial.println("Failed to init time");
      return false;
    }
    
    Serial.print(".");
    delay(500);
  }

  time_t now = time(nullptr);

  Serial.println("---------Inited time---------");
  Serial.print("Time: "); Serial.print(ctime(&now));
  Serial.println("-----------------------------");

  return true;
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

  server.stop();
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  delay(100);

  esp_deep_sleep_start();
}

bool mountSpiffs() {
    Serial.println("Trying to mount SPIFFS");
    if (!SPIFFS.begin(true)) {
      Serial.println("Failed to mount SPIFFS");
      return false;
    }

    Serial.println("-------Mounted SPIFFS--------");
    Serial.print("Total Storage: "); Serial.println(SPIFFS.totalBytes());
    Serial.print("Used Storage: "); Serial.println(SPIFFS.usedBytes());
    Serial.println("-----------------------------");    
    
    return true;
}

// -------- WEB HANDLERS --------
void sendFile(String fileName, const char * fileType) {
  File file = SPIFFS.open(fileName, "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  size_t fileSize = file.size();

  Serial.printf("Send client file of size %u bytes\n", fileSize);

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

bool hmacFile(
    const char *path,
    const uint8_t *key,
    size_t keyLen,
    uint8_t output[32]
) {
    File file = SPIFFS.open(path, "r");

    if (!file) {
        Serial.println("Failed to open file");
        return false;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (!info) {
        Serial.println("SHA256 not available");
        file.close();
        return false;
    }

    if (mbedtls_md_setup(&ctx, info, 1) != 0) {
        Serial.println("mbedtls_md_setup failed");
        file.close();
        return false;
    }

    if (mbedtls_md_hmac_starts(&ctx, key, keyLen) != 0) {
        Serial.println("hmac_starts failed");
        mbedtls_md_free(&ctx);
        file.close();
        return false;
    }

    const size_t BUFFER_SIZE = 512;
    uint8_t buffer[BUFFER_SIZE];

    while (file.available()) {

        size_t bytesRead =
            file.read(buffer, BUFFER_SIZE);

        if (bytesRead > 0) {

            if (mbedtls_md_hmac_update(
                    &ctx,
                    buffer,
                    bytesRead) != 0) {

                Serial.println("hmac_update failed");

                mbedtls_md_free(&ctx);
                file.close();
                return false;
            }
        }
    }

    if (mbedtls_md_hmac_finish(&ctx, output) != 0) {
        Serial.println("hmac_finish failed");

        mbedtls_md_free(&ctx);
        file.close();
        return false;
    }

    mbedtls_md_free(&ctx);
    file.close();

    return true;
}

String hmacToString(uint8_t hmac[32]) {
    char output[65];

    for (int i = 0; i < 32; i++) {
        sprintf(output + (i * 2), "%02x", hmac[i]);
    }

    output[64] = '\0';

    return String(output);
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

  // Turn off conncetions to draw less power
  server.stop();
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  delay(100);

  water(server.arg("duration").toInt());

  delay(100);

  connectToWifiFast();
  connectMDNS(url);
  startServer();

  startTime = millis();
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
  downloadFileSecure(indexURL, "/index.html", indexKeyURL);
  downloadFileSecure(styleURL, "/style.css", styleKeyURL);
  downloadFileSecure(scriptURL, "/script.js", scriptKeyURL);

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

bool waitForWifi() {
    Serial.print("Trying to connect to Wifi");
    unsigned long timeoutStart = millis();
    const unsigned long timeout = 20000;

    while(true) {
      if(WiFi.status() == WL_CONNECTED) {
        Serial.println("");
        Serial.println("------Connected to Wifi------");
        Serial.print("SSID: "); Serial.println(WiFi.SSID());
        Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
        Serial.print("Local IP: "); Serial.println(WiFi.localIP());
        Serial.print("Gateway IP: "); Serial.println(WiFi.gatewayIP());
        Serial.print("Subnet Mask: "); Serial.println(WiFi.subnetMask());
        Serial.print("Mac Address: "); Serial.println(WiFi.macAddress());
        Serial.print("Channel: "); Serial.println(WiFi.channel());
        Serial.print("BSSID: "); Serial.println(WiFi.BSSIDstr());
        Serial.println("-----------------------------");
        break;
      }

      if(millis() - timeoutStart > timeout) {
        Serial.println("");
        Serial.println("Wifi Connection Failed");
        return false;
      }

      Serial.print(".");
      delay(500);
    }

    return true;
}

bool connectToWifiDefault() {
    WiFi.begin(ssid, password);

    if(!waitForWifi()) return false;

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

    if(!waitForWifi()) return connectToWifiDefault();

    return true;
}

bool connectMDNS(const char* hostname) {
    Serial.println("Trying to start MDNS");
    if (!MDNS.begin(hostname)) {
      Serial.println("Failed to start  MDNS");
      return false;
    }
    Serial.println("--------Started MDNS---------");
    Serial.print("Hostname: "); Serial.println(hostname);
    Serial.println("-----------------------------");    
    
    return true;
}

bool downloadFileSecure(String fileURL, const char* fileName, String keyURL) {

  downloadFile(fileURL, fileName);
  downloadFile(keyURL, "/hmac.txt");

  File file = SPIFFS.open("/hmac.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file");
    return false;
  }
  String testHmac = file.readString();

  Serial.println(testHmac);

  uint8_t hmac[32];
  if(!hmacFile(fileName, (const uint8_t *) privateKey, strlen(privateKey), hmac)) return false;

  String hmacString = hmacToString(hmac);

  Serial.println(hmacString);

  return true;
}


bool downloadFile(String fileURL, const char* fileName) {
  HTTPClient http;
  http.begin(fileURL);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();

    File file = SPIFFS.open(fileName, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file");
      return false;
    }

    uint8_t buffer[128];
    size_t totalBytes = 0;

    unsigned long timeoutStart = millis();
    const unsigned long timeout = 500;
    int contentLength = http.getSize();

    Serial.print("Try Downloading file");

    while (true) {
      if (stream->available()) {
        size_t bytesRead = stream->readBytes(buffer, sizeof(buffer));
        file.write(buffer, bytesRead);
        totalBytes += bytesRead;

        if(contentLength > 0 && totalBytes == (size_t)contentLength) {
          Serial.println("");
          break;
        }

        timeoutStart = millis(); // reset timeout
      } else {
        if (!http.connected() || millis() - timeoutStart > timeout) {
          Serial.println("");
          Serial.println("Failed Downloading file");
          file.close();
          return false;
        }

        Serial.print(".");
        delay(10);
      }
    }
    file.close();

    File checkFile = SPIFFS.open(fileName, FILE_READ);

    Serial.println("------Downloaded file--------");
    Serial.print("Name: "); Serial.println(checkFile.name());
    Serial.print("Size: "); Serial.println(checkFile.size());
    Serial.println("-----------------------------");    

    checkFile.close();
  } else {
    Serial.printf("HTTP GET failed, error: %d\n", httpCode);

    return false;
  }

  http.end();

  return true;
}

bool updateWebsite() {
    if(!downloadFileSecure(indexURL, "/index.html", indexKeyURL)) return false;
    if(!downloadFileSecure(styleURL, "/style.css", styleKeyURL)) return false;
    if(!downloadFileSecure(scriptURL, "/script.js", scriptKeyURL)) return false;

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

bool firstBoot() {
  if(!connectToWifiDefault())     return false;
  if(!connectMDNS(url))           return false;
  if(!initTime())                 return false;
  if(!mountSpiffs())              return false;
  if(!updateWebsite())            return false;
  startServer();
  startTime = millis();
  return true;
}

bool buttonBoot() {
  if(!connectToWifiFast())  return false;
  if(!connectMDNS(url))     return false;
  if(!initTime())           return false;
  if(!mountSpiffs())        return false;
  startServer();
  startTime = millis();
  return true;
}

bool scheduledBoot() {
  water(wakeDuration);
  goToSleep();
  return true;
}

bool bootUp(ConfigMode configMode) {
  if (configMode == firstBootMode) {
    return firstBoot();
  } else if(configMode == buttonBootMode) {
    return buttonBoot();
  } else if(configMode == scheduledBootMode) {
    return scheduledBoot();
  }

  return false;
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
    configMode = buttonBootMode;
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Scheduled boot");
    configMode = scheduledBootMode;
  } else {
    Serial.println("First boot");
    configMode = firstBootMode;
  }

    // Wait for wifi to be connected
    Serial.println("--------------------Trying to boot-------------------------------");

    while(true) {
      if(bootUp(configMode)) {
        Serial.println("-----------------------Booted up---------------------------------");
        break;
      }

      Serial.println("------------------Error: Trying again----------------------------");
      delay(500);
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
