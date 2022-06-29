// Test file for mqtt/wifi.  Measures voltage on ADC pin.

#include <mqtt_wifi.h>  // Private library
#include <mqtt_globals.h>  // Private library

// Edit user variables below as required
#define MY_DEBUG                // Enable debug to serial monitor
const std::string logger_name = "test-1"; // Unique logger/MQTT client ID
const std::string mqtt_topic_prefix = "utilities/";
const std::string mqtt_topic = mqtt_topic_prefix + logger_name + "/";
const unsigned int sleeptime = 15;      // Loop/sleep delay in seconds
time_t loopTime = 30;     // Initialise loop timer for 30 seconds
time_t loopInterval = 30; // interval in seconds for system battery measurements
// Battery voltage measurement
const int BAT_SENSE_PIN = A0; // Analog input for battery sense. A0 for 8266. GPIO8 for ESP32
const int R1 = 100;       // kohm - 8266 Built in voltage divider 100k // 220k
const int R2 = 220;      // ESP32-S2 built in voltage divider 470k // 4700k Measured 433k // 2390k ??
const int R3 = 1200;         // 8266 Add 1.2M resistor in series with A0 for voltages > 3.3V (Use 1 for ESP32)
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
  mqtt_wifi::connectToWiFi();
  mqtt_wifi::initMqtt();

  #ifdef MY_DEBUG
    Serial.print("Booted\n");
  #endif
}

uint32_t getADC_Cal(int ADC_Raw) {
#ifdef ESP32
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_0, ADC_WIDTH_BIT_DEFAULT, 1100, &adc_chars);
  return(esp_adc_cal_raw_to_voltage(ADC_Raw, &adc_chars));
#elif defined(ESP8266)
  return ADC_Raw * 1000 / 1023;
#endif
}

void loop() {
  // Set a map for each item to later send to mqtt
  std::map<std::string, float> sensor_values;

  // Read the system voltage every loopInterval seconds
  if (loopTime <= time(0)) {
    loopTime = time(0) + loopInterval;
    double ADC_VALUE = analogRead(BAT_SENSE_PIN);
    delay(100);  // Wait for read
    float v_cal = getADC_Cal(ADC_VALUE);
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
        mqttConnect();
      }
      if (mqtt_client.isConnected()) {
        mqtt_client.update();
        snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), key.c_str());
        snprintf (msg, sizeof(msg), "%.2f", value);
        Serial.printf("Sending: %s to topic %s... \n", msg, topic);
        mqtt_client.publish(topic, msg);
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
