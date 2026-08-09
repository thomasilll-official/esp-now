//normal wroom
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin();

  delay(1000);

  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Chip Model: ");
  Serial.println(ESP.getChipModel());

  Serial.print("Chip Revision: ");
  Serial.println(ESP.getChipRevision());

  Serial.print("Chip Cores: ");
  Serial.println(ESP.getChipCores());
}

void loop() {
}
//s3 chipset cam--------------------

#include <Arduino.h>
#include "esp_mac.h"

void setup() {
  Serial.begin(115200);
  delay(2000);

  uint8_t mac[6];

  // Read factory/base MAC address from eFuse
  esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);

  Serial.println();
  Serial.println("================================");
  Serial.println("       ESP32-S3 MAC ADDRESS");
  Serial.println("================================");

  Serial.printf(
    "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    mac[0], mac[1], mac[2],
    mac[3], mac[4], mac[5]
  );

  Serial.println("================================");
}

void loop() {
}

//s3 u1------------------------------------------------------

#include <Arduino.h>
#include "esp_mac.h"

void setup() {
  Serial.begin(115200);
  delay(3000);

  uint8_t mac[6];

  // Read factory MAC from ESP32-S3 eFuse
  esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);

  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP32-S3 U1 MAC ADDRESS");
  Serial.println("==============================");

  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);

  Serial.println("==============================");
}

void loop() {
  delay(1000);
}
