// Read active zone from Crow Runner 8/16 Alarm System
// Pinout: Convert CROW 5V clock (CLK) and data (DAT) keypad signals to 3.3V with a resistor divider or other means. 
// Then connect clock (CLK) and data (DAT) to GPIO pins D5, D6 for Esp8266
// Also connect crow NEG to Esp8266 GND

#include <Arduino.h>
#include <deque>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

#define WDT_TIMEOUT_S 8 // Set the WDT timeout to 8 seconds

int statusAddress = 0;
byte status = 0;
byte statussaved = 0;

unsigned long startupTime;

const char* ssid = "YourWifiSSID";
const char* password = "YourWifiPassword";

const char* mqttServer = "YourMqttServeraddress";
const int mqttPort = 1883; //port used by the Mqtt broker - usually 1883
const char* mqttID = "AlarmESP8266";
const char* mqttUser = "YourMqttUsername";
const char* mqttPassword = "YourMqttPassword";
const char* mqttTopic = "Alarm/active_zones";
const char* mqttStateTopic = "Alarm/status";
const char* mqttControlTopic = "Alarm/control"; //this topic is used to control the alarm activation, as well as to activate debug data for the alarm protocol - this data was extremely usefull for the development, but is probably irrelevant now
const char* lwtTopic = "Alarm/lwt";
const char* birthMessage = "Online";
const char* lwtMessage = "Offline";
const char* activeZoneTopic = "Alarm/active_zone_data"; //Topic for debug data
const char* debugTopic = "Alarm/debug_data"; //Topic for debug data
const char* logTopic = "Alarm/log"; //Topic where parts of the log are published, like restart reason and some changes to the status
const char* teleTopic = "Alarm/tele"; //Topic for the telemetry

const char* resetCause;

const int clockPin = D5;
const int dataPin = D6;
const int parcialPin = D1;
const int totalPin = D2;
const int alarmePin = D7;

const int boundaryLen = 8;
const int bufferSize = 200;

bool debugalarme = false;
bool zonedata = false;

std::deque<int> dataBuffer;
int insideState = 1;
int boundaryAge = 0;
int boundary[boundaryLen] = {0, 1, 1, 1, 1, 1, 1, 0};

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long previousMillis = 0;
unsigned long previousMillistele = 0;
const unsigned long interval = 1000;
const unsigned long intervaltele = 10000;
unsigned long lastActivationTime = 0;

void activatePin(int pin, unsigned long duration) {
  digitalWrite(pin, HIGH);
  lastActivationTime = millis();
  while (millis() - lastActivationTime < duration) {
    // Wait for the specified duration
    ESP.wdtFeed();
    yield();
  }
  digitalWrite(pin, LOW);
}

void publishStatus(byte estado) {
  switch (estado) {
      case 0:
        client.publish(mqttStateTopic, "Desarmado", true);
        break;
      case 1:
        client.publish(mqttStateTopic, "Armado Total", true);
        break;
      case 2:
        client.publish(mqttStateTopic, "Armado Parcial", true);
        break;
      case 3:
        client.publish(mqttStateTopic, "Alarme Despoletado", true);
        break;
      case 4:
        client.publish(mqttStateTopic, "A armar Total", true);
        break;
      case 5:
        client.publish(mqttStateTopic, "A armar Parcial", true);
        break;
    }
}

void printBuffer(const std::deque<int>& buffer, unsigned int length) {
  unsigned int inicio = buffer.size() - length;
  if (buffer.size() >= length) {
    for (unsigned int i = inicio; i < buffer.size(); i++) {
      Serial.print(buffer[i]);
    }
    Serial.println();

    if (debugalarme) {
      String hexValue = "";
      for (unsigned int i = inicio; i < buffer.size(); i += 8) {
        int value = 0;
        for (int j = 0; j < 8; j++) {
          value |= (buffer[i + j] << (7 - j));
        }
        hexValue += String(value, HEX);
      }
      client.publish(debugTopic, hexValue.c_str());
    }
    
    if (length == 72) {
      bool activeZoneDetected = false;
      int multiplicador = 0;
      int multiplic = inicio + 16;
      int armado = inicio + 63;
      int aarmar = inicio + 31;
      int total = inicio + 48;
      int parcial = inicio + 56;
      if (buffer[armado] == 0) {  //bit 63 is 0 when the messages report the zones and not status changes
        if (buffer[multiplic] == 1) {  //bit 16 is 1 when the the active zones are from 9 to 16
          multiplicador = 8; //so you must add 8 to the index number to get the actual zone
          }
        for (unsigned int i = inicio + 24; i < inicio + 32; i++) {
          if (buffer[i] == 1) {
            char message[20];
            snprintf(message, sizeof(message), "%d activo", i - inicio - 23 + multiplicador);
            client.publish(mqttTopic, message);
            Serial.print(message);
            Serial.println();
            activeZoneDetected = true;
          }
        }
        for (unsigned int i = inicio + 32; i < inicio + 40; i++) { //when the alarm is triggered, the triggered zone is in these bits
          if (buffer[i] == 1) {
            char message[20];
            snprintf(message, sizeof(message), "%d triggered", i - inicio - 31 + multiplicador);
            client.publish(mqttTopic, message);
            Serial.print(message);
            Serial.println();
            status = 3;
            publishStatus(status);
            EEPROM.put(statusAddress, status);
            EEPROM.commit(); // Commit the changes to EEPROM
          }
        }
      } else {
        if (buffer[total] == 0 && buffer[parcial] == 0) { //if both are not 1 the alarm is being disarmed
          status = 0;
          Serial.println("Desarmado");
        } else if (buffer[parcial] == 1) { //bit 56 is 1 when the alarm is armed partially and 0 if totally
          if (buffer[aarmar] == 1) { //bit 31 is 1 when the alarm is being armed (the keypad is chimming))
            Serial.println("A armar Parcial");
            status = 5;
          } else {
            Serial.println("Armado Parcial");
            status = 2;
          }
        } else {
          if (buffer[aarmar] == 1) {
            Serial.println("A armar Total");
            status = 4;
          } else {
            Serial.println("Armado Total");
            status = 1;
          }
        }
        publishStatus(status);
        //prevent unnecessary writting to flash
        EEPROM.get(statusAddress, statussaved);
        if (statussaved != status) {
          EEPROM.put(statusAddress, status);
          EEPROM.commit(); // Commit the changes to EEPROM
        }
        
      }
      if (activeZoneDetected && zonedata) {
        String hexValue = "";
        for (unsigned int i = inicio; i < buffer.size(); i += 8) {
          int value = 0;
          for (int j = 0; j < 8; j++) {
            value |= (buffer[i + j] << (7 - j));
          }
          hexValue += String(value, HEX);
        }
        client.publish(activeZoneTopic, hexValue.c_str());
      }
    }
  }
}

void IRAM_ATTR clockCallback() {
  int dbit = digitalRead(dataPin);
  dataBuffer.push_back(dbit);

  if (dataBuffer.size() > bufferSize) {
    dataBuffer.pop_front();
  }

  boundaryAge++;

  int lastIndex = bufferSize - 1;

  if (dataBuffer.size() == bufferSize && dataBuffer[lastIndex] == 0 && 
      dataBuffer[lastIndex - 1] == 1 && dataBuffer[lastIndex - 2] == 1 && 
      dataBuffer[lastIndex - 3] == 1 && dataBuffer[lastIndex - 4] == 1 && 
      dataBuffer[lastIndex - 5] == 1 && dataBuffer[lastIndex - 6] == 1 && 
      dataBuffer[lastIndex - 7] == 0) {
    if (insideState == 1) {
      printBuffer(dataBuffer, boundaryAge + boundaryLen);
    }
    insideState = insideState == 0 ? 1 : 0;
    boundaryAge = 0;
  }

  if (boundaryAge > bufferSize) {
    insideState = 0;
    boundaryAge = -1;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String receivedPayload = String((char*)payload);
  
  if (receivedPayload == "parcial") {
    client.publish(logTopic, "Activada Guarda Parcial");
    activatePin(parcialPin, 1000);
  } else if (receivedPayload == "total") {
    client.publish(logTopic, "Activada Guarda Total");
    activatePin(totalPin, 1000);
  } else if (receivedPayload == "alarme") {
    client.publish(logTopic, "Alarme despoletado activamente");
    activatePin(alarmePin, 1000);
  } else if (receivedPayload == "debugon") {
    client.publish(logTopic, "Debug on!");
    Serial.println("Debug on!");
    debugalarme = true;
  } else if (receivedPayload == "debugoff") {
    client.publish(logTopic, "Debug off!");
    Serial.println("Debug off!");
    debugalarme = false;
    zonedata = false;
  } else if (receivedPayload == "zonedataon") {
    client.publish(logTopic, "Dados da zona on!");
    Serial.println("Dados da zona on!");
    zonedata = true;
  } else if (receivedPayload == "zonedataoff") {
    client.publish(logTopic, "Dados da zona off!");
    Serial.println("Dados da zona off!");
    zonedata = false;
  } else if (receivedPayload == "restart") {
    client.publish(logTopic, "A reiniciar...");
    Serial.println("Restart..");
    delay(1000);
    ESP.restart();
  }
}

void setup() {
  // Enable the Watchdog Timer
  ESP.wdtEnable(WDT_TIMEOUT_S * 1000000); // Convert seconds to microseconds

  // Get reset cause
  rst_info* resetInfo = ESP.getResetInfoPtr();
  String resetCause;
  switch (resetInfo->reason) {
    case REASON_DEFAULT_RST:
      resetCause = "Power-on reset";
      break;
    case REASON_WDT_RST:
      resetCause = "Watchdog reset";
      break;
    case REASON_EXCEPTION_RST:
      resetCause = "Exception reset";
      break;
    case REASON_SOFT_WDT_RST:
      resetCause = "Software Watchdog reset";
      break;
    case REASON_SOFT_RESTART:
      resetCause = "Software restart";
      break;
    case REASON_DEEP_SLEEP_AWAKE:
      resetCause = "Deep sleep wake-up";
      break;
    case REASON_EXT_SYS_RST:
      resetCause = "External reset";
      break;
    default:
      resetCause = "Unknown reset cause";
      break;
  }

  startupTime = millis(); // Record the startup time in milliseconds

  Serial.begin(115200);

  EEPROM.begin(1); // Initialize EEPROM with the number of bytes needed
  EEPROM.get(statusAddress, statussaved);

  if (statussaved >= 0 && statussaved <= 5) { // Ensure it's a valid value (greater than or equal to 1 and smaller or equal to 3)
    if (statussaved >= 4){
      status = statussaved - 3;
    } else {
      status = statussaved;
    }
    Serial.println();
    Serial.println("Status recuperado da EEPROM");
  }

  pinMode(clockPin, INPUT);
  pinMode(dataPin, INPUT);
  pinMode(parcialPin, OUTPUT);
  pinMode(totalPin, OUTPUT);
  pinMode(alarmePin, OUTPUT);
  
  delay(2000);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  Serial.println("Connected to WiFi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqttServer, mqttPort);
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect(mqttID, mqttUser, mqttPassword, lwtTopic, 0, 1, lwtMessage)) {
      Serial.println("Connected to MQTT");
      client.publish(lwtTopic, birthMessage, true);
      client.subscribe(mqttControlTopic);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }

  client.publish(logTopic, "Started");
  // Publish reset cause to logTopic
  const char* resetCauseChar = resetCause.c_str();
  client.publish(logTopic, resetCauseChar);

  attachInterrupt(digitalPinToInterrupt(clockPin), clockCallback, FALLING);

  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    Serial.println("Reconnecting to MQTT...");
    if (client.connect(mqttID, mqttUser, mqttPassword, lwtTopic, 0, 1, lwtMessage)) {
      Serial.println("Reconnected to MQTT");
      client.publish(lwtTopic, birthMessage, true);
      client.publish(logTopic, "Reconnected to MQTT");
      client.subscribe(mqttControlTopic);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      unsigned long retryDelay = 5000;
      while (millis() - previousMillis < retryDelay) {
        // Wait for the retryDelay
      }
      previousMillis = millis();
      return;
    }
  }
  client.loop();

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    // Perform your action here at the specified interval
    ESP.wdtFeed();
    previousMillis = currentMillis;
  }

  if (millis() - previousMillistele >= intervaltele) {
    // Perform your action here at the specified interval in intervaltele
    // send tele values

    // Calculate uptime in milliseconds
    unsigned long uptimeMillis = millis() - startupTime;
    // Convert milliseconds to HH:MM:SS format
    unsigned long seconds = uptimeMillis / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24; // Calculate days
    seconds %= 60;
    minutes %= 60;
    hours %= 24; // Ensure hours don't exceed 24

    // Create a formatted string in DDd HH:MM:SS format
    char uptimeStr[20]; // Format: DDd HH:MM:SS\0
    sprintf(uptimeStr, "%luD %02lu:%02lu:%02lu", days, hours, minutes, seconds);
    // Create a JSON object
    StaticJsonDocument<128> jsonDoc;
    // Add the uptime, RSSI and IP values to the JSON object
    jsonDoc["Uptime"] = String(uptimeStr);
    jsonDoc["IP"] = WiFi.localIP();
    jsonDoc["RSSI"] = WiFi.RSSI();
  
    // Serialize the JSON object to a string
    String jsonStr;
    serializeJson(jsonDoc, jsonStr);
    // Publish the JSON string to the MQTT teleTopic
    client.publish(teleTopic, jsonStr.c_str());

    publishStatus(status);

    previousMillistele = millis();
  }

  // Other non-blocking tasks can go here
}
