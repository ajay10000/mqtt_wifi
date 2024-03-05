// Test file for mqtt/wifi.  Measures voltage on ADC pin.

#define OTAACTIVE

#include <mqtt_wifi_ota.h>  // Private library
#include <mqtt_globals.h>  // Private library

// Edit user variables below as required
#define MY_DEBUG                // Enable debug to serial monitor
const String update_path = "/firmware/";    // Path on webserver to firmware update
const String branch_name = "MQTT_WIFI_OTA_TEST";  // Used for binary file name on webserver
const std::string rev_name = "MQTT_WiFi_OTA_TEST_v1k";  // Full name including revision
const std::string logger_name = "tester-1"; // Unique logger/MQTT client ID
const std::string mqtt_topic_prefix = "tester/";
const std::string mqtt_topic = mqtt_topic_prefix + logger_name + "/";
const unsigned int sleeptime = 15;      // Loop/sleep delay in seconds
time_t loopTime = 30;     // Initialise loop timer to 30 seconds
time_t loopInterval = 30; // interval (sec) for system battery measurements
time_t updateTime = time(0) + 60; // Initialise update timer to now + 60 seconds
time_t updateInterval = 60; // Interval (sec) before next firmware check
// Battery voltage measurement
const int BAT_SENSE_PIN = 8; // Analog input for battery sense. A0 for 8266. GPIO8? for ESP32
const int R1 = 470;       // kohm - 8266 Built in voltage divider 100k // 220k
const int R2 = 4700;      // ESP32-S2 built in voltage divider 470k // 4700k Measured 433k // 2390k ??
const int R3 = 1;         // 8266 Add 1.2M resistor in series with A0 for voltages > 3.3V (Use 1 for ESP32)
float V_REF_CAL = 1.0; // Calculated battery voltage multiplication factor: 1.017 ESP32-S2 tank-2
// Fix type conversion issue using ADC_WIDTH_BIT_DEFAULT in ESP32 calibration code
#undef ADC_WIDTH_BIT_DEFAULT
#define ADC_WIDTH_BIT_DEFAULT   ((adc_bits_width_t) ((int)ADC_WIDTH_MAX-1))

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    Serial.print(".");
    delay(100);
  }

  Serial.printf("\n\nSketch: %s\n", rev_name.c_str());
  mqtt_wifi::wifi_connect();
  mqtt_wifi::mqtt_init();

  #ifdef MY_DEBUG
    Serial.print("Booted.\n");
  #endif
}

// uint32_t getADC_Cal(int ADC_Raw) {
// #ifdef ESP32
//   esp_adc_cal_characteristics_t adc_chars;
//   esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_0, ADC_WIDTH_BIT_DEFAULT, 1100, &adc_chars);
//   return(esp_adc_cal_raw_to_voltage(ADC_Raw, &adc_chars));
// #elif defined(ESP8266)
//   return ADC_Raw * 1000 / 1023;
// #endif
// }

void loop() {
#if defined(OTAACTIVE)
  // Firmware update, wait for interval before checking
  if (updateTime <= time(0)) {
    updateTime = time(0) + updateInterval;
    const String update_string = update_path + branch_name + ".bin";
    if (mqtt_wifi::otaUpdate(update_string) == true) {
      Serial.printf("Rebooting now...\n\n");
      delay(3000);
      ESP.restart();
    }
  }
#endif

  // Set a map for each item to later send to mqtt
  std::map<std::string, float> sensor_values;

  // Read the system voltage every loopInterval seconds
  if (loopTime <= time(0)) {
    loopTime = time(0) + loopInterval;
    double ADC_VALUE = analogRead(BAT_SENSE_PIN);
    delay(100);  // Wait for read
    float v_cal = ADC_VALUE * 1000 / 1023;    //getADC_Cal(ADC_VALUE);
    float v_calc = v_cal / ((float)R1 / (R3 + R2 + R1)) / 1000;
    v_calc = v_calc * V_REF_CAL;
    #ifdef MY_DEBUG
      Serial.printf("\nADC read: %.2f, Calibrated voltage: %.2f, Calc. voltage on pin: %.2f\n",ADC_VALUE, v_cal, v_calc);
    #endif
    if (ADC_VALUE != 0) {
      sensor_values.insert({"voltage", v_calc});
    }
  }

  for (auto [key, value] : sensor_values) {
    #ifdef OLED
      // Send data to OLED buffer, display after loop
      ;display.printf("%s: %.2f\n", key.c_str(), value);
    #endif

    // Send MQTT
    // Check if connected to the MQTT server
    using namespace mqtt_wifi;
      if (!mqtt_client.isConnected()) {
        Serial.printf("First attempt to connect...\n");
        mqtt_connect();
      }
      //Serial.printf("Should be still connected...\n");
      if (mqtt_client.isConnected()) {
        mqtt_client.update();
        snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), key.c_str());
        snprintf (msg, sizeof(msg), "%.2f", value);
        Serial.printf("Sending: %s to topic %s... \n", msg, topic);
        mqtt_client.publish(topic, msg);
      } else {
        Serial.printf("Not connected to MQTTT server. (%s)\n", topic);
      }
      // debug Serial.printf("Sent.\n");
  }
  #ifdef OLED
    // Display data on OLED screen
    ;display.display();
  #endif
  //Serial.printf("Wait for %d seconds\n\n", sleeptime);
  delay(sleeptime * 1000);
}
