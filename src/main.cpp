#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

constexpr uint8_t BACKLIGHT_PIN = 21;
constexpr uint8_t BACKLIGHT_CHANNEL = 0;
constexpr uint16_t BACKLIGHT_FREQ = 5000;
constexpr uint8_t BACKLIGHT_RESOLUTION = 8;

uint8_t brightnessPercent = 80;

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

void drawOnlineScreen() {
  tft.fillScreen(TFT_BLACK);
  drawCentered("LIFT LOGIC", 46, 4);
  drawCentered("DISPLAY ONLINE", 100, 4, TFT_GREEN);
  drawCentered(WiFi.localIP().toString(), 145, 4, TFT_WHITE);
  drawCentered("Open this address for", 187, 2, TFT_LIGHTGREY);
  drawCentered("brightness settings", 210, 2, TFT_LIGHTGREY);
}

String settingsPage() {
  String html = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LiftLogic Display</title>
<style>
body{font-family:system-ui,sans-serif;max-width:520px;margin:40px auto;padding:0 18px;background:#111;color:#eee}
.card{background:#1d1d1d;border:1px solid #333;border-radius:16px;padding:20px}
h1{margin-top:0} input[type=range]{width:100%} button{padding:12px 16px;border:0;border-radius:10px;font-size:16px}
small{color:#aaa}
</style>
</head>
<body>
<div class="card">
<h1>LiftLogic Display</h1>
<p>Hardware test firmware v0.1</p>
<p><b>Wi-Fi:</b> )rawliteral";
  html += WiFi.SSID();
  html += R"rawliteral(</p>
<p><b>IP:</b> )rawliteral";
  html += WiFi.localIP().toString();
  html += R"rawliteral(</p>
<label for="brightness"><b>Brightness:</b> <span id="value">)rawliteral";
  html += String(brightnessPercent);
  html += R"rawliteral(%</span></label>
<input id="brightness" type="range" min="0" max="100" value=")rawliteral";
  html += String(brightnessPercent);
  html += R"rawliteral(">
<p><button id="save">Save brightness</button></p>
<small>The full subscriber counter, branding, touch controls and bedtime schedule come next.</small>
</div>
<script>
const slider=document.getElementById('brightness');
const value=document.getElementById('value');
slider.addEventListener('input',()=>value.textContent=slider.value+'%');
document.getElementById('save').addEventListener('click',async()=>{
  await fetch('/brightness?value='+slider.value);
  document.getElementById('save').textContent='Saved';
  setTimeout(()=>document.getElementById('save').textContent='Save brightness',1000);
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

  server.begin();
}

void setup() {
  Serial.begin(115200);

  prefs.begin("liftlogic", false);
  brightnessPercent = prefs.getUChar("brightness", 80);

  ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ, BACKLIGHT_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  setBrightness(brightnessPercent);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
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

  drawOnlineScreen();
  startWebServer();

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
  }

  delay(2);
}
