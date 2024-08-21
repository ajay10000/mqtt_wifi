#pragma once
// Library for WiFi and MQTT connections with OTA updates

#if defined(ESP32)
  #include <WiFi.h>
  //#include <NetworkClientSecure.h>    // Change in v3
  #include <WiFiClientSecure.h>
  #include <HTTPUpdate.h>
#elif defined(ESP8266)
 #include <ESP8266WiFi.h>
 #include <WiFiClientSecure.h>
 #include <ESP8266httpUpdate.h>
#endif
#include <Arduino.h>
#include <MQTTPubSubClient.h>
#include <time.h>
#include <mqtt_globals.h>  // Private library
#include <credentials.h>  // Private library
// ssid, password and CA cert stored in credentials.h

#define MY_DEBUG                // Enable debug to serial monitor

namespace mqtt_wifi {
  const String branch_rev_name = "MQTT_WiFi_OTA_v1w";
  // NTP Time variables
  const char* ntpServer = "ntp.openwrt.ferndale";   // Local router is set as ntp server
  // TZ string information: // https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
  //AEDT starts 02:00 1st Sunday Oct, ends 02:00 1st Sunday April
  const char* TZstr = "AEST-10AEDT,M10.1.0/2,M4.1.0/2";
  char timestr[30];                             // char buffer for date/time
  // MQTT variables
  const char* mqtt_server = "rpi4-2.ferndale";  // MQTT server
  const short mqtt_port = 8883;                 // MQTT port
  #define MSG_BUFFER_SIZE (50)
  char topic[MSG_BUFFER_SIZE];
  char msg[MSG_BUFFER_SIZE];
  // OTA variables
  const String updateServer = "update.ferndale";
  const short updateServerPort = 80;  // Caddy as http server from docker
  short delayTime = 0;
  long lastMillis = 0;

  // Structure to store connection information for ESP8266 'RTC' memory
  // The ESP8266 RTC memory is arranged into blocks of 4 bytes. The access methods read and write 4 bytes at a time,
  // so the RTC data structure should be a multiple of 4-byte.
  #ifdef ESP32
  typedef struct {
    uint16_t magic;     // 2 bytes
    uint8_t channel;  // 1 byte
    uint8_t bssid[6];// 6 bytes
    uint32_t gateway_ip; // Assume dns_ip is the same IPV4 IP#  4 bytes
    uint32_t my_ip;   // Device IP#                             4 bytes
    uint32_t subnet;  // Subnet mask -                          4 bytes
    uint16_t boot_count;  // 2 bytes
    uint8_t padding;      // 1 byte, 24 in total
  } rtcDataStruct;
  RTC_DATA_ATTR rtcDataStruct rtcData;
  #elif defined(ESP8266)
  const uint8_t RTCDATA_OFFSET = 120;       // Offset blocks (of 4 bytes), 127 is highest available (512 bytes)
                                            // 32 blocks will be lost after performing an OTA update
  struct {
    uint16_t magic;     // 2 bytes
    uint8_t channel;  // 1 byte
    uint8_t bssid[6];// 6 bytes
    uint32_t gateway_ip; // Assume dns_ip is the same IPV4 IP#  4 bytes
    uint32_t my_ip;   // Device IP#                             4 bytes
    uint32_t subnet;  // Subnet mask -                          4 bytes
    uint16_t boot_count;  // 2 bytes
    uint8_t padding;      // 1 byte, 24 in total
  } rtcData;
  #endif
  const uint16_t RTCDATA_MAGIC = 0xF3ED;    // Magic number to check valid RTC read. 62445 decimal

  // Declare functions
  void wifi_connect();
  void wifi_disconnect();
  void mqtt_setcallback() ;
  bool mqtt_init();
  bool mqtt_connect();
  void mqtt_disconnect();
  int8_t otaUpdate(std::string fileName);
  std::string timeToString(long timestamp);
  time_t setClock();

  WiFiClientSecure tls_client;
  MQTTPubSubClient mqtt_client;

  void wifi_connect() {
    #ifdef ESP32
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.printf("ResetInfo.reason = %s\n", String(reset_reason));
    //if (reset_reason == ESP_RST_DEEPSLEEP) {
    #elif defined (ESP8266)
    rst_info *resetInfo;
    resetInfo = ESP.getResetInfoPtr();
    Serial.println(String("ResetInfo.reason = ") + (*resetInfo).reason);
    if ((*resetInfo).reason == REASON_DEEP_SLEEP_AWAKE) {
      // Read WiFi settings from RTC memory.
      ESP.rtcUserMemoryRead(120, (uint32_t *)&rtcData, sizeof(rtcData));
    }
    #endif
    bool rtcValid = false;
    if( rtcData.magic == RTCDATA_MAGIC ) {
      rtcValid = true;
      Serial.printf("rtcData channel: %d, Gateway IP: ", rtcData.channel);
      Serial.print(IPAddress(rtcData.gateway_ip));
      Serial.println();
    }

    Serial.printf("-> WiFi start millis: %lu ms\n", millis());
    WiFi.mode (WIFI_STA);
    if( rtcValid ) {
      Serial.println("RTC OK, Try quick connection");
      // Bring up the WiFi connection
      WiFi.config( rtcData.my_ip, rtcData.gateway_ip, rtcData.subnet, rtcData.gateway_ip );   // ip, gateway, subnet, dns
      WiFi.begin( ssid, password, rtcData.channel, rtcData.bssid, true );
    } else {
      Serial.println("First boot or RTC invalid, Try regular connection.");
      WiFi.begin( ssid, password, 0, NULL, true );
    }
    Serial.printf("-> WiFi begin complete millis: %lu ms\n", millis());

    //------now wait for connection
    unsigned long check_time = millis() + 5000;
    bool fast_connect = true;
    while( WiFi.status() != WL_CONNECTED ) {
      yield();
      Serial.print(".");
      if( millis() > check_time && fast_connect == true) {
        Serial.printf("WiFi.status: %i\n", WiFi.status());
        Serial.println("WIFI not connected in 5s, trying standard connection.");
        WiFi.begin( ssid, password, 0, NULL, true );
        fast_connect = false;
      }
      if( millis() > (check_time + 5000) ) {
        Serial.println("WIFI not connected in 10s, try reboot");
        delay(3000);
        ESP.restart();
      }
      delay(20);
    }

    if (WiFi.status() == WL_CONNECTED) {
      #ifdef MY_DEBUG
      Serial.printf("\n-> WiFi connected millis: %lu ms\n", millis());
      Serial.printf("Channel: %i", WiFi.channel());
      Serial.printf(", WiFi.BSSID %s", WiFi.BSSIDstr().c_str());
      Serial.printf(", Gateway IP: %s", WiFi.gatewayIP().toString().c_str());
      Serial.printf(", IP address: %s", WiFi.localIP().toString().c_str());
      Serial.printf(", RRSI: %i\n\n", WiFi.RSSI());
      //WiFi.printDiag(Serial);   / debug
      #endif
      if (rtcValid == false) {
        // Boot from power up or rtc memory invalid
        // Write current connection info back to RTC
        rtcData.magic = RTCDATA_MAGIC;
        rtcData.channel = WiFi.channel();
        memcpy( rtcData.bssid, WiFi.BSSID(), 6 ); // Copy 6 bytes of BSSID (AP's MAC address)
        rtcData.gateway_ip = WiFi.gatewayIP();
        rtcData.my_ip = WiFi.localIP();
        rtcData.subnet = WiFi.subnetMask();
        rtcData.boot_count = 1;
        rtcData.padding = 1;  // not used
        // Disable WiFi persistence.  The ESP8266 will not load and save WiFi settings unnecessarily in the flash memory.
        WiFi.persistent(false);
        //WiFi.setAutoConnect(false);
        WiFi.setAutoReconnect(false);
      } else {
        // Just increment the boot count
        rtcData.boot_count++;
      }
      // ESP32 RTC update is automatic.
      #ifdef ESP8266
      if (ESP.rtcUserMemoryWrite(120, (uint32_t*)&rtcData, sizeof(rtcData))) {
        //Serial.printf("-> RTC write millis: %lu ms\n", millis());
      }
      #endif

      // Set the clock
      setClock();
      Serial.printf("-> Clock set millis: %lu ms\n", millis());

    } else {
      Serial.printf(" WiFi connection FAILED, restarting.\n");
      delay(3000);
      ESP.restart();
    }
  }

  void wifi_disconnect() {
    WiFi.disconnect(true);
    delay(1);
    WiFi.mode(WIFI_OFF);
  }

  void mqtt_setcallback() {
    // callback subscribes to mqtt_topic only
    mqtt_client.subscribe(mqtt_topic.c_str(), [](const String& payload, const size_t size) {
    #ifdef MY_DEBUG
      Serial.printf("\nMessage to: %s = ", mqtt_topic.c_str());
      Serial.println(payload);
    #endif
    });
  }

  bool mqtt_init() {
    wifi_connect();   // Connect to WiFi first
    // Set up a secure connection to the MQTT server
    #ifdef ESP32
      tls_client.setCACert(root_ca_cert); // for CA certificate verification
    #elif defined(ESP8266)
      tls_client.setFingerprint(fingerprint);  // server cert fingerprint
    #endif
    Serial.printf("Connecting to MQTT server");
    tls_client.stop();
    tls_client.connect(mqtt_server, mqtt_port, 10000);
    unsigned short i = 0;
    unsigned short delayTime = 50;
    unsigned long lastMillis = millis();
    // Try to connect for x * delayTime ms
    while (!tls_client.connected()) {
      if (i >= 100) break;   // ~5s give up
      if (millis() - lastMillis >= delayTime) {
        lastMillis = millis();
        Serial.print(",");
        i++;
      }
    }
    if (tls_client.connected()) {
      Serial.printf(" ->Connected.\n");
      //Serial.printf("-> tls_client connected millis: %lu ms\n", millis());
      // Debugging for error: [WiFiGeneric.cpp:1230] hostByName(): DNS Failed for...
      try {
        mqtt_client.begin(tls_client);
      } catch (...) {
        delay(5000);
        ESP.restart();
      }
      // MQTT callback init for subscribe
      mqtt_setcallback();
      if (mqtt_connect()) {
        return true;
      }
    } else {
      //Get the last error for WiFiClientSecure
      char buf[200];
      #ifdef ESP32
      int lastErr = tls_client.lastError(buf, sizeof(buf));
      #elif defined(ESP8266)
      int lastErr = tls_client.getLastSSLError(buf, sizeof(buf));
      #endif
      Serial.printf(" ->Cannot connect, tls_client error %i\n", lastErr);
      return false;
    }
    return false;
  }

  // Connect the client to the MQTT server
  bool mqtt_connect() {
    // Loop until we're connected, max. 5 times
    unsigned char i = 0;
    Serial.printf("Connecting MQTT client");
    mqtt_client.connect(logger_name.c_str());
    while ((!mqtt_client.isConnected()) && (i < 5)) {
      yield();
      if (i == 4) {
        // Only print it once
        #ifdef MY_DEBUG
        Serial.printf("Could not connect MQTT client, rc= %i", mqtt_client.getLastError());
        #endif
        delay(3000);
        ESP.restart();
      } else {
        // Still connecting
        Serial.print(".");
        delay(10);
      }
      i++;
    }
    #ifdef MY_DEBUG
      Serial.printf("\nClient %s is connected to %s.\n",logger_name.c_str(), mqtt_server);
    #endif
    // Print connected message, etc.
    snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), "ip");
    snprintf (msg, sizeof(msg), "%s", WiFi.localIP().toString().c_str());
    mqtt_client.publish(topic, msg);
    snprintf (topic, sizeof(topic), "%s%s", mqtt_topic.c_str(), "rssi");
    snprintf (msg, sizeof(msg), "%d", WiFi.RSSI());
    mqtt_client.publish(topic, msg);
    mqtt_client.update();
    return true;
  }

  void mqtt_disconnect() {
    mqtt_client.disconnect();   // Close MQTT connection
    wifi_disconnect();        // Shut down wifi
  }

  int8_t otaUpdate(std::string fileName) {
    // // Wait for serial buffer to empty
    // for (uint8_t t = 4; t > 0; t--) {
    //   //Serial.printf("[SETUP] WAIT %d...\n", t);
    //   Serial.flush();
    //   delay(100);
    // }
    WiFiClient wifi_client;
    // Not using https yet
    // WiFiClientSecure wifi_client;
    // #ifdef ESP32
    //   client.setCACert(root_ca_cert); // for CA certificate verification
    // #elif defined(ESP8266)
    //   wifi_client.setFingerprint(fingerprint);  // server cert fingerprint
    //   Serial.printf("ESP8266: Fingerprint set.\n");
    // #endif
    // unsigned int i = 0;
    // while (not wifi_client.connected() && (i < 10)) {
    //   Serial.printf(".");
    //   delay(200);
    //   i++;
    // }
    // if (wifi_client.connected()) {
    //   Serial.printf(" Secure client connected.\n");
    // } else {
    //   delay(5000);
    //   ESP.restart();
    // }
    int16_t err;
    String err_str;
#if defined(ESP32)
    httpUpdate.rebootOnUpdate(false); // remove automatic update
    t_httpUpdate_return ret = httpUpdate.update(wifi_client, updateServer, updateServerPort, fileName.c_str());
    err = httpUpdate.getLastError();
    err_str = httpUpdate.getLastErrorString();
#elif defined(ESP8266)
    ESPhttpUpdate.rebootOnUpdate(false); // remove automatic update
    String url = "http://" + updateServer + ":" + String(updateServerPort) + fileName.c_str();
    Serial.printf("Checking for update at %s ->", url.c_str());
    t_httpUpdate_return ret = ESPhttpUpdate.update(wifi_client, url);
    err = ESPhttpUpdate.getLastError();
    err_str = ESPhttpUpdate.getLastErrorString();
#endif
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        if (err == -102) {
          Serial.print("No updates");
} else if (err == -100) {
          Serial.print("Not enough space");
        } else {
          Serial.printf("Error (%d): %s", err, err_str.c_str());
        }
break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.print("No updates");
break;
      case HTTP_UPDATE_OK:
        Serial.printf("Update uploaded %s", timeToString(-1).c_str());
    break;
      default:
        Serial.print("Unknown OTA status");
        break;
    }
    Serial.println();
    return err;
  }

  std::string timeToString(long timestamp = -1) {
      time_t rawtime;
      if (timestamp < 0) {
        rawtime = time(nullptr);    // Default parameter:- get current time
      } else {
        rawtime = static_cast<time_t>(timestamp); // Otherwise convert the given argument
      }
      struct tm* dt = localtime(&rawtime);
      strftime(timestr, sizeof(timestr), "%Y-%m-%dT%X", dt);
      return std::string(timestr);
  }

  // Set time via NTP server
  time_t setClock() {
    Serial.print("NTP time sync");
    configTzTime(TZstr, ntpServer);
    time_t now = time(nullptr);
    short i = 0;
    while (now < 8 * 3600 * 2) {
      if (i > 50) {   // ~1s, give up
        return 0;
      }
      yield();
      delay(20);
      Serial.print(".");
      now = time(nullptr);
      i++;
    }
    Serial.printf(" ->%s\n", timeToString().c_str());
    return now;
  }

}   // end namespace mqtt_wifi
