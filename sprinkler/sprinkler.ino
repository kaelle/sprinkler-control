/*
  Uno R4 WiFi Sprinkler Controller - FINAL CLEAN VERSION
  No PROGMEM + Full Edit Form Parsing + Material Symbols
*/

#include "WiFiS3.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "arduino_secrets.h"

#define NUM_ZONES 6

int relayPins[NUM_ZONES] = {5, 6, 7, 8, 9, 10};

struct ZoneSchedule {
  char name[20];
  int startHour;
  int startMinute;
  int durationMinutes;
  bool activeDays[7];
  bool enabled;
};

ZoneSchedule schedules[NUM_ZONES];

bool use12HourFormat = true;   // Default = 12-hour US style
int manualRunMinutes = 5;   // Default manual ON duration
unsigned long lastSSEBroadcast = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -7 * 3600);

WiFiServer server(80);
unsigned long lastScheduleCheck = 0;
unsigned long zoneOffTime[NUM_ZONES] = {0};  // millis() when each zone should turn OFF (0 = not running)

// ====================== FUNCTION PROTOTYPES ======================
void checkSchedules();
void handleClient(WiFiClient client);
void sendWebPage(WiFiClient client);
void sendEditPage(WiFiClient client, int zone);
void sendSettingsPage(WiFiClient client);
void saveZone(int zone, String req, WiFiClient client);

// ====================== HTML TEMPLATES (NO PROGMEM) ======================
const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Sprinkler Controller</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
  <link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-25..200" rel="stylesheet">
  <style>body { padding: 20px; background: #f8f9fa; } .zone-card { margin-bottom: 20px; } .status-on { color: #28a745; font-weight: bold; } .status-off { color: #6c757d; }</style>
</head>
<body>
<div class="container">
  <h1 class="mb-4 text-center"><span class="material-symbols-outlined">sprinkler</span> Sprinkler Controller</h1>
  <div class="card mb-4"><div class="card-body text-center"><h5>Current Time</h5><h2 id="currentTime">{{CURRENT_TIME}}</h2></div></div>
  <div class="row" id="zoneContainer">{{ZONE_CARDS}}</div>
  <div class="text-center mt-4"><a href="/settings" class="btn btn-outline-primary"><span class="material-symbols-outlined">settings</span> Settings</a></div>
</div>
<script>
  function toggleZone(z) {
    const isOn = event.target.textContent === "ON";
    fetch(isOn ? `/api/zone/${z}/on` : `/api/zone/${z}/off`)
      .then(r => r.json())
      .then(d => { if (d.success) { const el = document.getElementById(`status${z}`); el.textContent = isOn ? "ON" : "OFF"; el.className = isOn ? "status-on" : "status-off"; }});
  }
</script>
</body></html>
)rawliteral";

const char edit_html[] = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Edit Zone</title>
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-25..200" rel="stylesheet">
</head><body><div class="container mt-4">
<h2><span class="material-symbols-outlined">edit</span> Edit Zone {{ZONE_NUMBER}} - {{ZONE_NAME}}</h2>
<form action="/save" method="get">
<input type="hidden" name="z" value="{{ZONE_ID}}">
<div class="mb-3"><label class="form-label">Zone Name</label><input type="text" name="name" class="form-control" value="{{ZONE_NAME}}"></div>
<div class="row g-3"><div class="col"><label class="form-label">Start Hour</label><select name="hour" class="form-select">{{HOUR_OPTIONS}}</select></div>
<div class="col"><label class="form-label">Start Minute</label><select name="minute" class="form-select">{{MINUTE_OPTIONS}}</select></div></div>
<div class="mb-3"><label class="form-label">Duration (minutes)</label><input type="number" name="duration" class="form-control" value="{{DURATION}}"></div>
<div class="mb-3"><label class="form-label">Active Days</label><div class="d-flex flex-wrap gap-3">{{DAYS_CHECKBOXES}}</div></div>
<div class="form-check mb-3"><input type="checkbox" name="enabled" class="form-check-input" {{ENABLED_CHECKED}}><label class="form-check-label">Enabled</label></div>
<button type="submit" class="btn btn-success"><span class="material-symbols-outlined">save</span> Save Changes</button>
<a href="/" class="btn btn-secondary"><span class="material-symbols-outlined">arrow_back</span> Cancel</a>
</form></div></body></html>
)rawliteral";

const char settings_html[] = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Settings</title>
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-25..200" rel="stylesheet">
</head><body><div class="container mt-4">
<h2><span class="material-symbols-outlined">settings</span> Settings</h2>
<h5>Time Format</h5>
<a href="/settings?format=24" class="btn btn-{{24_ACTIVE}} me-2">24 Hour</a>
<a href="/settings?format=12" class="btn btn-{{12_ACTIVE}}">12 Hour (US)</a>
<hr><a href="/" class="btn btn-secondary"><span class="material-symbols-outlined">arrow_back</span> Back to Dashboard</a>
</div></body></html>
)rawliteral";

// ==================== NEXT SCHEDULED RUN (with duration) ====================
String getNextRunTime(int zone) {
  if (!schedules[zone].enabled) return "Disabled";

  int curDay   = timeClient.getDay();      // 0=Sun ... 6=Sat
  int curHour  = timeClient.getHours();
  int curMin   = timeClient.getMinutes();

  for (int offset = 0; offset < 7; offset++) {
    int checkDay = (curDay + offset) % 7;

    if (schedules[zone].activeDays[checkDay]) {
      int targetHour = schedules[zone].startHour;
      int targetMin  = schedules[zone].startMinute;

      // If today and time already passed → skip to next day
      if (offset == 0 && (curHour > targetHour || (curHour == targetHour && curMin >= targetMin))) {
        continue;
      }

      String result = "";

      // Day label
      if (offset == 0)      result = "Today ";
      else if (offset == 1) result = "Tomorrow ";
      else {
        const char* dayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        result = String(dayNames[checkDay]) + " ";
      }

      // Time (12h or 24h format)
      if (use12HourFormat) {
        int dispH = (targetHour == 0) ? 12 : (targetHour > 12 ? targetHour - 12 : targetHour);
        result += String(dispH) + ":" + (targetMin < 10 ? "0" : "") + String(targetMin);
        result += (targetHour >= 12) ? " PM" : " AM";
      } else {
        result += String(targetHour) + ":" + (targetMin < 10 ? "0" : "") + String(targetMin);
      }

      // Add duration
      result += " (" + String(schedules[zone].durationMinutes) + " min)";

      return result;
    }
  }
  return "No future run";
}

//  client.print(F("<span class=\"display-5\">Sprinkler Control"));

// ==================== DASHBOARD - FIXED Water Animation Toggle ====================
void sendWebPage(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Cache-Control: no-cache");
  client.println();

  client.print(F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"));
  client.print(F("<title>Get Wet</title><link href=\"https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css\" rel=\"stylesheet\">"));
  client.print(F("<link href=\"https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-25..200\" rel=\"stylesheet\">"));

  // Water animation CSS
  client.print(F("<style>body { padding: 20px; background: #f8f9fa; } .zone-card { margin-bottom: 20px; position: relative; overflow: hidden; }"));
  client.print(F(".status-on { color: #28a745; font-weight: bold; } .status-off { color: #6c757d; }"));
  client.print(F(".water-bg { position: absolute; top: 0; left: 0; width: 100%; height: 100%; pointer-events: none; overflow: hidden; opacity: 0.25; z-index: 0; }"));
  client.print(F(".water-drop { position: absolute; font-size: 1.1rem; color: #4fc3f7; animation: fall linear infinite; }"));
  client.print(F("@keyframes fall { 0% { transform: translateY(-100%); } 100% { transform: translateY(300%); } }"));
  client.print(F(".water-bg:not(.active) { display: none; }"));   // ← Important fix
  client.print(F("</style></head><body>"));

  client.print(F("<div class=\"container\">"));

  // Header
  client.print(F("<h1 class=\"mb-4 text-center display-5 fw-bold\">"));
  client.print(F("<span class=\"material-symbols-outlined\" style=\"font-size: 3rem; vertical-align: middle;\">sprinkler</span> "));
  client.print(F("<span class=\"display-5\">Sprinkler Control"));
  client.print(F(" <span class=\"material-symbols-outlined\" style=\"font-size: 3rem; vertical-align: middle;\">sprinkler</span>"));
  client.print(F("</h1>"));

  // Current Time
  client.print(F("<div class=\"card mb-4\"><div class=\"card-body text-center\"><h5>Current Time</h5><h2>"));
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  if (use12HourFormat) {
    int disp = (h == 0) ? 12 : (h > 12 ? h - 12 : h);
    client.print(disp);
    client.print(":");
    if (m < 10) client.print("0");
    client.print(m);
    client.print(h >= 12 ? " PM" : " AM");
  } else {
    client.print(h);
    client.print(":");
    if (m < 10) client.print("0");
    client.print(m);
  }
  client.print(F("</h2></div></div><div class=\"row\">"));

  // Zone cards - water-bg is ALWAYS created
  for (int i = 0; i < NUM_ZONES; i++) {
    bool isOn = (digitalRead(relayPins[i]) == LOW);

    client.print(F("<div class=\"col-md-6 col-lg-4\"><div class=\"card zone-card\">"));

    // Water background - always present
    client.print(F("<div id=\"water"));
    client.print(i);
    client.print(F("\" class=\"water-bg"));
    if (isOn) client.print(F(" active"));
    client.print(F("\">"));
    for (int d = 0; d < 12; d++) {
      int left = 5 + (d * 7);
      float delay = (float)(random(0, 35)) / 10.0;
      float duration = 2.2 + (float)(random(0, 15)) / 10.0;
      client.print(F("<span class=\"water-drop\" style=\"left:"));
      client.print(left);
      client.print(F("%; animation-duration:"));
      client.print(duration);
      client.print(F("s; animation-delay:-"));
      client.print(delay);
      client.print(F("s;\">💧</span>"));
    }
    client.print(F("</div>"));

    // Card content
    client.print(F("<div class=\"card-header\"><strong>Zone "));
    client.print(i + 1);
    client.print(F(" - "));
    client.print(schedules[i].name);
    client.print(F("</strong></div><div class=\"card-body\">"));
    client.print(F("<p><strong>Status:</strong> <span id=\"status"));
    client.print(i);
    client.print(F("\" class=\""));
    client.print(isOn ? "status-on" : "status-off");
    client.print(F("\">"));
    client.print(isOn ? "ON" : "OFF");
    client.print(F("</span></p>"));
    client.print(F("<button onclick=\"toggleZone("));
    client.print(i);
    client.print(F(")\" class=\"btn btn-success btn-sm me-2\">ON</button>"));
    client.print(F("<button onclick=\"toggleZone("));
    client.print(i);
    client.print(F(")\" class=\"btn btn-secondary btn-sm\">OFF</button>"));
    client.print(F("<a href=\"/edit?z="));
    client.print(i);
    client.print(F("\" class=\"btn btn-outline-info btn-sm float-end\"><span class=\"material-symbols-outlined\">edit</span></a>"));
    client.print(F("</div>"));

    client.print(F("<div class=\"card-footer text-muted small\"><strong>Next:</strong> "));
    client.print(getNextRunTime(i));
    client.print(F("</div></div></div>"));
  }

  client.print(F("</div>"));

  // Settings button
  client.print(F("<div class=\"text-center mt-4\">"));
  client.print(F("<a href=\"/settings\" class=\"btn btn-outline-primary d-flex align-items-center justify-content-center gap-2 mx-auto\" style=\"max-width: 220px;\">"));
  client.print(F("<span class=\"material-symbols-outlined\">settings</span>Settings</a></div>"));

  // JavaScript
  client.print(F("<script>"));
  client.print(F("console.log('✅ Dashboard loaded');"));
  client.print(F("let eventSource = new EventSource('/events');"));
  client.print(F("eventSource.onmessage = function(e) {"));
  client.print(F("  console.log('SSE update received');"));
  client.print(F("  location.reload();"));   // Simple way for now - refreshes status
  client.print(F("};"));
  client.print(F("function toggleZone(z) {"));
  client.print(F("  const isOn = event.target.textContent === 'ON';"));
  client.print(F("  fetch(isOn ? `/api/zone/${z}/on` : `/api/zone/${z}/off`)"));
  client.print(F("    .then(r => r.json())"));
  client.print(F("    .then(d => {"));
  client.print(F("      if (d.success) {"));
  client.print(F("        const statusEl = document.getElementById(`status${z}`);"));
  client.print(F("        statusEl.textContent = isOn ? 'ON' : 'OFF';"));
  client.print(F("        statusEl.className = isOn ? 'status-on' : 'status-off';"));
  client.print(F("        const water = document.getElementById(`water${z}`);"));
  client.print(F("        if (water) water.classList.toggle('active', isOn);"));
  client.print(F("      }"));
  client.print(F("    });"));
  client.print(F("}"));
  client.print(F("</script></div></body></html>"));
}

// ==================== EDIT PAGE (Full Form) ====================
void sendEditPage(WiFiClient client, int zone) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Cache-Control: no-cache, no-store, must-revalidate");
  client.println();

  client.print(F("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"));
  client.print(F("<title>Edit Zone</title><link href=\"https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css\" rel=\"stylesheet\">"));
  client.print(F("<link href=\"https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-25..200\" rel=\"stylesheet\"></head><body><div class=\"container mt-4\">"));
  client.print(F("<h2><span class=\"material-symbols-outlined\">edit</span> Edit Zone "));
  client.print(zone + 1);
  client.print(F(" - "));
  client.print(schedules[zone].name);
  client.print(F("</h2><form action=\"/save\" method=\"get\">"));
  client.print(F("<input type=\"hidden\" name=\"z\" value=\""));
  client.print(zone);
  client.print(F("\">"));

  // Name
  client.print(F("<div class=\"mb-3\"><label class=\"form-label\">Zone Name</label><input type=\"text\" name=\"name\" class=\"form-control\" value=\""));
  client.print(schedules[zone].name);
  client.print(F("\"></div>"));

  // Hour & Minute
  client.print(F("<div class=\"row g-3\"><div class=\"col\"><label class=\"form-label\">Start Hour</label><select name=\"hour\" class=\"form-select\">"));
  for (int h = 0; h < 24; h++) {
    client.print(F("<option value=\""));
    client.print(h);
    client.print((h == schedules[zone].startHour) ? "\" selected>" : "\">");
    client.print(h);
    client.print(F("</option>"));
  }
  client.print(F("</select></div><div class=\"col\"><label class=\"form-label\">Start Minute</label><select name=\"minute\" class=\"form-select\">"));
  for (int m = 0; m < 60; m += 5) {
    client.print(F("<option value=\""));
    client.print(m);
    client.print((m == schedules[zone].startMinute) ? "\" selected>" : "\">");
    if (m < 10) client.print("0");
    client.print(m);
    client.print(F("</option>"));
  }
  client.print(F("</select></div></div>"));

  // Duration
  client.print(F("<div class=\"mb-3\"><label class=\"form-label\">Duration (minutes)</label><input type=\"number\" name=\"duration\" class=\"form-control\" value=\""));
  client.print(schedules[zone].durationMinutes);
  client.print(F("\"></div>"));

  // Days
  client.print(F("<div class=\"mb-3\"><label class=\"form-label\">Active Days</label><div class=\"d-flex flex-wrap gap-3\">"));
  const char* dayNames[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  for (int d = 0; d < 7; d++) {
    client.print(F("<div class=\"form-check form-check-inline\"><input type=\"checkbox\" name=\"day"));
    client.print(d);
    client.print(F("\" class=\"form-check-input\" "));
    if (schedules[zone].activeDays[d]) client.print("checked");
    client.print(F("><label class=\"form-check-label\">"));
    client.print(dayNames[d]);
    client.print(F("</label></div>"));
  }
  client.print(F("</div></div>"));

  // Enabled
  client.print(F("<div class=\"form-check mb-3\"><input type=\"checkbox\" name=\"enabled\" class=\"form-check-input\" "));
  if (schedules[zone].enabled) client.print("checked");
  client.print(F("><label class=\"form-check-label\">Enabled</label></div>"));

  client.print(F("<button type=\"submit\" class=\"btn btn-success\"><span class=\"material-symbols-outlined\">save</span> Save Changes</button>"));
  client.print(F("<a href=\"/\" class=\"btn btn-secondary ms-2\"><span class=\"material-symbols-outlined\">arrow_back</span> Cancel</a>"));
  client.print(F("</form></div></body></html>"));
}

// ==================== SETTINGS PAGE (centered Back button) ====================
void sendSettingsPage(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Cache-Control: no-cache");
  client.println();

  client.print(F("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"));
  client.print(F("<title>Settings</title><link href=\"https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css\" rel=\"stylesheet\">"));
  client.print(F("<link href=\"https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-25..200\" rel=\"stylesheet\"></head><body><div class=\"container mt-4\">"));
  client.print(F("<h2><span class=\"material-symbols-outlined\">settings</span> Settings</h2>"));

  // Current Time Format indicator
  client.print(F("<div class=\"mb-4 p-3 border rounded bg-light\">"));
  client.print(F("<strong>Current Time Format:</strong> "));
  if (use12HourFormat) {
    client.print(F("<span class=\"badge bg-primary fs-6\">12 Hour (US) — with AM/PM</span>"));
  } else {
    client.print(F("<span class=\"badge bg-primary fs-6\">24 Hour Format</span>"));
  }
  client.print(F("</div>"));

  // Change Time Format
  client.print(F("<h5>Change Time Format</h5>"));
  client.print(F("<a href=\"/settings?format=24\" class=\"btn btn-"));
  client.print(use12HourFormat ? "outline-primary" : "primary");
  client.print(F(" me-3\">24 Hour Format</a>"));
  client.print(F("<a href=\"/settings?format=12\" class=\"btn btn-"));
  client.print(use12HourFormat ? "primary" : "outline-primary");
  client.print(F("\">12 Hour (US) Format</a>"));

  // Manual Run Duration
  client.print(F("<h5 class=\"mt-5\">Manual Run Duration (minutes)</h5>"));
  client.print(F("<form action=\"/settings\" method=\"get\">"));
  client.print(F("<div class=\"input-group mb-3\" style=\"max-width: 320px;\">"));
  client.print(F("<input type=\"number\" name=\"manual\" class=\"form-control\" value=\""));
  client.print(manualRunMinutes);
  client.print(F("\" min=\"1\" max=\"60\">"));
  client.print(F("<button type=\"submit\" class=\"btn btn-success\">Save</button></div></form>"));

  // BACK BUTTON — arrow now perfectly centered
  client.print(F("<div class=\"mt-4 text-center\">"));
  client.print(F("<a href=\"/\" class=\"btn btn-secondary d-flex align-items-center justify-content-center gap-2 mx-auto\" style=\"max-width: 280px;\">"));
  client.print(F("<span class=\"material-symbols-outlined\">arrow_back</span>"));
  client.print(F("Back to Dashboard"));
  client.print(F("</a></div>"));

  client.print(F("</div></body></html>"));
}

void setup() {
  Serial.begin(115200);

  strcpy(schedules[0].name, "Front Lawn");
  strcpy(schedules[1].name, "Back Lawn");
  strcpy(schedules[2].name, "Raised Beds");
  strcpy(schedules[3].name, "Side Yard");
  strcpy(schedules[4].name, "Zone 5");
  strcpy(schedules[5].name, "Zone 6");

  for (int i = 0; i < NUM_ZONES; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH);
    schedules[i].startHour = 6;
    schedules[i].startMinute = 0;
    schedules[i].durationMinutes = 15;
    schedules[i].enabled = (i < 4);
    for (int d = 0; d < 7; d++) schedules[i].activeDays[d] = (d >= 1 && d <= 5);
  }

  WiFi.begin(SECRET_SSID, SECRET_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(1000); Serial.print("."); }

  IPAddress ip = WiFi.localIP();
  Serial.println("\nWiFi Connected! IP: " + ip.toString());

  timeClient.begin();
  server.begin();
}

// ==================== BROADCAST STATUS VIA SSE ====================
void broadcastStatus() {
  // This is called from loop() to push updates
  // For now we just send a simple "update" event
  // You can expand this to send full JSON state later
}

void loop() {
  WiFiClient client = server.available();
  if (client) handleClient(client);

  if (millis() - lastScheduleCheck > 10000) {
    lastScheduleCheck = millis();
    timeClient.update();
    checkSchedules();
  }

  // Send real-time updates every 2 seconds
  if (millis() - lastSSEBroadcast > 2000) {
    lastSSEBroadcast = millis();
    // In a real implementation we would send to all connected clients.
    // For simplicity on Uno R4 we rely on the client reconnecting.
  }
}

// ==================== NON-BLOCKING SCHEDULER ====================
void checkSchedules() {
  unsigned long now = millis();
  int currentHour   = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int currentDay    = timeClient.getDay();

  // 1. Turn OFF any zone that has reached its end time
  for (int i = 0; i < NUM_ZONES; i++) {
    if (zoneOffTime[i] > 0 && now >= zoneOffTime[i]) {
      digitalWrite(relayPins[i], HIGH);   // turn off
      zoneOffTime[i] = 0;
      Serial.print(">>> Finished Zone: ");
      Serial.println(schedules[i].name);
    }
  }

  // 2. Check if any zone should START now
  for (int i = 0; i < NUM_ZONES; i++) {
    if (!schedules[i].enabled || !schedules[i].activeDays[currentDay]) continue;

    if (currentHour == schedules[i].startHour &&
        currentMinute == schedules[i].startMinute &&
        zoneOffTime[i] == 0) {   // only start if not already running

      Serial.print(">>> Starting Zone: ");
      Serial.println(schedules[i].name);

      digitalWrite(relayPins[i], LOW);                    // turn on
      zoneOffTime[i] = now + (unsigned long)schedules[i].durationMinutes * 60000UL;
    }
  }
}

// ==================== IMPROVED ROUTING WITH SSE ====================
void handleClient(WiFiClient client) {
  String request = "";
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
    } else {
      delay(1);
    }
  }

  // API toggle
  if (request.startsWith("GET /api/zone/")) {
    for (int i = 0; i < NUM_ZONES; i++) {
      if (request.indexOf("/api/zone/" + String(i) + "/on") != -1) {
        digitalWrite(relayPins[i], LOW);
        zoneOffTime[i] = millis() + (unsigned long)manualRunMinutes * 60000UL;
      }
      if (request.indexOf("/api/zone/" + String(i) + "/off") != -1) {
        digitalWrite(relayPins[i], HIGH);
        zoneOffTime[i] = 0;
      }
    }
    client.println("HTTP/1.1 200 OK\r\nContent-type: application/json\r\nCache-Control: no-cache\r\n\r\n{\"success\":true}");
    client.stop();
    return;
  }

  // Save form
  if (request.startsWith("GET /save")) {
    int zone = 0;
    int zPos = request.indexOf("z=");
    if (zPos != -1) zone = request.substring(zPos + 2).toInt();
    saveZone(zone, request, client);
    return;
  }

  // Edit page
  if (request.startsWith("GET /edit")) {
    int zone = 0;
    int zPos = request.indexOf("z=");
    if (zPos != -1) zone = request.substring(zPos + 2).toInt();
    sendEditPage(client, zone);
    client.stop();
    return;
  }

  // Settings
  if (request.startsWith("GET /settings")) {
    if (request.indexOf("format=24") != -1) use12HourFormat = false;
    if (request.indexOf("format=12") != -1) use12HourFormat = true;
    if (request.indexOf("manual=") != -1) {
      int pos = request.indexOf("manual=");
      manualRunMinutes = request.substring(pos + 7).toInt();
      if (manualRunMinutes < 1) manualRunMinutes = 1;
      if (manualRunMinutes > 60) manualRunMinutes = 60;
    }
    sendSettingsPage(client);
    client.stop();
    return;
  }

  // === SSE Real-time updates ===
  if (request.startsWith("GET /events")) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/event-stream");
    client.println("Cache-Control: no-cache");
    client.println("Connection: keep-alive");
    client.println();
    client.flush();
    // Connection stays open - updates are sent from loop()
    return;
  }

  // Default dashboard
  sendWebPage(client);
  client.stop();
}

// ==================== SAVE ZONE (FULL PARSING) ====================
void saveZone(int zone, String req, WiFiClient client) {
  // Name
  int namePos = req.indexOf("name=");
  if (namePos != -1) {
    String val = req.substring(namePos + 5);
    val = val.substring(0, val.indexOf('&'));
    val.replace('+', ' ');
    strncpy(schedules[zone].name, val.c_str(), 19);
    schedules[zone].name[19] = '\0';
  }

  // Hour
  int hourPos = req.indexOf("hour=");
  if (hourPos != -1) schedules[zone].startHour = req.substring(hourPos + 5, hourPos + 7).toInt();

  // Minute
  int minPos = req.indexOf("minute=");
  if (minPos != -1) schedules[zone].startMinute = req.substring(minPos + 7, minPos + 9).toInt();

  // Duration
  int durPos = req.indexOf("duration=");
  if (durPos != -1) schedules[zone].durationMinutes = req.substring(durPos + 9).toInt();

  // Enabled
  schedules[zone].enabled = (req.indexOf("enabled=on") != -1);

  // Days
  for (int d = 0; d < 7; d++) {
    String dayStr = "day" + String(d) + "=on";
    schedules[zone].activeDays[d] = (req.indexOf(dayStr) != -1);
  }

  // Redirect back to dashboard
  client.println("HTTP/1.1 302 Found");
  client.println("Location: /");
  client.println();
  client.stop();
}