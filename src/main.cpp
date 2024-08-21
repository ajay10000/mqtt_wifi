// Test file for mqtt/wifi.  Measures voltage on ADC pin.
// and sends the data to a mqtt server.

#include <mqtt_wifi_ota.h>  // Private library
#include <mqtt_globals.h>  // Private library

// Edit user variables below as required
#define MY_DEBUG                // Enable debug to serial monitor
// MQTT, WiFi & OTA Updates managed by mqtt_wifi_ota.h library with these variables
const std::string update_path = "/firmware/";    // Path on webserver to firmware update
const std::string fw_name = "test_mqtt_ota_test1";  // Firmware file name on webserver
const std::string rev_name = "test_mqtt_ota_v2h";  // Full name including revision
const std::string logger_name = "test-1"; // Unique logger/MQTT client ID
const std::string mqtt_topic_prefix = "utilities/";   // MQTT location/path. Leave empty for no path
const std::string mqtt_topic = mqtt_topic_prefix + logger_name + "/";

unsigned short sleeptime = 15;      // Loop/sleep delay in seconds
// Loop timers - adjust low when using sleep mode
time_t loopTime = 5;     // Initialise loop timer (s)
time_t loopInterval = 0; // interval (sec) for system battery measurements
time_t updateTime = time(0) + 5; // Initialise update timer to now + xx seconds
time_t updateInterval = 1 * loopTime; // Interval (sec) before next firmware check
const short ledPin = 8;     // ESP32 p18 LED_BUILTIN, ESP07/12 module is p2, ESP32-S2 p15
const bool LEDOFF = LOW;  // NodeMCU LED inverted (HIGH)

// Battery voltage measurement
const short BAT_SENSE_PIN = 8; // Analog input for battery sense. A0 for 8266. GPIO8 for ESP32
const int R1 = 470;       // kohm - 8266 Built in voltage divider 100k // 220k
const int R2 = 4700;      // ESP32-S2 built in voltage divider 470k // 4700k Measured 433k // 2390k ??
const int R3 = 1;         // 8266 Add 1.2M resistor in series with A0 for voltages > 3.3V (Use 1 for ESP32)
float V_REF_CAL = 1.0; // Calculated battery voltage multiplication factor: 1.017 ESP32-S2 tank-2
const float V_MIN = 11.8;    // Minimum voltage for slowing down readings to conserve battery.
const float V_THRESH = 4.5; // Minimum voltage to measure

// Declare functions
void ledBlink(short numTimes, short delayTime);

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
  Serial.printf("\n\nSketch: %s\n", rev_name.c_str());
  Serial.printf("-> setup serial millis: %lu ms\n", millis());
  #endif

  pinMode (ledPin, OUTPUT);
  ledBlink(2, 200);   //pin, delay time in mS

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
  // read the analog / millivolts value for adc pin as per
  // https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/AnalogRead/AnalogRead.ino
  int adcRaw = analogRead(BAT_SENSE_PIN);
  delay(100);  // Wait for read
  #ifdef ESP32
  int adcMilliVolts = analogReadMilliVolts(BAT_SENSE_PIN);
  float volts = adcMilliVolts * (R1 + R2) / R2;
  volts = volts / 1000 * V_REF_CAL;
  Serial.printf("ADC raw: %d, ADC mV: %d, Calc. volts: %f\n",adcRaw, adcMilliVolts, volts);
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
      sleeptime *= 5;   // Increase to conserve battery
    }
  }
  // Add sleep time to MQTT messages map
  sensor_values.insert({"sleep_time", std::to_string(sleeptime)});

  using namespace mqtt_wifi;
    Serial.printf("-> mqtt_init start millis: %lu ms\n", millis());
    if (mqtt_init()) {
    Serial.printf("-> mqtt_init end millis: %lu ms\n", millis());
    // Check if connected to the MQTT server
    //if (!mqtt_client.isConnected()) {
       //Serial.printf("First attempt to connect...\n"); //debug
       //mqtt_connect();
    //}
    //Serial.printf("Should be still connected...\n");  //debug
    //if (mqtt_connect()) {      //if (mqtt_client.isConnected()) {
      //Serial.printf("-> mqtt_connect end millis: %lu ms\n", millis());
      //  mqtt_client.update();

      unsigned long bootCount = rtcData.boot_count;
      sensor_values.insert({"boot_count", std::to_string(bootCount)});
      std::string timeNow = timeToString();
      if (bootCount == 1) {
        sensor_values.insert({"fw_name", fw_name});
        sensor_values.insert({"fw_version", rev_name});
        sensor_values.insert({"first_seen", timeNow});
      } else {
        sensor_values.insert({"last_seen", timeNow});
      }

      // Check for OTA firmware update, every 2 cycles
      if (bootCount % 2 == 0) {
        Serial.printf("-> Start OTA update check millis: %lu ms\n", millis());
        std::string update_string = update_path + fw_name;
        short update_status = mqtt_wifi::otaUpdate(update_string);
        if (update_status == 0) {
          sensor_values.insert({"fw_updated", timeToString()});
          reboot = true;
        } else {
          sensor_values.insert({"fw_status", std::to_string(update_status)});
        }
        Serial.printf("-> OTA update check complete millis: %lu ms\n", millis());
      }

      // Publish MQTT messages to topics
      for (auto [key, value] : sensor_values) {
        snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), key.c_str());
        snprintf (msg, sizeof(msg), "%s", value.c_str());
        #ifdef MY_DEBUG
        Serial.printf("Sending: %s/%s\n", topic, msg);
        #endif
        mqtt_client.publish(topic, msg);
        //Serial.printf("Sent.\n");  //debug
      }
      //mqtt_disconnect();
    } else {
      Serial.printf("Not connected to MQTT server. (%s)\n", topic);  //debug
      // Check for OTA update to potentially fix any MQTT issues
      Serial.printf("-> Start OTA update check millis: %lu ms\n", millis());
      std::string update_string = update_path + fw_name;
      if (mqtt_wifi::otaUpdate(update_string) == 0) {
        Serial.printf("-> Found OTA update, millis: %lu ms\n", millis());
      }
      Serial.printf("-> OTA update check complete millis: %lu ms\n", millis());
      reboot = true;
    }
  // End using namespace mqtt_wifi

  if (reboot == true) {
    #ifdef MY_DEBUG
    Serial.printf("Rebooting now...\n\n");
    delay(3000);
    #endif
    ESP.restart();
  } else {
    //Configure the wake up source and sleep time
    digitalWrite (ledPin, LEDOFF);
    #ifdef MY_DEBUG
    Serial.printf("-> Sleep start millis: %lu ms\n", millis());
    Serial.printf("Sleeping for %d seconds\n\n", sleeptime);
    Serial.flush();
    #endif
    #ifdef ESP32
    esp_sleep_enable_timer_wakeup(sleeptime * 1e6);   // uS For light and deep sleep options
    esp_deep_sleep_start();  // Deep sleep
    #elif defined(ESP8266)
    ESP.deepSleep(sleeptime * 1e6, WAKE_RF_DISABLED);
    #endif
    //delay(sleeptime * 1000);   // Delay option, no sleep
  }
}

void ledBlink(short numTimes, short delayTime) {
  long lastMillis = millis();
  digitalWrite (ledPin, !LEDOFF);
  for (short i = 0; i >= numTimes;) {
    if (millis() - lastMillis >= delayTime) {
      lastMillis = millis();
      digitalWrite (ledPin, !digitalRead(ledPin));
      i++;
    }
  }
}
