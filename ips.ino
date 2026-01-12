/*
  --- SMART MULTI-TAG ANCHOR (ROBUST PARSING VERSION) ---
  - Correctly distinguishes between Tag 0 and Tag 1 IDs.
  - Fixes the issue where data overwrites Tag 0 slot.
*/

#include <WiFi.h>
#include <esp_now.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <Preferences.h>

// --- PINS ---
#define UWB_RX 18
#define UWB_TX 17
#define OLED_SDA 39
#define OLED_SCL 38

HardwareSerial UWBSerial(2);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
AsyncWebServer server(80);
Preferences pref;

// --- VARIABLES ---
int limit_warn = 80;
int limit_danger = 40;
int dist0_CM = 0; // Store Tag 0 Distance
int dist1_CM = 0; // Store Tag 1 Distance

// --- ESP-NOW PACKET ---
typedef struct struct_packet {
  float dist0;    // Slot for Tag 0
  float dist1;    // Slot for Tag 1
  int warn_set;
  int dang_set;
} struct_packet;

struct_packet myData;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// --- HTML PAGE ---
const char* html_page = R"rawliteral(
<!DOCTYPE html><html><head><title>UWB Multi-Tag</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>body{font-family:Arial;text-align:center;background:#222;color:#fff}.box{background:#333;padding:20px;margin:10px;border-radius:10px}
.val{font-size:3em;font-weight:bold;color:#00ffcc}.lbl{color:#aaa}</style></head><body>
<h2>ANCHOR DASHBOARD</h2>
<div class="box"><span class="lbl">TAG 0</span><div class="val"><span id="d0">--</span> cm</div></div>
<div class="box"><span class="lbl">TAG 1</span><div class="val"><span id="d1">--</span> cm</div></div>
<div class="box"><h3>Limits</h3><form action="/set" method="POST">
Warn: <input type="number" name="w" value="%W%"> Danger: <input type="number" name="d" value="%D%"> <input type="submit" value="SAVE"></form></div>
<script>setInterval(function(){fetch('/read').then(r=>r.json()).then(j=>{
document.getElementById('d0').innerText=j.t0; document.getElementById('d1').innerText=j.t1;
})},500);</script>
</body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  
  // 1. Display
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(0,0); display.println("ANCHOR: 2 TAGS");
  display.display();

  // 2. Load Settings
  pref.begin("uwb_app", false);
  limit_warn = pref.getInt("w", 80);
  limit_danger = pref.getInt("d", 40);

  // 3. WiFi & ESP-NOW
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("UWB_ANCHOR_NET", "12345678");
  if (esp_now_init() != ESP_OK) ESP.restart();
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0; peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // 4. Web Server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    String html = html_page;
    html.replace("%W%", String(limit_warn));
    html.replace("%D%", String(limit_danger));
    req->send(200, "text/html", html);
  });
  server.on("/set", HTTP_POST, [](AsyncWebServerRequest *req){
    if(req->hasParam("w", true)) {
      limit_warn = req->getParam("w", true)->value().toInt();
      limit_danger = req->getParam("d", true)->value().toInt();
      pref.putInt("w", limit_warn);
      pref.putInt("d", limit_danger);
    }
    req->redirect("/");
  });
  server.on("/read", HTTP_GET, [](AsyncWebServerRequest *req){
    // Return JSON for both tags
    String json = "{\"t0\":" + String(dist0_CM) + ",\"t1\":" + String(dist1_CM) + "}";
    req->send(200, "application/json", json);
  });
  server.begin();

  // 5. UWB CONFIG
  UWBSerial.begin(115200, SERIAL_8N1, UWB_RX, UWB_TX);
  delay(1000);
  // Configure as Anchor (ID 0 usually)
  UWBSerial.println("AT+SETCFG=0,1,0,1"); delay(500); 
  UWBSerial.println("AT+SETPAN=0x1234"); delay(500);
  UWBSerial.println("AT+SETRPT=1"); delay(500); // Auto Report
  UWBSerial.println("AT+SAVE"); delay(500);
  UWBSerial.println("AT+RESTART"); delay(1000);
}

// Helper to scale values (Meters/CM/MM) to CM
int scaleValue(float rawVal) {
  int cm = 0;
  if(rawVal <= 5.0) cm = (int)(rawVal * 100.0); // Meters
  else if (rawVal > 5.0 && rawVal < 500.0) cm = (int)rawVal; // CM
  else cm = (int)(rawVal / 10.0); // MM
  
  if(cm > 3000) cm = 0; // Filter glitches
  return cm;
}

void loop() {
  while(UWBSerial.available()){
    String s = UWBSerial.readStringUntil('\n');
    s.trim();
    
    // Debugging: Print exactly what UWB sends to Serial Monitor
    // Serial.println(s); 

    int idx = s.indexOf("range:");
    
    if(idx != -1) {
       // Look for structure: "range:ID(DISTANCE)"
       int startParen = s.indexOf("(", idx);
       int endParen = s.indexOf(")", startParen);
       
       if(startParen != -1 && endParen != -1) {
          
          // 1. EXTRACT ID
          // The ID is strictly between "range:" and "("
          // "range:" is 6 chars long.
          String idStr = s.substring(idx + 6, startParen);
          idStr.trim(); // Remove any accidental spaces
          int tagID = idStr.toInt();

          // 2. EXTRACT DISTANCE
          String valStr = s.substring(startParen + 1, endParen);
          float rawVal = valStr.toFloat();
          
          if(rawVal > 0) {
            int calculatedCM = scaleValue(rawVal);
            
            // 3. STRICT ASSIGNMENT
            // Do not use a generic 'else'. Explicitly check IDs.
            if(tagID == 0) {
              dist0_CM = calculatedCM;
            } 
            else if(tagID == 1) {
              dist1_CM = calculatedCM;
            }
            // If ID is something else (garbage or error), we do nothing.

            // Broadcast Updates
            myData.dist0 = (float)dist0_CM;
            myData.dist1 = (float)dist1_CM;
            myData.warn_set = limit_warn;
            myData.dang_set = limit_danger;
            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

            updateDisplay();
          }
       }
    }
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(0,0); display.print("ANCHOR (Host)");
  
  // Show Tag 0
  display.setCursor(0,20);
  display.print("T0: "); display.print(dist0_CM); display.print(" cm");

  // Show Tag 1
  display.setCursor(0,40);
  display.print("T1: "); display.print(dist1_CM); display.print(" cm");

  display.display();
}
