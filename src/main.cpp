// Read active zone and status and control a Crow Runner 8/16 Alarm System
// Pinout option 1: Convert CROW 5V clock (CLK) and data (DAT) keypad signals to 3.3V with a logic level converter connected to GPIO pins D5, D6 of the Esp8266
//if you want to use just the bus.
// Pinout option 2: Convert CROW 5V clock (CLK) and data (DAT) keypad signals to 3.3V with a resistor divider connected to GPIO pins D5, D6 of the Esp8266
//if you only want reporting or want control using relays and keyswitch simulation.
// Also connect crow NEG to Esp8266 GND (pin 6)

#include <Arduino.h>
#include <deque>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

#define WDT_TIMEOUT_S 8 // Set the WDT timeout to 8 seconds

const int statusAddress = 0;
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

const int clockPin = D6;
const int dataPin = D7;
const int parcialPin = D1;
const int totalPin = D2;
const int alarmePin = D5;

const int boundaryLen = 8;
const int bufferSize = 128;

bool debugalarme = false;
bool zonedata = false;

std::deque<int> dataBuffer;
int insideState = 1;
int consecutiveOnes = 0;
int boundaryAge = 0;
bool cansend = false;
// Define the size of the binary array in bytes
const int binarySizeBytes = 1;  // 8 bits

WiFiClient espClient;
PubSubClient client(espClient);
bool otaInProgress = false;

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
        client.publish(mqttStateTopic, "Chime", true);
        break;
      case 5:
        client.publish(mqttStateTopic, "A armar Total", true);
        break;
      case 6:
        client.publish(mqttStateTopic, "A armar Parcial", true);
        break;
    }
}

void printBuffer(const std::deque<int>& buffer, unsigned int length) {
  if (buffer.size() >= length) {
    unsigned int inicio = buffer.size() - length;
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
        // Pad the hexadecimal value with leading zeros to ensure it always has 2 characters
        String hexByte = String(value, HEX);
        if (hexByte.length() == 1) {
            hexByte = "0" + hexByte; // Add a leading zero if necessary
        }
        hexValue += hexByte;
      }
      client.publish(debugTopic, hexValue.c_str());
    }

    if (length == 72) {
      bool activeZoneDetected = false;
      int multiplicador = 0;
      int multiplic = inicio + 16;
      int statusrep = inicio + 63;
      int statu1 = inicio + 24;
      int statu2 = inicio + 25;
      int statu3 = inicio + 27;
      int jaarmado = inicio + 26;
      int total = inicio + 48;
      int parcial = inicio + 56;
      if (buffer[statusrep] == 0) {  //bit 63 is 0 when the messages report the zones and not status changes
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
            activeZoneDetected = true;
          }
        }
      } else { //handle status messages
        if (buffer[statu1] == 1 && buffer[statu2] == 1 && buffer[statu3] == 1) { //triggered
          if (buffer[jaarmado] == 1) {
            status = 3;
            Serial.println("Disparado");
          } else {
            status = 4;
            Serial.println("Chime");
          }
        } else if (buffer[statu1] == 1 && buffer[statu2] == 0 && buffer[jaarmado] == 1) { //disarmed
            status = 0;
            Serial.println("Desarmado");
        } else if (buffer[parcial] == 1 && status != 3) { //bit 56 is 1 when the alarm is armed partially and 0 if totally
          if ((buffer[statu2] == 0 && buffer[jaarmado] == 1) || (buffer[statu1] == 1 && buffer[statu3] == 0)) {
            Serial.println("Armado Parcial");
            status = 2;
          } else if (status != 2) {
            Serial.println("A armar Parcial");
            status = 6;
          }
        } else if (buffer[total] == 1 && status != 3) {
          if ((buffer[statu2] == 0 && buffer[jaarmado] == 1) || (buffer[statu1] == 1 && buffer[statu3] == 0)) {
            Serial.println("Armado Total");
            status = 1;
          } else if (status != 1) {
            Serial.println("A armar Total");
            status = 5;
          }
        } else if (status != 3) { //disarmed
          status = 0;
          Serial.println("Desarmado");
        } else if (buffer[statu1] == 0) { //disarm successful
          status = 0;
          Serial.println("Desarmado");
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
          // Pad the hexadecimal value with leading zeros to ensure it always has 2 characters
          String hexByte = String(value, HEX);
          if (hexByte.length() == 1) {
              hexByte = "0" + hexByte; // Add a leading zero if necessary
          }
          hexValue += hexByte;
        }
        client.publish(activeZoneTopic, hexValue.c_str());
      }
    }
  }
}

void IRAM_ATTR clockCallback() {
  // Check if OTA update is in progress, and disable the interrupt if it is
  if (otaInProgress) {
    return;
  }
  int dbit = digitalRead(dataPin);
  bool boundaryfound = false;
  if (dbit == 1) {
    dataBuffer.push_back(dbit);
    boundaryAge++;
    consecutiveOnes++;
  } else if (dbit == 0 && consecutiveOnes == 5) {
    consecutiveOnes = 0;
  } else if (dbit == 0 && consecutiveOnes == 6) {
    dataBuffer.push_back(dbit);
    boundaryAge++;
    boundaryfound = true;
    consecutiveOnes = 0;
  } else {
    dataBuffer.push_back(dbit);
    boundaryAge++;
    consecutiveOnes = 0;
  }

  if (dataBuffer.size() > bufferSize) {
    dataBuffer.pop_front();
  }

  if (consecutiveOnes >= 10 && insideState == 0) {
    consecutiveOnes = 10; //prevent from increasing too much
    cansend = true;
  } else {
    cansend = false;
  }

  if (dataBuffer.size() == bufferSize && boundaryfound) {
    if (insideState == 1) {
      int messagesize = boundaryAge + boundaryLen;
      printBuffer(dataBuffer, messagesize);
    }
    insideState = insideState == 0 ? 1 : 0;
    boundaryAge = 0;
  }

  if (boundaryAge > bufferSize) {
    insideState = 0;
    boundaryAge = -1;
  }
}

// Function to send a packet to the alarm system
void sendPacket(const byte* binaryData, size_t dataSize) {
  // Stop receiving
  //detachInterrupt(digitalPinToInterrupt(clockPin));
  // Ensure dataPin is set as an output
  while (!cansend) {
    delayMicroseconds(100);
    yield();
  }
  pinMode(dataPin, OUTPUT);

  for (size_t i = 0; i < dataSize; i++) {
    byte currentByte = binaryData[i];

    for (int bit = 7; bit >= 0; bit--) {
      // Wait for the clock to be low
      while (digitalRead(clockPin) != LOW) {
        delayMicroseconds(10);
      }

      // Set the data line (dataPin) to the current bit value
      digitalWrite(dataPin, (currentByte >> bit) & 1);

      // Wait for the clock to go high
      while (digitalRead(clockPin) != HIGH) {
        delayMicroseconds(10);
      }
    }
  }

  // Wait for the clock to be low before ending transmission
  while (digitalRead(clockPin) != LOW) {
    delayMicroseconds(10);
  }

  // Send a final 0
  digitalWrite(dataPin, LOW);

  // Wait for the clock to go high
  while (digitalRead(clockPin) != HIGH) {
    delayMicroseconds(10);
  }

  while (digitalRead(clockPin) != LOW) {
    delayMicroseconds(10);
  }

  // Ensure data line stays high
  digitalWrite(dataPin, HIGH);

  // Wait for the clock to go high
  while (digitalRead(clockPin) != HIGH) {
    delayMicroseconds(10);
  }

  pinMode(dataPin, INPUT);

  // Restart receiving
  //attachInterrupt(digitalPinToInterrupt(clockPin), clockCallback, FALLING);
  delay(50);
}

// Function to send keypad button presses
void sendBinaryPacket(int decimalNumber) {
  byte binaryArray[binarySizeBytes + 4];  // 8 bits + leading/trailing bytes

  // Initialize the binary array
  memset(binaryArray, 0, sizeof(binaryArray));

  // Add leading bytes
  binaryArray[0] = 0b01111110;
  binaryArray[1] = 0b10000101;
  binaryArray[2] = 0b00000000;

  // Convert decimal to binary and reverse the bits
  for (int i = 0; i < 8; i++) {
    binaryArray[3] |= ((decimalNumber & 1) ? 1 : 0) << (7 - i);
    decimalNumber >>= 1;
  }

  // Add trailing byte
  binaryArray[4] = 0b01111110;  // Trailing byte

  // Send the constructed binary data
  sendPacket(binaryArray, binarySizeBytes + 4);
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Check if OTA update is in progress, and disable the interrupt if it is
  if (otaInProgress) {
    return;
  }
  payload[length] = '\0';
  String receivedPayload = String((char*)payload);
  
  if (strcmp(topic, mqttControlTopic) == 0) {
    if (receivedPayload == "parcialpin") {
      client.publish(logTopic, "Activado Keyswitch da Guarda Parcial");
      activatePin(parcialPin, 1000);
    } else if (receivedPayload == "totalpin") {
      client.publish(logTopic, "Activado Keyswitch da Guarda Total");
      activatePin(totalPin, 1000);
    } else if (receivedPayload == "alarmepin") {
      client.publish(logTopic, "Alarme despoletado activamente");
      activatePin(alarmePin, 1000);
    } else if (receivedPayload == "parcial") {
      client.publish(logTopic, "Activada Guarda Parcial");
      sendBinaryPacket(17); //Send "enter" at the beggining to "wake up the system"
      sendBinaryPacket(14);
    } else if (receivedPayload == "total") {
      client.publish(logTopic, "Activada Guarda Total");
      sendBinaryPacket(17); //Send "enter" at the beggining to "wake up the system"
      sendBinaryPacket(13);
    } else if (receivedPayload == "alarme") {
      client.publish(logTopic, "Alarme despoletado activamente");
      sendBinaryPacket(17); //Send "enter" at the beggining to "wake up the system"
      sendBinaryPacket(32);
    } else if (receivedPayload == "actualizar") {
      client.publish(logTopic, "Actualizar...");
      sendBinaryPacket(1);
      sendBinaryPacket(17);
    } else if (receivedPayload == "enter") {
      client.publish(logTopic, "enter");
      sendBinaryPacket(17);
    } else if (receivedPayload == "1") {
      client.publish(logTopic, "1");
      sendBinaryPacket(1);
    //receive the code after "desarmar-" or "desarmar " and send it to the alarm to deactivate it
    } else if (receivedPayload.startsWith("desarmar-") || receivedPayload.startsWith("desarmar ")) {
      sendBinaryPacket(17); //Send "enter" at the beggining to "wake up the system"
      String digits = receivedPayload.substring(9); // Get the digits after "desarmar-"
      for (size_t i = 0; i < digits.length(); i++) {
        char digitChar = digits.charAt(i);
        if (isdigit(digitChar)) {
          int digit = digitChar - '0'; // Convert char to integer
          sendBinaryPacket(digit);
        }
      }
      sendBinaryPacket(17); //Send "enter" at the end
      client.publish(logTopic, "Desarmado");
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

  if (statussaved >= 0 && statussaved <= 6) { // Ensure it's a valid value (greater than or equal to 1 and smaller or equal to 5)
    if (statussaved >= 5){
      status = statussaved - 4;
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

  WiFi.persistent(true);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  WiFi.setAutoReconnect(true);
  Serial.println("Connected to WiFi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());

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

  // Initialize OTA
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    Serial.println("OTA update started...");
  });
  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    Serial.println("\nOTA update finished!");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("OTA authentication failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("OTA begin failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("OTA connect failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA receive failed");
    else if (error == OTA_END_ERROR) Serial.println("OTA end failed");
  });

  // Start OTA
  ArduinoOTA.begin();
  
  //Get alarm status
  sendBinaryPacket(1);
  sendBinaryPacket(17);
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
        yield();
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
  // Handle OTA updates
  ArduinoOTA.handle();
}
