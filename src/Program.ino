#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>

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
const long gmtOffset_sec = 0;      // adjust if needed
const int daylightOffset_sec = 0;  // adjust if needed

// -----------------------

WebServer server(80);
Preferences prefs;

int wakeHour = 12;
int wakeMinute = 0;
int wakeDuration = 30;

bool configMode = false;

int startTime = 0;

// -------- HTML PAGE --------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <style>
      * {
        touch-action: none;
      }
      head {height: 0dvh; margin: 0; padding: 0;}
      body {height: 100dvh; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; overflow: hidden;}

      #menu {width: 100vw; height: 100dvh;}
      #down {width: 100vw; height: 100dvh; position: absolute; display: none; z-index: 2;  user-select: none; background: rgb(51, 28, 28); opacity: 0.5;}
      #downText{padding: 3vw; font-size: 10vw; font-family: Verdana, Geneva, sans-serif; color: white;  opacity: 1.0; display: none;  position: absolute; z-index: 2; background: rgb(102, 52, 41);border-radius: 5dvh; user-select: none;}

      .backgroundElement {background: rgb(23, 28, 51);}
      .foregroundElement {background: rgb(41, 52, 102);}

      .downBackgroundElement {background: rgb(23, 28, 51);}
      .downForegroundElement {background: rgb(41, 52, 102);}

      #topSettings {height: 70dvh; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; overflow: hidden;}
      #dial {height: 65dvh; margin: 0; padding: 0; border-radius: 10dvh; display: flex; align-items: center; justify-content: center; overflow: hidden;}
      #selector {width: 100vw; height: 70dvh; margin: 0; padding: 0; display: flex;  position: absolute; z-index: 2; align-items: center; justify-content: center; pointer-events: none;}
      #separator {height: 65dvh; width: 5vw; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; user-select: none;}
      #hours {height: 60dvh; width: 30vw; margin: 0; padding: 0; text-align: center; overflow: hidden;}
      #minutes {height: 60dvh; width: 30vw; margin: 0; padding: 0; text-align: center; overflow: hidden;}
            
      #bottomSettings {height: 30dvh; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; overflow: hidden;}
      #duration {height: 30dvh; width: 20vw; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center;}
      #seconds {height: 25dvh; width: 20vw; margin: 0; padding: 0; border-radius: 5dvh; text-align: center; overflow: hidden; }
      #space {height: 30dvh; width: 5vw; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; }
      #shutdown {height: 25dvh; width: 20vw; margin: 0; padding: 0; border-radius: 5dvh; text-align: center; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; user-select: none;}
      #water {height: 25dvh; width: 20vw; margin: 0; padding: 0; border-radius: 5dvh; text-align: center; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; user-select: none;}

      .separatorText{font-size: 4vh; font-family: Verdana, Geneva, sans-serif; color: white;  opacity: 1.0;}
      .upperText{font-size: 3vh; font-family: Verdana, Geneva, sans-serif; color: white;  opacity: 0.5;}
      .lowerText{font-size: 3vh; font-family: Verdana, Geneva, sans-serif; color: white;  opacity: 0.5;}
      #topBar{width: 70vw; height: 1dvh; position: absolute; margin: 0; margin-bottom: 35dvh; background: white;}
      #bottomBar{width: 70vw; height: 1dvh; position: absolute; margin: 0; margin-top: 35dvh; background: white;}
    </style>
  </head>
  <div id="menu">
    <body class="backgroundElement">
      <div id="topSettings">
        <div id="selector">
          <div id="topBar"></div>
          <div id="bottomBar"></div>
        </div>
        <div id="dial" class="foregroundElement">
          <div id="hours"></div>
          <div id="separator" class="clockText" id="separator">
            <t class="separatorText">:</t>
          </div>
          <div id="minutes"></div>
        </div>
      </div>
      <div id="bottomSettings">
        <div id="duration">
          <div id="seconds" class="foregroundElement"></div>
        </div>
        <div id="space"></div>
        <div id="shutdown" class="foregroundElement">
          <div class="upperText">Speichern</div>
          <div class="lowerText">(Herunterfahren)</div>
        </div>
        <div id="space"></div>
        <div id="water" class="foregroundElement">
          <div class="upperText">Bew&auml;ssern</div>
          <div class="lowerText">(Sofort)</div>
        </div>
      </div>
    </div>
    <div id="down">
    </div>
    <div id="downText">Server schl&auml;ft</div>

    <script>
      function shiftNumbers(parentDiv, count, startIndex, endIndex, time) {
        const firstDiv = parentDiv.querySelector("#number0");
        const lastDiv = parentDiv.querySelector("#number" + (count * 3 + 1));

        const elementHeight = firstDiv.offsetHeight;
        const activeElementHeight = lastDiv.offsetHeight;
        
        const startOffset = 0.5* parentDiv.offsetHeight - 0.5 * activeElementHeight - startIndex * elementHeight;
        const endOffset = 0.5* parentDiv.offsetHeight - 0.5 * activeElementHeight - endIndex * elementHeight;

        for(let i = 0; i <= count * 3; i++) {
          const numberDiv = parentDiv.querySelector("#number" + i);

          numberDiv.animate([
            { transform: 'translate(0px, ' + startOffset + 'px)' },
            { transform: 'translate(0px, ' + endOffset + 'px)' }
          ], {
            duration: time,
            easing: "ease-in-out",
            fill: "forwards"
          });
        }
      }

      function scaleNumbers(parentDiv, count, index, smallFont, bigFont, time) {
        for(let i = 0; i <= count * 3; i++) {
          const numberDiv = parentDiv.querySelector("#number" + i);
          animateNumber(numberDiv, smallFont, smallFont, "0.5", "0.5", 0);
        }

          const endDiv = parentDiv.querySelector("#number" + (count * 3 + 1));
          animateNumber(endDiv, bigFont, bigFont, "1.0", "1.0", 0);

        const activeDiv = parentDiv.querySelector("#number" + index);
        animateNumber(activeDiv, bigFont, bigFont, "1.0", "1.0", 0);
      }

      function scaleText(id, fontSize, opacity) {
        const textDivs = document.getElementsByClassName(id);
        for (let textDiv of textDivs) {
          animateNumber(textDiv, fontSize, fontSize, opacity, opacity, 0);
        }
      }

      function animateNumber(animatedDiv, fonstStart, fontEnd, opacityStart, opacityEnd, time) {
          animatedDiv.animate([
            { fontSize: fonstStart, opacity: opacityStart },
            { fontSize: fontEnd, opacity: opacityEnd }
          ], {
            duration: time,
            easing: "ease-in-out",
            fill: "forwards"
          });
      }

      function transitionNumber(parentDiv, count, startIndex, endIndex, smallFont, bigFont, time) {
        // Get elements
        const startNumberDiv = parentDiv.querySelector("#number" + startIndex);
        const endNumberDiv = parentDiv.querySelector("#number" + endIndex);

        animateNumber(startNumberDiv, bigFont, smallFont, "1.0", "0.5", time);
        animateNumber(endNumberDiv, smallFont, bigFont, "0.5", "1.0", time);
        shiftNumbers(parentDiv, count, startIndex, endIndex, time);
      }

      function scaleNumber(parentDiv, count, oldIndex, startIndex, endIndex, smallFont, bigFont, time) {
        // Set the wapped active number
        if(oldIndex != startIndex) transitionNumber(parentDiv, count, oldIndex, startIndex, smallFont, bigFont, 0);

        // Animate the transition
        if(startIndex != endIndex) transitionNumber(parentDiv, count, startIndex, endIndex, smallFont, bigFont, time);
      }

      async function waitForAnimations(parentDiv, count) {
        for(let i = 0; i <= count * 3; i++) {
          const numberDiv = parentDiv.querySelector("#number" + i);

          const animations = numberDiv.getAnimations();
          const promises = animations.map(anim => anim.finished);
          await Promise.all(promises);
        }
      }

      function wrapIndex(index, count) {
        if(index > count * 2) {
          index -= count;
        }
        else if(index < count) {
          index += count;
        }

        return index;
      }

      async function setActive(parentDiv, count, activeIndex, index, smallFont, bigFont, time) {
        const oldIndex = activeIndex;

        if(index - activeIndex >= 0.5 * count) {
          activeIndex += count;
        }
        else if(index - activeIndex <= -0.5 * count) {
          activeIndex -= count;
        }

        scaleNumber(parentDiv, count, oldIndex, activeIndex, index, smallFont, bigFont, time);

        await waitForAnimations(parentDiv, count);

        return index;
    }

    function setStyle(numberDiv, index, text, fontSize, opacity) {
        numberDiv.textContent = (text.length == 1) ? ("0" + text) : text;
        numberDiv.id = "number" + index;
        numberDiv.style.fontFamily  = 'Verdana, Geneva, sans-serif';
        numberDiv.style.fontSize = fontSize;
        numberDiv.style.opacity = opacity;
        numberDiv.style.color = 'white';
        numberDiv.style.userSelect = 'none';
    }

    function addNumbers(parentDiv, count, increment, smallFont, bigFont) {
      for(let i = 0; i <= count * 3; i++) {
        const numberDiv = document.createElement("div");
        let wrappedIndex = ((i % count) * increment).toString();
        setStyle(numberDiv, i, wrappedIndex, smallFont, "0.5");
        parentDiv.appendChild(numberDiv);
      }

      const endDiv = document.createElement("div");
      setStyle(endDiv, (count * 3 + 1), "end", bigFont, "1.0");
      parentDiv.appendChild(endDiv);
    }

    function getOutput(output, parentDiv, index) {
      const numberDiv = parentDiv.querySelector("#number" + index);
      output.text = numberDiv.textContent;
    }

    async function getInput(id, defaultValue) {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 5000);

      try {
        const res = await fetch(id, { signal: controller.signal });

        if (!res.ok) {
          throw new Error(`Server error: ${res.status}`);
        }

        const value = await res.text();
        return parseInt(value.trim());
      } catch (err) {
        if (err.name === "AbortError") {
          console.error("Request timed out (server may be down)");
        } else {
          console.error("Network/server error:", err.message);
        }
      } finally {
        clearTimeout(timeout);
      }

      return defaultValue;
    }

    async function addScrollNumbers(output, id, count, increment=1, defaultValue=0, smallFontH=6, bigFontH=16, smallFontW=6, bigFontW=16) {
      const parentDiv = document.getElementById(id);
      if(parentDiv == null) return;

      let activeIndex = 0;
      let isDragging = false;
      let startX = 0;
      let startY = 0;
      let targetIndex = 0;
      let scrollEnergy = 0;
      let wait = false;

      let smallFont = window.innerHeight < window.innerWidth ? (smallFontH + 'vh') : (smallFontW + 'vw');
      let bigFont = window.innerHeight < window.innerWidth ? (bigFontH + 'vh') : (bigFontW + 'vw');

      addNumbers(parentDiv, count, increment, smallFont, bigFont);
      targetIndex = count + defaultValue / increment;
      activeIndex = await setActive(parentDiv, count, activeIndex, Math.round(targetIndex), smallFont, bigFont, 100);
      getOutput(output, parentDiv, activeIndex);

      parentDiv.addEventListener('wheel', async function(event) {
        scrollEnergy += 0.005 * event.deltaY;
      });
      parentDiv.addEventListener("pointerdown", (e) => {
        isDragging = true;
        startX = e.clientX;
        startY = e.clientY;
        parentDiv.setPointerCapture(e.pointerId);
      });

      const firstDiv = parentDiv.querySelector("#number0");
      const elementHeight = firstDiv.offsetHeight;

      parentDiv.addEventListener("pointermove", async (e) => {
        if (!isDragging) return;
        scrollEnergy -= 1.0 / (elementHeight) * (e.clientY - startY);
        startY = e.clientY;

      });
      parentDiv.addEventListener("pointerup", (e) => {
        isDragging = false;
        parentDiv.releasePointerCapture(e.pointerId);
      });
      parentDiv.addEventListener("pointercancel", (e) => {
        isDragging = false;
      });

      window.addEventListener("resize", async () => {
        smallFont = parentDiv.clientHeight < parentDiv.clientWidth ? (smallFontH + 'vh') : (smallFontW + 'vw');
        bigFont = parentDiv.clientHeight < parentDiv.clientWidth ? (bigFontH + 'vh') : (bigFontW + 'vw');

        scaleNumbers(parentDiv, count, activeIndex, smallFont, bigFont, 0);
        shiftNumbers(parentDiv, count, activeIndex, activeIndex, 0);
      });

      const checkLoop = setInterval(async () => {
        if(wait) return;

        wait = true;

        while(Math.abs(scrollEnergy) > 0.01) {
          targetIndex = wrapIndex(targetIndex + Math.min(scrollEnergy, 1.0), count); 
          const animatonDuration = 200 / (1 + Math.log(1 + Math.abs(scrollEnergy)));
          activeIndex = await setActive(parentDiv, count, activeIndex, Math.round(targetIndex), smallFont, bigFont, animatonDuration);
          getOutput(output, parentDiv, activeIndex);
          scrollEnergy *= 0.5
        }

        wait = false;
      }, 100);

    }

    async function main() {
      let serverUp = true;

      let hour = {};
      let minute = {};
      let duration = {};

      let hourDefault = await getInput("/getHour", 12);
      let minuteDefault = await getInput("/getMinute", 0);
      let durationDefault = await getInput("/getDuration", 20);

      await addScrollNumbers(hour, "hours" , 24, 1, hourDefault, 12, 30, 6, 15);
      await addScrollNumbers(minute, "minutes", 12, 5, minuteDefault, 12, 30, 6, 15);
      await addScrollNumbers(duration, "seconds", 50, 5, durationDefault, 6, 12, 3, 6);

      window.addEventListener("resize", async () => {
        const separatorDiv = document.getElementById("separator");
        const bigDiv = document.getElementById("shutdown");

        bigFont = separatorDiv.clientHeight < separatorDiv.clientWidth ? (30 + 'vh') : (15 + 'vw');
        upperFont = 1.5 * bigDiv.clientHeight < bigDiv.clientWidth ? (6 + 'vh') : (3 + 'vw');
        lowerFont = 1.5 * bigDiv.clientHeight < bigDiv.clientWidth ? (4 + 'vh') : (2 + 'vw');

        scaleText("separatorText", bigFont, "1.0");
        scaleText("upperText", upperFont, "1.0");
        scaleText("lowerText", lowerFont, "0.5");
      });

      window.dispatchEvent(new Event("resize"));

      const shutdownDiv = document.getElementById("shutdown");

      shutdownDiv.addEventListener("click", async () => {
        try {
          const response = await fetch("/set?hour=" + hour.text + "&minute=" + minute.text + "&duration=" + duration.text);

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          const data = await response.json();
          console.log(data);
        } catch (err) {
          console.error("Fetch failed:", err);
        }
      });

      const waterDiv = document.getElementById("water");

      waterDiv.addEventListener("click", async () => {
        try {
          const response = await fetch("/water?duration=" + duration.text);

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          const data = await response.json();
          console.log(data);
        } catch (err) {
          console.error("Fetch failed:", err);
        }
      });

      const serverUpLoop = setInterval(async () => {
        try {
          const response = await fetch("/health");

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          if(serverUp) return;

          serverUp = true;

          const down = document.getElementById("down");
          down.style.display = "none";

          const downText = document.getElementById("downText");
          downText.style.display = "none";
        } catch (err) {
          console.error("Fetch failed:", err);

          if(!serverUp) return;

          serverUp = false;

          const down = document.getElementById("down");
          down.style.display = "flex";

          const downText = document.getElementById("downText");
          downText.style.display = "block";
        }
      }, 10000);
    }

    main();

    </script>
  </body>
</html>

)rawliteral";

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
void handleRoot() {
  server.send(200, "text/html", PAGE);
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

// -------- SETUP --------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(CONFIG_PIN, INPUT_PULLUP);

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
    Serial.println("Wake: BUTTON -> Config mode");
    configMode = true;
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wake: TIMER -> Run task");
  } else {
    Serial.println("First boot or reset");
    configMode = true; // allow config on first boot
  }

  if (configMode) {
    // Start AP
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED && WiFi.localIP().toString() == "0.0.0.0") {
      delay(2000);
      Serial.print(WiFi.status());
      Serial.print(" / ");
      Serial.println(WiFi.localIP().toString());
    }
  
    if (!MDNS.begin("mamas-pflanze")) {  // your custom name
      Serial.println("Error setting up MDNS responder!");
    }
    Serial.println("mDNS responder started");

    // Sync time
    initTime();

    // Web routes
    server.on("/", handleRoot);
    server.on("/set", handleSet);
    server.on("/getHour", handleGetHour);
    server.on("/getMinute", handleGetMinute);
    server.on("/getDuration", handleGetDuration);
    server.on("/water", handleWater);
    server.on("/health", handleHealth);

    server.begin();

    startTime = millis();
  } else {
    Serial.println("Timer wake task running...");

    water(wakeDuration);

    delay(1000);
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
      delay(1000);
      goToSleep();
    }

  }
}
