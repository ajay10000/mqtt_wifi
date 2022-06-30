#pragma once

// WiFi and MQTT connections

#ifdef ESP32
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <WiFiClientSecure.h>
#include <MQTTPubSubClient.h>
//#include <iostream>
#include <mqtt_globals.h>  // Private library
#include <credentials.h>  // Private library

//const String rev_name = "MQTT_WiFi_v2d";
// ssid, password and CA cert stored in credentials.h

#define MY_DEBUG                // Enable debug to serial monitor

namespace mqtt_wifi {
  // MQTT variables
  const char* mqtt_server = "rpi4-2.ferndale"; // MQTT server
  const int mqtt_port = 8883;                  // MQTT port
  #define MSG_BUFFER_SIZE (50)
  char topic[MSG_BUFFER_SIZE];
  char msg[MSG_BUFFER_SIZE];

  WiFiClientSecure mqtt_wifi;
  MQTTPubSubClient mqtt_client;

  void wifi_connect() {
    Serial.printf("\nConnecting to WiFi");
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
          Serial.printf(" WiFi Connected, IP address: %s, ", WiFi.localIP().toString().c_str());
          Serial.printf("RRSI: %i.\n", WiFi.RSSI());
      } else {
          Serial.printf(" WiFi connection FAILED.\n");
      }
    #endif
  }

  void wifi_disconnect() {
    WiFi.disconnect(true);
    delay(1);
    WiFi.mode(WIFI_OFF);
  }

  void mqtt_init() {
    // MQTT server/broker init and callback for subscribe only
    //mqtt_client.setCallback(callback);
    #ifdef ESP32
      mqtt_wifi.setCACert(root_ca_cert); // for CA certificate verification
    #elif defined(ESP8266)
      mqtt_wifi.setFingerprint(fingerprint);  // server cert fingerprint
      Serial.printf("ESP8266: Fingerprint set.\n");
    #endif
    Serial.printf("Initialising connection to server");
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
    Serial.printf("Connecting to MQTT server: ");
    while ((!mqtt_client.isConnected()) && (i < 5)) {
      if (mqtt_client.connect(logger_name.c_str())) {
        #ifdef MY_DEBUG
          Serial.printf("Client %s is connected to %s.\n",logger_name.c_str(), mqtt_server);
          //Serial.printf("Client state: %s \n",String(mqtt_client.state()));
        #endif
        // Print connected message, etc.
        //snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), "client");
        //snprintf (msg, sizeof(msg), "Connected");
        //mqtt_client.publish(topic, msg);
      } else {
        if (i == 4) {
          // Only print it once
          #ifdef MY_DEBUG
            Serial.printf("Could not connect.\n");  //, rc= %s", mqtt_client.state());
          #endif
          //char lastError[100];
          //mqtt_wifi.lastError(lastError,100);  //Get the last error for WiFiClientSecure
          //Serial.printf("WiFiClientSecure client state: %s",lastError);
        } else {
          #ifdef MY_DEBUG
            Serial.printf(".");
          #endif
        }
        delay(1000);
        i++;
      }
    }
  }

void mqtt_wifi_disconnect() {
  mqtt_client.disconnect();   // Close MQTT connection
  wifi_disconnect();        // Shut down wifi
}


  void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    #ifdef MY_DEBUG
      Serial.printf("\n");
      Serial.printf("Message in: %s = ", topic);
      for (unsigned int i = 0; i < length; i++) {
        Serial.printf("%c", (char)payload[i]);
      }
    #endif
  }
}