#pragma once
// Library for WiFi and MQTT connections with OTA updates

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPUpdate.h>
#elif defined(ESP8266)
 #include <ESP8266WiFi.h>
 #include <ESP8266httpUpdate.h>
// #include <umm_malloc/umm_heap_select.h>
#endif
#include <WiFiClientSecure.h>
#include <MQTTPubSubClient.h>
#include <time.h>
#include <mqtt_globals.h>  // Private library
#include <credentials.h>  // Private library
// ssid, password and CA cert stored in credentials.h

#define MY_DEBUG                // Enable debug to serial monitor

namespace mqtt_wifi {
  const String branch_rev_name = "MQTT_WiFi_OTA_v1f";
  // NTP Time variables
  const char* ntpServer = "ntp.openwrt.ferndale";   // Local router is set as ntp server
  const long  gmtOffset_sec = 11 * 3600;        // Region offset to GMT
  const int   daylightOffset_sec = 3600;        // Daylight saving offset
  // MQTT variables
  const char* mqtt_server = "rpi4-2.ferndale"; // MQTT server
  const int mqtt_port = 8883;                  // MQTT port
  #define MSG_BUFFER_SIZE (50)
  char topic[MSG_BUFFER_SIZE];
  char msg[MSG_BUFFER_SIZE];
  // OTA variables
  const String updateServer = "rpi4-2.ferndale";
  const int updateServerPort = 8084;  // Using weewx server from docker

  WiFiClientSecure mqtt_wifi;
  MQTTPubSubClient mqtt_client;

  std::string timeToString(long timestamp = -1) {
      time_t rawtime;
      if (timestamp < 0) {
        rawtime = time(nullptr);    // Default parameter:- get current time
      } else {
        rawtime = static_cast<time_t>(timestamp); // Otherwise convert the given argument
      }
      struct tm* dt = localtime(&rawtime);
      char timestr[30];
      strftime(timestr, sizeof(timestr), "%Y-%m-%dT%X", dt);
      return std::string(timestr);
  }

  // Set time via NTP
  time_t setClock() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.printf("NTP time sync... ");
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2) {
      yield();
      delay(500);
      Serial.printf(".");
      now = time(nullptr);
    }
    //Serial.printf(" ");
    Serial.printf(" %s\n", timeToString().c_str());
    return now;
  }

  void wifi_connect() {
    Serial.printf("Connecting to WiFi...");
    WiFi.mode( WIFI_STA );
    WiFi.begin( ssid, password );
    int retries = 0;
    while ((WiFi.status() != WL_CONNECTED) && (retries < 5)) {
      retries++;
      delay(3000);
      Serial.printf(".");
    }
    #ifdef MY_DEBUG
      if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("\nWiFi Connected, IP address: %s, ", WiFi.localIP().toString().c_str());
          Serial.printf("RRSI: %i.\n", WiFi.RSSI());
      } else {
          Serial.printf(" WiFi connection FAILED.\n");
      }
    #endif
    // Set the RTC clock
    setClock();
  }

  void wifi_disconnect() {
    WiFi.disconnect(true);
    delay(1);
    WiFi.mode(WIFI_OFF);
  }

  void mqtt_setcallback() {
    // callback subscribes to every MQTT packet received
    //mqtt_client.subscribe([](const String& topic, const String& payload, const size_t size) {
    //    Serial.println("mqtt received: " + topic + " - " + payload);
    //});

    // callback subscribes to mqtt_topic only
    mqtt_client.subscribe(mqtt_topic.c_str(), [](const String& payload, const size_t size) {
      #ifdef MY_DEBUG
        Serial.printf("\nMessage to: %s = ", mqtt_topic);
        Serial.println(payload);
      #endif

      // Flash the LED if message received.
      // unsigned int i = 0;
      // while (i < 5) {
      //   digitalWrite(ledPin, HIGH);
      //   delay(500);
      //   digitalWrite(ledPin, LOW);
      //   delay(250);
      //   i++;
      // }
    });
  }

  void mqtt_init() {
    #ifdef ESP32
      mqtt_wifi.setCACert(root_ca_cert); // for CA certificate verification
    #elif defined(ESP8266)
      mqtt_wifi.setFingerprint(fingerprint);  // server cert fingerprint
      Serial.printf("ESP8266: Fingerprint set.\n");
    #endif
    Serial.printf("Connecting to MQTT server...");
    unsigned int i = 0;
    while (not mqtt_wifi.connected() && (i < 5)) {
      mqtt_wifi.connect(mqtt_server, mqtt_port);
      Serial.printf(".");
      delay(1000);
      i++;
    }
    if (mqtt_wifi.connected()) {
      Serial.printf(" Connected.\n");
      // Debugging for error: [WiFiGeneric.cpp:1230] hostByName(): DNS Failed for...
      try {
        mqtt_client.begin(mqtt_wifi);
      } catch (...) {
        delay(100);
        ESP.restart();
      }
      // MQTT callback init for subscribe
      mqtt_setcallback();
    } else {
      Serial.printf(" Cannot connect.\n");
    }
  }

  // This function connects to the MQTT server
  void mqtt_connect() {
    if (WiFi.status() != WL_CONNECTED) {
      wifi_connect();
    }
    // Loop until we're connected, max. 5 times
    unsigned int i = 0;
    Serial.printf("Connecting MQTT client...");
    while ((!mqtt_client.isConnected()) && (i < 5)) {
      if (mqtt_client.connect(logger_name.c_str())) {
        #ifdef MY_DEBUG
          Serial.printf("\nClient %s is connected to %s.\n",logger_name.c_str(), mqtt_server);
          //Serial.printf("Client state: %s \n",String(mqtt_client.state()));
        #endif
        // Print connected message, etc.
        snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), "ip");
        snprintf (msg, sizeof(msg), WiFi.localIP().toString().c_str());
        mqtt_client.publish(topic, msg);
        snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), "firmware");
        snprintf (msg, sizeof(msg), rev_name.c_str());
        mqtt_client.publish(topic, msg);
      } else {
        if (i == 4) {
          // Only print it once
          #ifdef MY_DEBUG
            Serial.printf("Could not connect to MQTT server, rc= %s", mqtt_client.getLastError());
          #endif
          char lastError[100];
          mqtt_wifi.lastError(lastError,100);  //Get the last error for WiFiClientSecure
          //mqtt_wifi.getLastSSLError(lastError,100);   //char *dest = NULL, size_t len = 0)
          Serial.printf("WiFiClientSecure client state: %s \n",lastError);
        } else {
          #ifdef MY_DEBUG
            Serial.printf(".");
          #endif
        }
        Serial.printf(".");
        delay(1000);
        i++;
      }
    }
  }

  void mqtt_wifi_disconnect() {
    mqtt_client.disconnect();   // Close MQTT connection
    wifi_disconnect();        // Shut down wifi
  }

  boolean otaUpdate(const String fileName) {
    Serial.printf("\nChecking for updates to %s...\n", fileName.c_str());
    // Wait for serial buffer to empty
    for (uint8_t t = 4; t > 0; t--) {
      //Serial.printf("[SETUP] WAIT %d...\n", t);
      Serial.flush();
      delay(500);
    }

    WiFiClient client;

#if defined(ESP32)
    httpUpdate.rebootOnUpdate(false); // remove automatic update
    t_httpUpdate_return ret = httpUpdate.update(client, updateServer, updateServerPort, fileName);
#elif defined(ESP8266)
    ESPhttpUpdate.rebootOnUpdate(false); // remove automatic update
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, updateServer, updateServerPort, fileName);
#endif
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        if (httpUpdate.getLastError() == -102)
        //if (ESPhttpUpdate.getLastError() == -102)
        {
          Serial.printf(" No updates\n");
        } else {
          Serial.printf("Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
          //Serial.printf("Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        }
        return false;

      case HTTP_UPDATE_OK:
        Serial.printf("Update uploaded ");
        Serial.printf("%s\n", timeToString().c_str());
        return true;
    }
    return 0;
  }

}