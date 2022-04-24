// WiFi and MQTT connections

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTTPubSubClient.h>
#include <mqtt_globals.h>  // Private library
#include <credentials.h>  // Private library

// rev_name = "MQTT_WiFi_v1a";
// ssid and password stored in credentials.h

// MQTT variables
const char* mqtt_server = "rpi4-2.ferndale"; // MQTT Broker
const int mqtt_port = 8883;                  // MQTT port
#define MSG_BUFFER_SIZE (50)
char topic[MSG_BUFFER_SIZE];
char msg[MSG_BUFFER_SIZE];

WiFiClientSecure wifi;
MQTTPubSubClient mqtt_client;

void connectToWiFi() {
  Serial.println("");
  Serial.print("Connecting to WiFi.");
  WiFi.mode( WIFI_STA );
  WiFi.begin( ssid, password );
  int retries = 0;
  while ((WiFi.status() != WL_CONNECTED) && (retries < 5)) {
    retries++;
    delay(3000);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected.");
    #ifdef MY_DEBUG
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("RRSI: ");
      Serial.println(WiFi.RSSI());
    #endif
  } else {
      Serial.println(" Connection FAILED");
  }
}

// This function connects to the MQTT broker
void MqttConnect() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  } 
  
  // Loop until we're connected, max. 5 times
  int i = 0;
  while ((!mqtt_client.isConnected()) && (i < 5)) {
    if (mqtt_client.connect(logger_name.c_str())) {
      Serial.print("MQTT connected");
      snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), "client");
      snprintf (msg, sizeof(msg), "Connected");
      mqtt_client.publish(topic, msg);
    } else {
      if (i == 4) {
        // Only print it once
        Serial.printf("MQTT not connected");  //, rc= %s", mqtt_client.state());
        //char lastError[100];
        //wifi.lastError(lastError,100);  //Get the last error for WiFiClientSecure
        //Serial.printf("WiFiClientSecure client state: %s",lastError);
      } else {
        Serial.print(".");
      }
      delay(1000);
      i++;
    }
  }
  Serial.println(".");  // Just to finish off either case.  
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println();  
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
}

void setup() {
}

void loop() {
}