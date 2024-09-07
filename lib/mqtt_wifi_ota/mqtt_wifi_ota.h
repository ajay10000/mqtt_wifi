#pragma once
// Library for WiFi and MQTT connections with OTA updates

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #include <HTTPUpdate.h>
#elif defined(ESP8266)
 #include <ESP8266WiFi.h>
 #include <WiFiClientSecure.h>
 #include <ESP8266httpUpdate.h>
#endif
//#include <Arduino.h>
#include <MQTTPubSubClient.h>
#include <time.h>
#include <string>
#include <credentials.h>  // Private library
// ssid, password and CA cert stored in credentials.h

//#define MY_DEBUG

namespace mqtt_wifi {
  const std::string branch_rev_name = "MQTT_WiFi_OTA_v2a";
  // NTP Time variables
  const char* ntpServer = "ntp.openwrt.ferndale";   // Local router is set as ntp server
  // TZ string information: // https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
  //AEDT starts 02:00 1st Sunday Oct, ends 02:00 1st Sunday April
  const char* TZstr = "AEST-10AEDT,M10.1.0/2,M4.1.0/2";
  char timestr[30];                             // char buffer for date/time
  // MQTT variables
  const char* mqttServer = "rpi4-2.ferndale";  // MQTT server, matches certificate
  const short mqttPort = 8883;                 // MQTT port
  // OTA variables
  const std::string updateServer = "update.ferndale";
  const short updateServerPort = 80;  // Caddy as http server from docker

  // Structure to store connection information in ESP 'RTC' memory
  // The ESP RTC memory is arranged into blocks of 4 bytes. The access methods read and write 4 bytes at a time,
  // so the RTC data structure should be a multiple of 4-bytes.
  #ifdef ESP32
  typedef struct {
    uint16_t magic;     // 2 bytes
    uint8_t channel;  // 1 byte
    uint8_t bssid[6];// 6 bytes
    uint32_t gatewayIP; // Assume dns_ip is the same IPV4 IP#  4 bytes
    uint32_t myIP;   // Device IP#                             4 bytes
    uint32_t subnet;  // Subnet mask -                          4 bytes
    uint16_t bootCount;  // 2 bytes
    uint8_t sleepIsOn;   // Sleep/loop switch, 1 byte, 24 in total
  } rtcDataStruct;
  RTC_NOINIT_ATTR rtcDataStruct rtcData;    // Survives reset, alternative to RTC_DATA_ATTR
  #elif defined(ESP8266)
  const uint8_t RTCDATA_OFFSET = 120;       // Offset blocks (of 4 bytes), 127 is highest available (512 bytes)
                                            // 32 blocks will be lost after performing an OTA update
  struct {
    uint16_t magic;     // 2 bytes
    uint8_t channel;  // 1 byte
    uint8_t bssid[6];// 6 bytes
    uint32_t gatewayIP; // Assume dns_ip is the same IPV4 IP#  4 bytes
    uint32_t myIP;   // Device IP#                             4 bytes
    uint32_t subnet;  // Subnet mask -                          4 bytes
    uint16_t bootCount;  // 2 bytes
    uint8_t sleepIsOn;      // 1 byte, 24 in total
  } rtcData;
  #endif
  const uint16_t RTCDATA_MAGIC = 0xF3ED;    // Magic number to check valid RTC read. 62445 decimal

  // Declare functions
  void wifiConnect();
  void wifiDisconnect();
  bool mqttConnect(std::string sensor);
  void mqttDisconnect();
  int8_t otaUpdate(std::string fileName);
  std::string timeToString(long timeStamp = -1);
  time_t setClock();

  WiFiClientSecure tlsClient;
  MQTTPubSubClient mqttClient;

  void wifiConnect() {
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
    static bool rtcValid = false;
    if (rtcData.magic == RTCDATA_MAGIC) rtcValid = true;

    Serial.printf("-> WiFi start millis: %lu ms\n", millis());
    Serial.printf("\nWiFi.status: %i\n", WiFi.status());
    WiFi.mode (WIFI_STA);
    if (rtcValid) {
      Serial.println("RTC OK, Try quick connection");
      // Bring up the WiFi connection
      WiFi.config(rtcData.myIP, rtcData.gatewayIP, rtcData.subnet, rtcData.gatewayIP);   // ip, gateway, subnet, dns
      WiFi.begin(ssid, password, rtcData.channel, rtcData.bssid, true);
    } else {
      Serial.println("First boot or RTC invalid, Try standard connection.");
      WiFi.begin( ssid, password, 0, NULL, true );
    }
    //Serial.printf("-> WiFi begin complete millis: %lu ms\n", millis());

    //------now wait for connection
    unsigned long check_time = millis() + 5000;
    bool fastConnect = true;
    while(WiFi.status() != WL_CONNECTED) {
      Serial.print(":");
      if( millis() > check_time && fastConnect == true) {
        Serial.printf("\nWiFi.status: %i\n", WiFi.status());
        Serial.println("WIFI not up in 5s, try standard connection.");
        WiFi.begin( ssid, password, 0, NULL, true );
        fastConnect = false;
      }
      if( millis() > (check_time + 5000) ) {
        Serial.println("\nWIFI not up in 10s, try reboot");
        delay(3000);
        ESP.restart();
      }
      delay(20);
      yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n-> WiFi connected millis: %lu ms\n", millis());
      #ifdef MY_DEBUG
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
        rtcData.gatewayIP = WiFi.gatewayIP();
        rtcData.myIP = WiFi.localIP();
        rtcData.subnet = WiFi.subnetMask();
        rtcData.bootCount = 0;
        rtcData.sleepIsOn = 1;
        // Disable WiFi persistence.  The ESP8266 will not load and save WiFi settings unnecessarily in the flash memory.
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
      }
      rtcData.bootCount++;    // RTC data OK, increment boot count
      // ESP32 RTC update is automatic.
      #ifdef ESP8266
      ESP.rtcUserMemoryWrite(120, (uint32_t*)&rtcData, sizeof(rtcData));
      #endif

      // Set the clock
      setClock();
      #ifdef MY_DEBUG
      Serial.printf("-> Clock set millis: %lu ms\n", millis());
      #endif

    } else {
      Serial.printf(" WiFi connection FAILED, restarting.\n");
      delay(3000);
      ESP.restart();
    }
  }

  void wifiDisconnect() {
    WiFi.disconnect(true);
    delay(1);
    WiFi.mode(WIFI_OFF);
  }

  bool mqttConnect(std::string sensor) {
    unsigned short i;
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnect();   // Connect to WiFi
      // mqttClient.begin only needed once per booted session.
      // Debugging for error: [WiFiGeneric.cpp:1230] hostByName(): DNS Failed for...
      try {
        mqttClient.begin(tlsClient);
      } catch (...) {
        delay(5000);
        ESP.restart();
      }
    }
    if (!tlsClient.connected()) {
      // Set up a secure connection to the MQTT server
      Serial.printf("Connecting to MQTT server");
      tlsClient.stop();
      #ifdef ESP32
      tlsClient.setCACert(root_ca_cert); // for CA certificate verification
      tlsClient.connect(mqttServer, mqttPort, 10000);
      #elif defined(ESP8266)
      tlsClient.setFingerprint(fingerprint);  // server cert fingerprint
      tlsClient.connect(mqttServer, mqttPort);
      #endif
      i = 0;
      // Try to connect for x * delay ms
      while (!tlsClient.connected()) {
        if (i >= 100) break;   // ~5s give up
        delay(50);
        Serial.print(",");
        i++;
        yield();
      }

      if (tlsClient.connected()) {
        Serial.print(" ->Connected.\n");
        // Connect the client to the MQTT server. Loop until connected, max. 5 times
        i = 0;
        Serial.print("Connecting MQTT client");
        mqttClient.connect(sensor.c_str());
        while ((!mqttClient.isConnected()) && (i < 5)) {
          if (i == 1) {
            mqttClient.disconnect();
            mqttClient.connect(sensor.c_str());
          }
          if (i == 4) {
            // Only print it once
            Serial.printf(" ->Could not connect MQTT client, rc= %i\n", mqttClient.getLastError());
            Serial.print("Rebooting now.");
            delay(3000);
            ESP.restart();
          } else {
            // Still connecting
            Serial.print(".");
            delay(50);
            yield();
          }
          i++;
        }
        Serial.printf("\n->Client %s is connected to %s.\n",sensor.c_str(), mqttServer);
        return true;

      } else {
        //Get the last error for WiFiClientSecure
        char buf[200];
        #ifdef ESP32
        int lastErr = tlsClient.lastError(buf, sizeof(buf));
        #elif defined(ESP8266)
        int lastErr = tlsClient.getLastSSLError(buf, sizeof(buf));
        #endif
        Serial.printf(" ->tlsClient error %i\n", lastErr);
        return false;
      }
    }
    return true;
  }

  void mqttDisconnect() {
    mqttClient.disconnect();   // Close MQTT connection
    wifiDisconnect();        // Shut down wifi
  }

  int8_t otaUpdate(std::string fileName) {
    // // Wait for serial buffer to empty
    // for (uint8_t t = 4; t > 0; t--) {
    //   //Serial.printf("[SETUP] WAIT %d...\n", t);
    //   Serial.flush();
    //   delay(100);
    // }
    WiFiClient wifiClient;
    // Not using https yet
    // WiFiClientSecure wifiClient;
    // #ifdef ESP32
    //   client.setCACert(root_ca_cert); // for CA certificate verification
    // #elif defined(ESP8266)
    //   wifiClient.setFingerprint(fingerprint);  // server cert fingerprint
    //   Serial.printf("ESP8266: Fingerprint set.\n");
    // #endif
    // unsigned int i = 0;
    // while (not wifiClient.connected() && (i < 10)) {
    //   Serial.printf(".");
    //   delay(200);
    //   i++;
    // }
    // if (wifiClient.connected()) {
    //   Serial.printf(" Secure client connected.\n");
    // } else {
    //   delay(5000);
    //   ESP.restart();
    // }
    int err = 0;
    String errStr;
    #if defined(ESP32)
    httpUpdate.rebootOnUpdate(false); // remove automatic update
    t_httpUpdate_return ret = httpUpdate.update(wifiClient, updateServer.c_str(), updateServerPort, fileName.c_str());
    err = httpUpdate.getLastError();
    errStr = httpUpdate.getLastErrorString();
    #elif defined(ESP8266)
    ESPhttpUpdate.rebootOnUpdate(false); // remove automatic update
    std::string url = "http://" + updateServer + ":" + String(updateServerPort) + fileName.c_str();
    t_httpUpdate_return ret = ESPhttpUpdate.update(wifiClient, url);
    err = ESPhttpUpdate.getLastError();
    err_str = ESPhttpUpdate.getLastErrorString();
    #endif
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        if (err == -102) {
          errStr = "No update";
        } else if (err == -100) {
          errStr = "No space for update";
        } else {
          errStr = "Update failed";
        }
        break;
      case HTTP_UPDATE_NO_UPDATES:
        errStr = "No update";
        break;
      case HTTP_UPDATE_OK:
        err = 0;
        errStr = "Update uploaded";
        break;
      default:
        errStr = "Unknown update status";
        break;
    }
    Serial.printf("OTA status (%d): %s\n", err, errStr.c_str());
    return err;
  }

  std::string timeToString(long timeStamp) {
    time_t rawtime;
    if (timeStamp < 0) {
      rawtime = time(nullptr);    // Default parameter:- get current time
    } else {
      rawtime = static_cast<time_t>(timeStamp); // Otherwise convert the given argument
    }
    struct tm* dt = localtime(&rawtime);
    strftime(timestr, sizeof(timestr), "%Y-%m-%dT%X", dt);
    return std::string(timestr);
  }

  // Set time via NTP server
  time_t setClock() {
    Serial.print("NTP sync");
    configTzTime(TZstr, ntpServer);
    time_t now = time(nullptr);
    unsigned short i = 0;
    while (now < 8 * 3600 * 2) {
      if (i > 100) {   // ~10s, give up
        return 0;
      }
      delay(100);
      Serial.print(".");
      now = time(nullptr);
      i++;
      yield();
    }
    Serial.printf(" ->%s\n", timeToString().c_str());
    return now;
  }

}   // end namespace mqtt_wifi
