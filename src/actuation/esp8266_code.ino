
  MQTT Broker : broker.emqx.io:1883  
             
  Topic       : mnnit/dashboard/control

 ================================================================================
  GPIO Pinout (NodeMCU D-pin -> GPIO mapping)
  ------------------------------------------------------------------------------
   D-Pin | GPIO | Component                 | Active State
  -------+------+---------------------------+----------------
   D1    |  5   | 5V Active Buzzer          | HIGH = ALARM ON
   D2    |  4   | Red LED (Alarm)           | HIGH = ON
   D3    |  0   | Yellow LED (Left Ind.)    | HIGH = ON
   D4    |  2   | Yellow LED (Right Ind.)   | HIGH = ON
   D5    | 14   | Headlights (LED output)   | HIGH = ON
  ------------------------------------------------------------------------------
  Note: GPIO2 (D4) is tied to the boot LED and must be LOW at reset.
 ================================================================================
  MQTT Payload -> Hardware Map
  ---------------------------------
   ALARM_ON   -> Buzzer & Red LED Pulse 3 Times
   ALARM_OFF  -> Buzzer & Red LED OFF
   IND_LEFT   -> Left LED Pulse 4 Times (Right OFF)
   IND_RIGHT  -> Right LED Pulse 4 Times (Left OFF)
   IND_OFF    -> Both LED LOW
   ON         -> Headlights HIGH (Continuous ON)
   OFF        -> Headlights LOW
 ================================================================================
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

const char* WIFI_SSID      = "SSID- NAME"; // SSID
const char* WIFI_PASSWORD  = "PASSWORD";   // Password


// Replace with IP if local, e.g. "192.168.1.104"
const char* MQTT_BROKER    = "broker.emqx.io";
const int   MQTT_PORT      = 1883;
const char* MQTT_CLIENT_ID = "esp8266_actuator_node";
const char* MQTT_TOPIC     = "mnnit/dashboard/control";


#define PIN_BUZZER      D8   // GPIO5  — Active Buzzer
#define PIN_LED_RED     D2   // GPIO4  — Red LED (alarm indicator)
#define PIN_LED_LEFT    D3   // GPIO0  — Yellow LED left turn
#define PIN_LED_RIGHT   D6   // GPIO2  — Yellow LED right turn
#define PIN_HEADLIGHTS  D5   // GPIO14 — Headlights output

const unsigned long WIFI_RECONNECT_INTERVAL_MS  = 5000;
const unsigned long MQTT_RECONNECT_INTERVAL_MS  = 3000;

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastWifiAttempt  = 0;
unsigned long lastMqttAttempt  = 0;


// Alarm Variables
bool alarmActive = false;
bool alarmPinState = false;
int alarmPulseCount = 0;
unsigned long alarmLastToggle = 0;
const int ALARM_PULSE_MS = 300; // Duration of beep

// Indicator 
enum IndState { IND_NONE, IND_LEFT_ACTIVE, IND_RIGHT_ACTIVE };
IndState currentIndState = IND_NONE;
bool indPinState = false;
int indPulseCount = 0;
unsigned long indLastToggle = 0;
const int IND_PULSE_MS = 400; // Duration blink

void actuatorsOff() {
  digitalWrite(PIN_BUZZER,     LOW);
  digitalWrite(PIN_LED_RED,    LOW);
  digitalWrite(PIN_LED_LEFT,   LOW);
  digitalWrite(PIN_LED_RIGHT,  LOW);
  digitalWrite(PIN_HEADLIGHTS, LOW);

  alarmActive = false;
  currentIndState = IND_NONE;
}

void indicatorsOff() {
  currentIndState = IND_NONE;
  indPinState = false;
  indPulseCount = 0;
  digitalWrite(PIN_LED_LEFT, LOW);
  digitalWrite(PIN_LED_RIGHT, LOW);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  // Convert byte array to null-terminated String for easy comparison
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print(F("[MQTT] Received on ["));
  Serial.print(topic);
  Serial.print(F("] -> "));
  Serial.println(msg);

  // Alarm Control
  if (msg == "ALARM_ON") {
    alarmActive = true;
    alarmPinState = true;
    alarmPulseCount = 0;
    alarmLastToggle = millis();
    // Alarm has highest priority: hard-stop indicator activity.
    indicatorsOff();
    digitalWrite(PIN_BUZZER,  HIGH);
    digitalWrite(PIN_LED_RED, HIGH);
    Serial.println(F("  ACTION: Alarm Triggered (3 Beeps/Blinks)"));
  }
  else if (msg == "ALARM_OFF") {
    alarmActive = false;
    digitalWrite(PIN_BUZZER,  LOW);
    digitalWrite(PIN_LED_RED, LOW);
    Serial.println(F("  ACTION: Alarm OFF"));
  }


  // Indicator 

  else if (msg == "IND_LEFT") {
    if (alarmActive) {
      Serial.println(F("  ACTION: IND_LEFT ignored (alarm active)"));
      return;
    }
    currentIndState = IND_LEFT_ACTIVE;
    indPinState = true;
    indPulseCount = 0;
    indLastToggle = millis();
    digitalWrite(PIN_LED_LEFT,  HIGH);
    digitalWrite(PIN_LED_RIGHT, LOW); // Force other off
    Serial.println(F("  ACTION: Left Indicator ON (4 Blinks)"));
  }
  else if (msg == "IND_RIGHT") {
    if (alarmActive) {
      Serial.println(F("  ACTION: IND_RIGHT ignored (alarm active)"));
      return;
    }
    currentIndState = IND_RIGHT_ACTIVE;
    indPinState = true;
    indPulseCount = 0;
    indLastToggle = millis();
    digitalWrite(PIN_LED_LEFT,  LOW); // Force other off
    digitalWrite(PIN_LED_RIGHT, HIGH);
    Serial.println(F("  ACTION: Right Indicator ON (4 Blinks)"));
  }
  else if (msg == "IND_OFF") {
    indicatorsOff();
    Serial.println(F("  ACTION: Both Indicators OFF"));
  }


  // Headlights 

  else if (msg == "ON") {
    digitalWrite(PIN_HEADLIGHTS, HIGH);
    Serial.println(F("  ACTION: HEADLIGHT ON (Continuous)"));
  }
  else if (msg == "OFF") {
    digitalWrite(PIN_HEADLIGHTS, LOW);
    Serial.println(F("  ACTION: HEADLIGHT OFF"));
  }

  else {
    Serial.print(F("  WARNING: Unknown payload ignored -> "));
    Serial.println(msg);
  }
}

// Wi-Fi
bool maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  unsigned long now = millis();
  if (now - lastWifiAttempt < WIFI_RECONNECT_INTERVAL_MS) return false;
  lastWifiAttempt = now;

  Serial.print(F("[WiFi] Connecting to "));
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Blocking wait up to 6 s for this attempt only
  unsigned long deadline = millis() + 6000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WiFi] Connected [OK]  IP: "));
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println(F("[WiFi] Failed - will retry."));
    return false;
  }
}

// MQTT
bool maintainMqtt() {
  if (mqttClient.connected()) return true;

  unsigned long now = millis();
  if (now - lastMqttAttempt < MQTT_RECONNECT_INTERVAL_MS) return false;
  lastMqttAttempt = now;

  Serial.print(F("[MQTT] Connecting to "));
  Serial.print(MQTT_BROKER);
  Serial.print(':');
  Serial.println(MQTT_PORT);

  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println(F("[MQTT] Connected [OK]"));
    bool ok = mqttClient.subscribe(MQTT_TOPIC, 1);  // QoS 1
    Serial.print(F("[MQTT] Subscribed to "));
    Serial.print(MQTT_TOPIC);
    Serial.println(ok ? F(" [OK]") : F(" [X] (retry next cycle)"));
    return ok;
  } else {
    Serial.print(F("[MQTT] Failed - state="));
    Serial.println(mqttClient.state());
    return false;
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  delay(200);   // Allow USB-serial to enumerate on host
  Serial.println(F("\n\n=== Driver Monitoring - ESP8266 Actuator Node ==="));
  Serial.print(F("Chip ID : "));
  Serial.println(ESP.getChipId(), HEX);

  // GPIO init
  const uint8_t OUTPUT_PINS[] = {
    PIN_BUZZER, PIN_LED_RED, PIN_LED_LEFT, PIN_LED_RIGHT, PIN_HEADLIGHTS
  };
  for (uint8_t pin : OUTPUT_PINS) {
    digitalWrite(pin, LOW);   // Ensure LOW before enabling output drive
    pinMode(pin, OUTPUT);
  }
  Serial.println(F("[GPIO] All output pins initialised LOW"));

  // Flash LED (Self-test)
  for (uint8_t pin : OUTPUT_PINS) digitalWrite(pin, HIGH);
  delay(300);
  actuatorsOff();
  Serial.println(F("[GPIO] Self-test complete"));

  // Configure MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setKeepAlive(60);
  mqttClient.setBufferSize(256);

  // Start
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);          // Avoid excessive flash write cycles

  Serial.println(F("[SETUP] Complete - entering main loop."));
}

void loop() {
  if (!maintainWifi())  return;   // No WiFi -> nothing else to do
  if (!maintainMqtt())  return;   // No MQTT -> wait for reconnect

  mqttClient.loop();

  unsigned long currentMillis = millis();


  if (alarmActive) {
    if (currentMillis - alarmLastToggle >= ALARM_PULSE_MS) {
      alarmLastToggle = currentMillis;

      if (alarmPinState) {
        // Currently ON -> Turn OFF
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_LED_RED, LOW);
        alarmPinState = false;
        alarmPulseCount++;

        // Stop after 3 full ON/OFF cycles
        if (alarmPulseCount >= 3) {
          alarmActive = false;
        }
      } else {
        // Currently OFF -> Turn ON
        digitalWrite(PIN_BUZZER, HIGH);
        digitalWrite(PIN_LED_RED, HIGH);
        alarmPinState = true;
      }
    }

    if (currentIndState != IND_NONE || indPinState) {
      indicatorsOff();
    }
  }


  if (!alarmActive && currentIndState != IND_NONE) {
    if (currentMillis - indLastToggle >= IND_PULSE_MS) {
      indLastToggle = currentMillis;
      uint8_t activePin = (currentIndState == IND_LEFT_ACTIVE) ? PIN_LED_LEFT : PIN_LED_RIGHT;

      if (indPinState) {
        // Currently ON -> Turn OFF
        digitalWrite(activePin, LOW);
        indPinState = false;
        indPulseCount++;

        // Stop after 4 full ON/OFF cycles
        if (indPulseCount >= 4) {
          currentIndState = IND_NONE;
        }
      } else {
        // Currently OFF -> Turn ON
        digitalWrite(activePin, HIGH);
        indPinState = true;
      }
    }
  }
}
