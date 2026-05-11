#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "mbedtls/md.h"
#include "tweetnacl.h"

// -------- CONFIG --------
#define CONFIG_PIN       D1  // D1 GPIO_2
#define MOTOR_ACTIVE_PIN D8  // D8
#define MOTOR_1_PIN      D9  // D9
#define MOTOR_2_PIN      D10 // D10

// AP Wifi
const char* apSsid = "ESP32_AP";
const char* apPassword = "12345678";

const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <style>
        * {touch-action: none;}
head {height: 0dvh; margin: 0; padding: 0;}
body {height: 100dvh; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; overflow: hidden;}

#menu {width: 100vw; align-items: center; justify-content: center; overflow: hidden;}

.backgroundElement {background: rgb(23, 28, 51);}
.fixedForegroundElement {background: rgb(31, 42, 82);}
.editForegroundElement {background: rgb(51, 62, 122);}
            
#settings {height: 17dvh; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; overflow: hidden;}

#description {height: 10dvh; width: 30vw; margin: 0; padding: 0; border-top-left-radius: 5dvh; border-bottom-left-radius: 5dvh; text-align: center; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; user-select: none;}
#textInput {height: 10dvh; width: 60vw; margin: 0; padding: 0; border-top-right-radius: 5dvh; border-bottom-right-radius: 5dvh; text-align: center; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; user-select: none;}
#send {height: 10dvh; width: 80vw; margin: 0; padding: 0; border-radius: 5dvh; text-align: center; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; user-select: none;}

.upperText{min-width: 52vw; min-height: 3vh; padding-top: 2vh; padding-bottom: 2vh; white-space: nowrap; font-size: 3vh; font-family: Verdana, Geneva, sans-serif; color: white;}

#update {padding: 1vw; font-size: 3vw; font-family: Verdana, Geneva, sans-serif; color: white; z-index: 2; opacity: 1.0; display: flex;  position: absolute; border-radius: 4dvh; user-select: none;}
</style>
  </head>
  <div id="menu">
    <body class="backgroundElement">
      <div id="settings">
        <div id="description" class="fixedForegroundElement">
          <div class="upperText">Wifi</div>
        </div>
        <div id="textInput" class="editForegroundElement">
          <div id="wifi" class="upperText" contenteditable="true"></div>
        </div>
      </div>
      <div id="settings">
        <div id="description" class="fixedForegroundElement">
          <div class="upperText">Password</div>
        </div>
        <div id="textInput" class="editForegroundElement">
          <div id="password" class="upperText" contenteditable="true">12345678</div>
        </div>
      </div>
      <div id="settings">
        <div id="description" class="fixedForegroundElement">
          <div class="upperText">URL</div>
        </div>
        <div id="textInput" class="editForegroundElement">
          <div id="url" class="upperText" contenteditable="true">meine-pflanze</div>
        </div>
      </div>
      <div id="settings">
        <div id="send" class="editForegroundElement">
          <div class="upperText">Confirm</div>
        </div>
      </div>     
    </div>
    <script>
    async function main() {
      const wifiDiv = document.getElementById("wifi");
      const passwordDiv = document.getElementById("password");
      const urlDiv = document.getElementById("url");

      const sendDiv = document.getElementById("send");
      sendDiv.addEventListener("click", async () => {
        try {
          const response = await fetch("/setCredentials?ssid=" + wifiDiv.textContent + "&password=" + passwordDiv.textContent + "&url=" + urlDiv.textContent);

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          const data = await response.json();
          console.log(data);
        } catch (err) {
          console.error("Fetch failed:", err);
        }
      });
    }

    main();
    </script>
  </body>
</html>
)rawliteral";

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
  broadcastBootMode = 1,
  firstBootMode = 2,
  buttonBootMode = 3,
  scheduledBootMode = 4,
};

ConfigMode configMode = noBootMode;

int startTime = 0;

String publicKey  = "36754df48c6fc69b623b07d087c5dcd7671cadd134b08fb472682d3791bdd8e8";

const char* indexURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Software/server/data/index.html";
const char* styleURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Software/server/data/style.css";
const char* scriptURL = "https://raw.githubusercontent.com/beckluca1/Pflanzen/refs/heads/main/Software/server/data/script.js";

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

bool hashFile(const char *path, uint8_t hash[32]) {
    File file = SPIFFS.open(path, "r");

    if (!file) {
        Serial.println("Failed to open file");
        return false;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);

    uint8_t buf[512];

    while (file.available()) {
      int len = file.read(buf, sizeof(buf));
      mbedtls_sha256_update(&ctx, buf, len);
    }

    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    return true;
}

bool verifyFile(String signature, uint8_t hash[32], String publicKey) {

    uint8_t signatureBuffer[signature.length()];
    signature.getBytes(signatureBuffer, signature.length() + 1);

    uint8_t publicKeyBuffer[publicKey.length()];
    publicKey.getBytes(publicKeyBuffer, publicKey.length() + 1);

    if(!crypto_sign_verify_detached(signatureBuffer, hash, 32, publicKeyBuffer)) return false;

    return true;
}

void handleRootBroadcast() {
  server.send(200, "text/html", webpage);
}

void handleSetCredentials() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String url = server.arg("url");

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.putString("url", url);
  prefs.end();

  Serial.printf("Saved credentials: %s %s %s\n", ssid, password, url);

  server.send(200, "application/json", "{\"success\":true}");

  delay(1000);
  ESP.restart();
}

void handleGetSSID() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  prefs.end();

  server.send(200, "text/plain", ssid);
}

void handleGetPassword() {
  prefs.begin("wifi", true);
  String password = prefs.getString("password", "");
  prefs.end();

  server.send(200, "text/plain", password);
}

void handleGetURL() {
  prefs.begin("wifi", true);
  String url = prefs.getString("url", "");
  prefs.end();

  server.send(200, "text/plain", url);
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
  connectMDNS();
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
  downloadFileSecure(indexURL, "/index.html");
  downloadFileSecure(styleURL, "/style.css");
  downloadFileSecure(scriptURL, "/script.js");

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

bool connectToWifiBroadcast() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid, apPassword);
    Serial.println("AP Started");
    Serial.println(WiFi.softAPIP());
    //if(!waitForWifi()) return false;

    // Record Wifi credentials for faster reconnect
    //uint8_t channel = WiFi.channel();
    //const uint8_t* bssid = WiFi.BSSID();
  
    // Store Wifi credentials
    //prefs.begin("wifi", false);
    //prefs.putUChar("channel", channel);
    //prefs.putBytes("bssid", bssid, 6);
    //prefs.end();

    return true;
}

bool connectToWifiDefault() {

    // Load wifi credentials
    Preferences prefs;
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
    prefs.end();

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
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
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

bool connectMDNS() {
    Preferences prefs;
    prefs.begin("wifi", true);
    String url = prefs.getString("url", "meine-pflanze");
    prefs.end();

    Serial.println("Trying to start MDNS");
    if (!MDNS.begin(url)) {
      Serial.println("Failed to start  MDNS");
      return false;
    }
    Serial.println("--------Started MDNS---------");
    Serial.print("Hostname: "); Serial.println(url);
    Serial.println("-----------------------------");    
    
    return true;
}

bool downloadFileSecure(String fileURL, const char* fileName) {

  downloadFile(fileURL, "/tempFile.txt");
  downloadFile(fileURL + ".sig", "/signature.txt");

  File file = SPIFFS.open("/signature.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file");
    return false;
  }
  String testSignature = file.readString();
  file.close();

  Serial.println("Signature:");
  Serial.println(testSignature);

  uint8_t hash[32];
  if(!hashFile("/tempFile.txt", hash)) return false;

  for (int i = 0; i < 32; i++)
  {
      Serial.printf("%02x", hash[i]);
  }
  Serial.println();

  if(verifyFile(testSignature, hash, publicKey)) {
      Serial.println("Delete old file and save new downloaded one.");

      SPIFFS.remove(fileName);
      SPIFFS.rename("/tempFile.txt", fileName);
  }
  else {
    Serial.println("Signature does not match. Keep old file");
  }

  SPIFFS.remove("/tempFile.txt");
  SPIFFS.remove("/signature.txt");

  return true;
}


bool downloadFile(String fileURL, const char* fileName) {
  HTTPClient http;

  http.begin(fileURL + "?nocache=" + String(millis()));

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
    Serial.print("URL: "); Serial.println(fileURL);
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
    if(!downloadFileSecure(indexURL, "/index.html")) return false;
    if(!downloadFileSecure(styleURL, "/style.css")) return false;
    if(!downloadFileSecure(scriptURL, "/script.js")) return false;

    return true;
}

bool startServerBroadcast() {
    // Init web routes
    server.on("/", handleRootBroadcast);
    server.on("/setCredentials", handleSetCredentials);
    server.on("/getSSID", handleGetSSID);
    server.on("/getPassword", handleGetPassword);
    server.on("/getURL", handleGetURL);

    // Start Server
    server.begin();

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
    server.on("/setCredentials", handleSetCredentials);
    server.on("/getSSID", handleGetSSID);
    server.on("/getPassword", handleGetPassword);
    server.on("/getURL", handleGetURL);

    // Start Server
    server.begin();

    return true;
}

bool broadcastBoot() {
  if(!connectToWifiBroadcast())   return false;
  if(!connectMDNS())              return false;
  //if(!initTime())                 return false;
  //if(!mountSpiffs())              return false;
  //if(!updateWebsite())            return false;
  startServerBroadcast();
  startTime = millis();
  return true;
}

bool firstBoot() {
  if(!connectToWifiDefault())     {configMode = broadcastBootMode; return false;}
  if(!connectMDNS())              return false;
  if(!initTime())                 return false;
  if(!mountSpiffs())              return false;
  if(!updateWebsite())            return false;
  startServer();
  startTime = millis();
  return true;
}

bool buttonBoot() {
  if(!connectToWifiFast())  return false;
  if(!connectMDNS())        return false;
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
  if (configMode == broadcastBootMode) {
    return broadcastBoot();
  } else if (configMode == firstBootMode) {
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
