// arduino_secrets_template.h
// Copy this file to arduino_secrets.h and fill in your real values

#define SECRET_SSID "YOUR_WIFI_NAME"
#define SECRET_PASS "YOUR_WIFI_PASSWORD"

#define MQTT_SERVER "192.168.1.XXX"   // your Raspberry Pi IP
#define MQTT_PORT   1883
#define MQTT_USER   ""
#define MQTT_PASS   ""
cd sprinkler/
cat > arduino_secrets_template.h << EOF
// arduino_secrets_template.h
// Copy this file to arduino_secrets.h and fill in your real values

#define SECRET_SSID "YOUR_WIFI_NAME"
#define SECRET_PASS "YOUR_WIFI_PASSWORD"

#define MQTT_SERVER "192.168.1.XXX"   // your Raspberry Pi IP
#define MQTT_PORT   1883
#define MQTT_USER   ""
#define MQTT_PASS   ""
