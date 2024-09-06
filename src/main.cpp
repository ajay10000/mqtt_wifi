// Test file for mqtt/wifi.  Measures voltage on ADC pin.
// and sends the data to a mqtt server.

#include <string>
#include <mqtt_wifi_ota.h>  // Private library

// Edit user variables below as required
#define MY_DEBUG                // Enable debug to serial monitor
// MQTT, WiFi & OTA Updates managed by mqtt_wifi_ota.h library with these variables
const std::string updatePath = "/firmware/";    // /path/ on webserver to firmware update
const std::string fwName = "test1_mqtt_ota";  // Firmware file name on webserver
const std::string revName = "v2t";  // Full name including revision
const std::string updateString = updatePath + fwName;
const std::string loggerName = "test-1"; // Unique logger/MQTT client ID
const std::string mqttTopicPrefix = "tester/";   // MQTT location/path. Leave empty for no path
const std::string mqttTopic = mqttTopicPrefix + loggerName + "/";

unsigned short sleepTime = 30;      // Loop/sleep delay in seconds
bool sleepIsOn = false;          // loop instead of sleep
unsigned short loopTime = 15;  // Loop timer when not sleeping
unsigned long loopCount = 1;   // Track loop count if not sleeping
const short ledPin = 33;    // ESP32 p18 LED_BUILTIN, ESP07/12 module is p2, ESP32-S2 p15
const bool LEDOFF = LOW;   // NodeMCU LED inverted (HIGH)

// Battery voltage measurement
const short BAT_SENSE_PIN = 6; // Analog input for battery sense. A0 for 8266. GPIO8 for ESP32 devkit battery
const int R1 = 470;       // kohm - 8266 Built in voltage divider 220 // 100k
const int R2 = 100;      // ESP32-S2 built in voltage divider 4700k // 470k?
const int R3 = 1;         // 8266 Add 1.2M resistor in series with A0 for voltages > 3.3V (Use 1 for ESP32)
float V_REF_CAL = 1.0; // Calculated battery voltage multiplication factor: 1.017 ESP32-S2 tank-2
const float V_MIN = 4.5;    // Minimum voltage for slowing down readings to conserve battery.
const float V_THRESH = 4.5; // Minimum voltage to measure

// Declare functions
void ledBlink(short numTimes = 1, short delayTime = 200);
void mqttSetCallback(std::string topicSub);
void mqttSetCallbackAll(std::string topicSub);

// Set a map for each item to later send to mqtt
// Can also be used for debugging via mqtt
std::map<std::string, std::string> sensor_values;

void setup() {
  #ifdef MY_DEBUG
  // Get monitor speed from platformio.ini
  Serial.begin(MONITOR_SPEED);
  Serial.setDebugOutput(false);
  while (!Serial) {
    Serial.print(":");
    delay(10);
  }
  Serial.printf("\n\nSketch: %s\n", (fwName + "_" + revName).c_str());
  Serial.printf("-> setup serial millis: %lu ms\n", millis());
  #endif

  pinMode (ledPin, OUTPUT);
  ledBlink();

  #ifdef ESP32
  //set the resolution to ESP32 12/13 bits (0-4095)
  analogReadResolution(13);
  #endif
  //Serial.printf("Initial battery read.\n"); //debug
  analogRead(BAT_SENSE_PIN);  // discard first battery readings to allow ADC to settle
  delay(100);  // Wait for read

  #ifdef MY_DEBUG
  Serial.printf("-> Booted millis: %lu ms\n", millis());
  #endif
}

// Only loops if a sleep mode is not used (i.e. timed loops)
void loop() {
  bool reboot = false;      // Reboot flag for updates
  sensor_values.clear();    // Clear the map, needed when looping

  // Read the system voltage
  int adcRaw = analogRead(BAT_SENSE_PIN);
  delay(100);  // Wait for read
  #ifdef ESP32
  int adcMilliVolts = analogReadMilliVolts(BAT_SENSE_PIN);
  float volts = adcMilliVolts * (R1 + R2) / R2;
  volts = volts / 1000 * V_REF_CAL;
  Serial.printf("\nADC raw: %d, ADC mV: %d, Calc. volts: %f\n",adcRaw, adcMilliVolts, volts);
  #elif defined(ESP8266)
  float v_cal = float(adcRaw) / 1024;
  float volts = (v_cal * (R1 + R2 + R3)) / R2;
  volts = volts * V_REF_CAL;
  Serial.printf("ADC raw: %d, ADC V: %f, Calc. volts: %f\n", adcRaw, v_cal, volts);
  #endif

  std::string volts_str = std::to_string(volts + 0.005);  // Round to 2 decimals
  std::string volts_rnd = volts_str.substr(0, volts_str.find(".")+3);
  if (volts > V_THRESH) {
    sensor_values.insert({"voltage", volts_rnd});
    if (volts < V_MIN) {
      sleepTime *= 5;   // Increase to conserve battery
    }
  }

  using namespace mqtt_wifi;
    if (sleepIsOn) {
      Serial.printf("-> mqttConnect start millis: %lu ms\n", millis());
    }
    if (mqttConnect(loggerName)) {
      if (sleepIsOn) {
        Serial.printf("-> mqttConnect end millis: %lu ms\n", millis());
      }
      std::string timeNow = timeToString();
      int bc = rtcData.bootCount;
      // Check if first boot or loop
      if (loopCount == 1 && (bc == 1)) {
        // MQTT subscribe callback, only once per booted session
        mqttSetCallback(mqttTopic + "set/+");
        sensor_values.insert({"ip", WiFi.localIP().toString().c_str()});
        sensor_values.insert({"fw_name", fwName});
        sensor_values.insert({"fw_version", revName});
        sensor_values.insert({"first_seen", timeNow});
        sensor_values.insert({"boot_count", std::to_string(bc)});
        sensor_values.insert({"sleep_on", std::to_string(sleepIsOn)});
      } else {
        // Check subscriptions
        mqttClient.update();
        sensor_values.insert({"rssi", std::to_string(WiFi.RSSI())});
        sensor_values.insert({"last_seen", timeNow});
        if (sleepIsOn) {
        sensor_values.insert({"boot_count", std::to_string(bc)});
        sensor_values.insert({"sleep_time", std::to_string(sleepTime)});
        } else {
        sensor_values.insert({"loop_count", String(loopCount).c_str()});
        sensor_values.insert({"loop_time", String(loopTime).c_str()});
        }
      }

      // Check for OTA firmware update, every x cycles
      if (loopCount % 2 == 0 || (bc % 2 == 0)) {
        //Serial.printf("-> Start OTA update check millis: %lu ms\n", millis());
        short updateStatus = otaUpdate(updateString);
        if (updateStatus == 0) {
          sensor_values.insert({"fw_updated", timeToString()});
          reboot = true;
        } else {
          sensor_values.insert({"fw_status", std::to_string(updateStatus)});
        }
        //Serial.printf("-> OTA update check complete millis: %lu ms\n", millis());
      }

      // Blink LED every x loop cycles
      if (loopCount % 10 == 0) ledBlink(2);
      loopCount++;

      // Publish MQTT messages to topics
      #define MSG_BUFFER_SIZE (50)
      static char topic[MSG_BUFFER_SIZE];
      static char msg[MSG_BUFFER_SIZE];
      for (auto [key, value] : sensor_values) {
        snprintf (topic, sizeof(topic), "%s%s", mqttTopic.c_str(), key.c_str());
        snprintf (msg, sizeof(msg), "%s", value.c_str());
        #ifdef MY_DEBUG
        Serial.printf("Sending: %s/%s\n", topic, msg);
        #endif
        mqttClient.publish(topic, msg);
      }
      //mqtt_disconnect();
    } else {
      Serial.print("Not connected to MQTT server.\n");
      // Check for OTA update to potentially fix any MQTT issues
      if (otaUpdate(updateString) == 0) {
        Serial.printf("-> Found OTA update, millis: %lu ms\n", millis());
      }
      reboot = true;
    }
  // End using namespace mqtt_wifi

  if (reboot) {
    #ifdef MY_DEBUG
    Serial.printf("Rebooting now...\n\n");
    #endif
    ledBlink(3);
    delay(3000);
    ESP.restart();
  } else if (sleepIsOn) {
    //Configure the wake up source and sleep time
    digitalWrite (ledPin, LEDOFF);
    #ifdef MY_DEBUG
    Serial.printf("-> Sleep start millis: %lu ms\n", millis());
    Serial.printf("Sleeping for %d seconds\n\n", sleepTime);
    Serial.flush();
    #endif
    #ifdef ESP32
    esp_sleep_enable_timer_wakeup(sleepTime * 1e6);   // uS For light and deep sleep options
    esp_deep_sleep_start();  // Deep sleep
    #elif defined(ESP8266)
    ESP.deepSleep(sleepTime * 1e6, WAKE_RF_DISABLED);
    #endif
  } else {
    // Delay loop
    //Serial.printf("-> Delay start millis: %lu ms\n", millis());
    delay(loopTime * 1e3);   // Delay option, no sleep
  }
}

void ledBlink(short numTimes, short delayTime) {
  for (int i = 0; i < numTimes; i++) {
    digitalWrite (ledPin, !LEDOFF);
    delay(delayTime);
    digitalWrite (ledPin, LEDOFF);
    delay(delayTime);
  }
}

#define mqtt_firmware_update "tester/test-1/set/fw_update"
// callback subscribes to specified topic
void mqttSetCallback(std::string topicSub) {
  Serial.printf("Callback set for %s\n", topicSub.c_str());
  mqtt_wifi::mqttClient.subscribe(String(topicSub.c_str()), [&](const String& payload, const size_t size) {
    Serial.printf("\n>>Message to topic %s: ", topicSub.c_str());
    Serial.println(payload);
    //mqtt_wifi::rtcData.sleepIsOn = payload.toInt();
  });
  mqtt_wifi::mqttClient.subscribe([](const String& topic, const String& payload, const size_t size) {
    Serial.println(">> MQTT received: " + topic + " = " + payload);
    if (topic == mqtt_firmware_update && payload.toInt() == 1) {
    Serial.println(">> Received Firmware update request.");
  }
  });
}
