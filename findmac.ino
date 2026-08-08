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
