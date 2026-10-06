#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

constexpr char CHANNEL_ID[] = "UC0F9K9qnsopawSgePiOls3g";
constexpr uint8_t BACKLIGHT_PIN = 21;
constexpr uint8_t BACKLIGHT_CHANNEL = 7;
constexpr uint16_t BACKLIGHT_FREQ = 1000;
constexpr uint8_t BACKLIGHT_RESOLUTION = 8;
constexpr unsigned long YOUTUBE_POLL_MS = 15000;

uint8_t brightnessPercent = 80;
String youtubeApiKey;
String subscriberCount = "--";
String lastStatus = "Waiting for API key";
unsigned long lastYouTubePoll = 0;

void setBrightness(uint8_t percent) {
  brightnessPercent = constrain(percent, 0, 100);
  uint8_t duty = map(brightnessPercent, 0, 100, 0, 255);
  ledcWrite(BACKLIGHT_CHANNEL, duty);
  prefs.putUChar("brightness", brightnessPercent);
}

void drawCentered(const String &text, int y, int font, uint16_t color = TFT_WHITE) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(text, tft.width() / 2, y, font);
}

void drawSetupScreen() {
  tft.fillScreen(TFT_BLACK);
  drawCentered("LIFT LOGIC", 42, 4);
  drawCentered("SUBSCRIBER DISPLAY", 76, 2, TFT_CYAN);
  drawCentered("Wi-Fi setup needed", 126, 2);
  drawCentered("Connect to:", 157, 2, TFT_LIGHTGREY);
  drawCentered("LiftLogic-Setup", 183, 4, TFT_YELLOW);
  drawCentered("Then follow the setup page", 218, 2, TFT_LIGHTGREY);
}

void drawApiKeyScreen() {
  tft.fillScreen(TFT_BLACK);
  drawCentered("LIFT LOGIC", 40, 4);
  drawCentered("API KEY NEEDED", 92, 4, TFT_YELLOW);
  drawCentered(WiFi.localIP().toString(), 140, 4, TFT_WHITE);
  drawCentered("Open this address", 181, 2, TFT_LIGHTGREY);
  drawCentered("to finish setup", 205, 2, TFT_LIGHTGREY);
}

void drawCounterScreen() {
  tft.fillScreen(TFT_BLACK);
  drawCentered("LIFT LOGIC", 28, 4, TFT_CYAN);
  tft.drawFastHLine(45, 52, 230, TFT_DARKGREY);
  drawCentered(subscriberCount, 116, 7, TFT_WHITE);
  drawCentered("SUBSCRIBERS", 169, 4, TFT_LIGHTGREY);
  drawCentered(lastStatus, 218, 2, TFT_DARKGREY);
}

bool fetchSubscriberCount() {
  if (youtubeApiKey.isEmpty() || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url = "https://www.googleapis.com/youtube/v3/channels?part=statistics&id=";
  url += CHANNEL_ID;
  url += "&fields=items/statistics(subscriberCount,hiddenSubscriberCount)&key=";
  url += youtubeApiKey;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    lastStatus = "Connection failed";
    drawCounterScreen();
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    lastStatus = "YouTube error " + String(code);
    http.end();
    drawCounterScreen();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    lastStatus = "Bad API response";
    drawCounterScreen();
    return false;
  }

  if (doc["items"].size() == 0) {
    lastStatus = "Channel not found";
    drawCounterScreen();
    return false;
  }

  bool hidden = doc["items"][0]["statistics"]["hiddenSubscriberCount"] | false;
  if (hidden) {
    subscriberCount = "HIDDEN";
    lastStatus = "Subscriber count hidden";
    drawCounterScreen();
    return true;
  }

  const char *count = doc["items"][0]["statistics"]["subscriberCount"];
  if (!count) {
    lastStatus = "Count unavailable";
    drawCounterScreen();
    return false;
  }

  subscriberCount = String(count);
  lastStatus = "Live - refreshes every 15 sec";
  drawCounterScreen();
  return true;
}

String settingsPage() {
  String html = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LiftLogic Display</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:40px auto;padding:0 18px;background:#0b0b0b;color:#eee}
.card{background:#171717;border:1px solid #333;border-radius:16px;padding:20px;margin-bottom:16px}
h1,h2{margin-top:0} input[type=range]{width:100%} input[type=password]{box-sizing:border-box;width:100%;padding:12px;border-radius:10px;border:1px solid #444;background:#0e0e0e;color:#fff}
button{padding:12px 16px;border:0;border-radius:10px;font-size:16px;margin-top:10px}
small{color:#aaa}.ok{color:#6ee7b7}.muted{color:#aaa}
</style>
</head>
<body>
<div class="card">
<h1>LiftLogic Display</h1>
<p>Firmware v0.2</p>
<p><b>Wi-Fi:</b> )rawliteral";
  html += WiFi.SSID();
  html += R"rawliteral(</p>
<p><b>IP:</b> )rawliteral";
  html += WiFi.localIP().toString();
  html += R"rawliteral(</p>
<p><b>Channel ID:</b> <span class="muted">)rawliteral";
  html += CHANNEL_ID;
  html += R"rawliteral(</span></p>
<p><b>Subscriber count:</b> )rawliteral";
  html += subscriberCount;
  html += R"rawliteral(</p>
</div>

<div class="card">
<h2>YouTube API</h2>
<p>)rawliteral";
  html += youtubeApiKey.isEmpty()
    ? "<span class=\"muted\">No API key saved yet.</span>"
    : "<span class=\"ok\">API key is saved on this display.</span>";
  html += R"rawliteral(</p>
<form method="POST" action="/apikey">
<label for="apikey"><b>YouTube Data API key</b></label>
<input id="apikey" name="apikey" type="password" autocomplete="off" placeholder="Paste API key">
<button type="submit">Save API key</button>
</form>
<small>The API key is stored on the ESP32, not in the public GitHub source.</small>
</div>

<div class="card">
<h2>Brightness</h2>
<label for="brightness"><b>Brightness:</b> <span id="value">)rawliteral";
  html += String(brightnessPercent);
  html += R"rawliteral(%</span></label>
<input id="brightness" type="range" min="0" max="100" value=")rawliteral";
  html += String(brightnessPercent);
  html += R"rawliteral(">
<p><button id="saveBrightness">Save brightness</button></p>
</div>

<script>
const slider=document.getElementById('brightness');
const value=document.getElementById('value');
slider.addEventListener('input',()=>value.textContent=slider.value+'%');
document.getElementById('saveBrightness').addEventListener('click',async()=>{
  await fetch('/brightness?value='+slider.value);
  document.getElementById('saveBrightness').textContent='Saved';
  setTimeout(()=>document.getElementById('saveBrightness').textContent='Save brightness',1000);
});
</script>
</body>
</html>
)rawliteral";
  return html;
}

void startWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", settingsPage());
  });

  server.on("/brightness", HTTP_GET, []() {
    if (!server.hasArg("value")) {
      server.send(400, "text/plain", "Missing value");
      return;
    }
    int value = server.arg("value").toInt();
    setBrightness(constrain(value, 0, 100));
    server.send(200, "text/plain", "OK");
  });

  server.on("/apikey", HTTP_POST, []() {
    if (!server.hasArg("apikey") || server.arg("apikey").length() < 10) {
      server.send(400, "text/plain", "Missing or invalid API key");
      return;
    }

    youtubeApiKey = server.arg("apikey");
    youtubeApiKey.trim();
    prefs.putString("yt_api_key", youtubeApiKey);

    subscriberCount = "--";
    lastStatus = "Checking YouTube...";
    drawCounterScreen();
    fetchSubscriberCount();

    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
  });

  server.on("/refresh", HTTP_GET, []() {
    bool ok = fetchSubscriberCount();
    server.send(ok ? 200 : 500, "text/plain", ok ? subscriberCount : lastStatus);
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);

  prefs.begin("liftlogic", false);
  brightnessPercent = prefs.getUChar("brightness", 80);
  youtubeApiKey = prefs.getString("yt_api_key", "");

  tft.init();
  tft.setRotation(1);

  ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ, BACKLIGHT_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  setBrightness(brightnessPercent);

  drawSetupScreen();

  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager;
  wifiManager.setHostname("liftlogic-display");

  bool connected = wifiManager.autoConnect("LiftLogic-Setup");
  if (!connected) {
    tft.fillScreen(TFT_BLACK);
    drawCentered("Wi-Fi setup failed", 100, 4, TFT_RED);
    drawCentered("Restarting...", 150, 2);
    delay(2500);
    ESP.restart();
  }

  startWebServer();

  if (youtubeApiKey.isEmpty()) {
    drawApiKeyScreen();
  } else {
    lastStatus = "Checking YouTube...";
    drawCounterScreen();
    fetchSubscriberCount();
  }

  Serial.print("LiftLogic display online at http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 10000) {
      lastReconnect = millis();
      WiFi.reconnect();
    }
  } else if (!youtubeApiKey.isEmpty() && millis() - lastYouTubePoll >= YOUTUBE_POLL_MS) {
    lastYouTubePoll = millis();
    fetchSubscriberCount();
  }

  delay(2);
}
