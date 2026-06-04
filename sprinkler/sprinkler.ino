/*
  Time to pivot - no more  webserver
  
  Sprinkler Controller - MQTT + Home Assistant Edition
  Arduino Uno R4 WiFi - Minimal & Reliable
*/

#include "WiFiS3.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <PubSubClient.h>
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

int manualRunMinutes = 5;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -7 * 3600);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastScheduleCheck = 0;
unsigned long zoneOffTime[NUM_ZONES] = {0};
unsigned long lastMQTTReconnect = 0;

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_ZONES; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH);
    schedules[i].startHour = 6;
    schedules[i].startMinute = 0;
    schedules[i].durationMinutes = 15;
    schedules[i].enabled = (i < 4);
    for (int d = 0; d < 7; d++) schedules[i].activeDays[d] = (d >= 1 && d <= 5);
  }

  strcpy(schedules[0].name, "Front Lawn");
  strcpy(schedules[1].name, "Back Lawn");
  strcpy(schedules[2].name, "Raised Beds");
  strcpy(schedules[3].name, "Side Yard");
  strcpy(schedules[4].name, "Zone 5");
  strcpy(schedules[5].name, "Zone 6");

  WiFi.begin(SECRET_SSID, SECRET_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  timeClient.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqttClient.connected()) mqttReconnect();
  mqttClient.loop();

  if (millis() - lastScheduleCheck > 10000) {
    lastScheduleCheck = millis();
    timeClient.update();
    checkSchedules();
  }
}

// ==================== MQTT CALLBACK (Updated) ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String p = "";
  for (unsigned int i = 0; i < length; i++) p += (char)payload[i];

  for (int i = 0; i < NUM_ZONES; i++) {
    // === Manual ON/OFF ===
    String cmdTopic = "sprinkler/zone" + String(i + 1) + "/set";
    if (t == cmdTopic) {
      if (p == "ON") {
        digitalWrite(relayPins[i], LOW);
        zoneOffTime[i] = millis() + (unsigned long)manualRunMinutes * 60000UL;
      } else if (p == "OFF") {
        digitalWrite(relayPins[i], HIGH);
        zoneOffTime[i] = 0;
      }
      publishZoneStatus(i);
    }

    // === Enable / Disable Scheduling ===
    String enabledTopic = "sprinkler/zone" + String(i + 1) + "/enabled/set";
    if (t == enabledTopic) {
      schedules[i].enabled = (p == "ON");
      publishEnabledStatus(i);           // Publish new state
    }
  }
}

void mqttReconnect() {
  if (millis() - lastMQTTReconnect < 5000) return;
  lastMQTTReconnect = millis();

  Serial.print("Connecting to MQTT... ");

  if (mqttClient.connect("SprinklerUnoR4", MQTT_USER, MQTT_PASS)) {
    Serial.println("connected");

    for (int i = 0; i < NUM_ZONES; i++) {
      mqttClient.subscribe(("sprinkler/zone" + String(i + 1) + "/set").c_str());
      mqttClient.subscribe(("sprinkler/zone" + String(i + 1) + "/enabled/set").c_str());
    }

    // Publish current states
    for (int i = 0; i < NUM_ZONES; i++) {
      publishZoneStatus(i);
      publishEnabledStatus(i);
    }

  } else {
    Serial.println("failed");
  }
}

void publishZoneStatus(int zone) {
  bool isOn = (digitalRead(relayPins[zone]) == LOW);
  String topic = "sprinkler/zone" + String(zone + 1) + "/state";
  
  // Important: retain = true so HA remembers the state
  mqttClient.publish(topic.c_str(), isOn ? "ON" : "OFF", true);
}

void publishEnabledStatus(int zone) {
  String topic = "sprinkler/zone" + String(zone + 1) + "/enabled/state";
  mqttClient.publish(topic.c_str(), schedules[zone].enabled ? "ON" : "OFF", true);
}

// ==================== NON-BLOCKING SCHEDULER ====================
void checkSchedules() {
  unsigned long now = millis();
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  int day = timeClient.getDay();

  // Turn off zones that have reached end time
  for (int i = 0; i < NUM_ZONES; i++) {
    if (zoneOffTime[i] > 0 && now >= zoneOffTime[i]) {
      digitalWrite(relayPins[i], HIGH);
      zoneOffTime[i] = 0;
      publishZoneStatus(i);
    }
  }

  // Check for scheduled starts
  for (int i = 0; i < NUM_ZONES; i++) {
    if (!schedules[i].enabled || !schedules[i].activeDays[day]) continue;

    if (h == schedules[i].startHour && m == schedules[i].startMinute && zoneOffTime[i] == 0) {
      Serial.print("Scheduled start → Zone ");
      Serial.println(schedules[i].name);
      digitalWrite(relayPins[i], LOW);
      zoneOffTime[i] = now + (unsigned long)schedules[i].durationMinutes * 60000UL;
      publishZoneStatus(i);
    }
  }
}