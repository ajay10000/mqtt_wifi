// WiFi and MQTT connections

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTTPubSubClient.h>
#include <mqtt_globals.h>  // Private library
#include <credentials.h>  // Private library

// rev_name = "MQTT_WiFi_v1d";
// ssid, password and CA cert stored in credentials.h

#define MY_DEBUG                // Enable debug to serial monitor
// MQTT variables
const char* mqtt_server = "rpi4-2.ferndale"; // MQTT server
const int mqtt_port = 8883;                  // MQTT port
#define MSG_BUFFER_SIZE (50)
char topic[MSG_BUFFER_SIZE];
char msg[MSG_BUFFER_SIZE];

WiFiClientSecure mqtt_wifi;
MQTTPubSubClient mqtt_client;

void connectToWiFi() {
  //Serial.printf("\n");
  //Serial.printf("Connecting to WiFi");
  WiFi.mode( WIFI_STA );
  WiFi.begin( ssid, password );
  int retries = 0;
  while ((WiFi.status() != WL_CONNECTED) && (retries < 5)) {
    retries++;
    delay(3000);
    //Serial.printf(".");
  }
  #ifdef MY_DEBUG
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi Connected, IP address: %s, ", WiFi.localIP().toString());
        Serial.printf("RRSI: %i.\n", WiFi.RSSI());
    } else {
        Serial.printf("WiFi connection FAILED.\n");
    }
  #endif
}

void initMqtt() {
  // MQTT server/broker init and callback for subscribe only
  //  mqtt_client.setCallback(callback);
  mqtt_wifi.setCACert(root_ca_cert); // for CA certificate verification
  Serial.printf("WiFi connecting to MQTT server");
  
  int i = 0;
  while (not mqtt_wifi.connected() && (i < 5)) {
    mqtt_wifi.connect(mqtt_server, mqtt_port);
    Serial.printf(".");
    delay(1000);
    i++;
  }
  // Debugging for error: [WiFiGeneric.cpp:1230] hostByName(): DNS Failed for...
  try {
    mqtt_client.begin(mqtt_wifi);
  } catch (...) {
    delay(100);
    ESP.restart();
  }
}

// This function connects to the MQTT server
void MqttConnect() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  } 
  
  // Loop until we're connected, max. 5 times
  int i = 0;
  while ((!mqtt_client.isConnected()) && (i < 5)) {
    if (mqtt_client.connect(logger_name.c_str())) {
      #ifdef MY_DEBUG
        Serial.printf("MQTT connected.\n");
        Serial.printf("Client: %s, ",logger_name.c_str());
        Serial.printf("MQTT server: %s\n",mqtt_server);
        //Serial.printf("Client state: %s \n",String(mqtt_client.state()));
      #endif
      //snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), "client");
      //snprintf (msg, sizeof(msg), "Connected");
      //mqtt_client.publish(topic, msg);
    } else {
      if (i == 4) {
        // Only print it once
        #ifdef MY_DEBUG
          Serial.printf("MQTT not connected.\n");  //, rc= %s", mqtt_client.state());
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

void callback(char* topic, byte* payload, unsigned int length) {
  #ifdef MY_DEBUG
    Serial.printf("\n");  
    Serial.printf("Message in: %s = ", topic);
    for (int i = 0; i < length; i++) {
      Serial.printf("%c", (char)payload[i]);
    }
  #endif
}
