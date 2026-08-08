//master---------------------------------------------------------------------------------------------------------

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// MASTER PIN CONFIGURATION
// =====================================================

#define LED1 26
#define LED2 33
#define LED3 32

#define BTN1 13
#define BTN2 14

// =====================================================
// ESP-NOW MAC ADDRESSES
// =====================================================

// SLEW / SLAVE
uint8_t SLEW_MAC[] = {
  0x08, 0x3A, 0xF2, 0x09, 0xBE, 0xF8
};

// MASTER
uint8_t MASTER_MAC[] = {
  0x80, 0xF3, 0xDA, 0x55, 0x40, 0x20
};

// =====================================================
// ESP-NOW CHANNEL
// =====================================================

#define ESPNOW_CHANNEL 1

// =====================================================
// MESSAGE TYPES
// =====================================================

#define MSG_HELLO       1
#define MSG_HELLO_ACK   2
#define MSG_HEARTBEAT   3
#define MSG_BTN1_STATE  4
#define MSG_RESET       5

// =====================================================
// DATA PACKET
// =====================================================

typedef struct {

  uint8_t type;

  uint8_t role;

  uint8_t buttonState;

  uint32_t counter;

} DataPacket;

// =====================================================
// ROLES
// =====================================================

#define ROLE_MASTER 1
#define ROLE_SLEW   2

// =====================================================
// STATUS
// =====================================================

volatile bool connected = false;

unsigned long lastMasterPacket = 0;
unsigned long lastHello = 0;
unsigned long lastHeartbeat = 0;

unsigned long lastBlink = 0;

bool blinkState = false;

// =====================================================
// TIMING
// =====================================================

const unsigned long HELLO_INTERVAL = 500;

const unsigned long HEARTBEAT_INTERVAL = 500;

const unsigned long CONNECTION_TIMEOUT = 2500;

const unsigned long BLINK_INTERVAL = 300;

// =====================================================
// BUTTON
// =====================================================

bool lastBtn2Reading = HIGH;
bool stableBtn2State = HIGH;

unsigned long btn2DebounceTime = 0;

const unsigned long DEBOUNCE_TIME = 40;

// =====================================================
// RESET COMMUNICATION STATE
// =====================================================

void resetCommunication()
{
  connected = false;

  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("COMMUNICATION RESET");
  Serial.println("SEARCHING FOR SLEW...");
  Serial.println("================================");
}

// =====================================================
// SEND PACKET TO SLEW
// =====================================================

void sendToSlew(uint8_t type, uint8_t buttonState = 0)
{
  DataPacket packet;

  packet.type = type;
  packet.role = ROLE_MASTER;
  packet.buttonState = buttonState;
  packet.counter = millis();

  esp_err_t result = esp_now_send(
    SLEW_MAC,
    (uint8_t *)&packet,
    sizeof(packet)
  );

  if (result != ESP_OK)
  {
    Serial.print("ESP-NOW SEND ERROR: ");
    Serial.println(result);
  }
}

// =====================================================
// RECEIVE CALLBACK
// =====================================================

void onDataReceive(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len)
{
  if (len != sizeof(DataPacket))
  {
    return;
  }

  // Make sure packet is from SLEW
  if (memcmp(info->src_addr, SLEW_MAC, 6) != 0)
  {
    return;
  }

  DataPacket packet;

  memcpy(
    &packet,
    data,
    sizeof(packet)
  );

  lastMasterPacket = millis();

  // ===================================================
  // HELLO FROM SLEW
  // ===================================================

  if (packet.type == MSG_HELLO)
  {
    Serial.println("SLEW FOUND");

    connected = true;

    // Send acknowledgement
    sendToSlew(MSG_HELLO_ACK);

    // Connected indication
    digitalWrite(LED1, HIGH);
    digitalWrite(LED2, HIGH);

    // LED3 initially OFF
    digitalWrite(LED3, LOW);

    Serial.println("MASTER <-> SLEW CONNECTED");
  }

  // ===================================================
  // HELLO ACK
  // ===================================================

  else if (packet.type == MSG_HELLO_ACK)
  {
    connected = true;

    digitalWrite(LED1, HIGH);
    digitalWrite(LED2, HIGH);
  }

  // ===================================================
  // HEARTBEAT
  // ===================================================

  else if (packet.type == MSG_HEARTBEAT)
  {
    connected = true;
  }

  // ===================================================
  // SLEW BUTTON 1
  // ===================================================

  else if (packet.type == MSG_BTN1_STATE)
  {
    connected = true;

    // BTN1 HELD
    if (packet.buttonState == 1)
    {
      Serial.println("SLEW BTN1 = HELD");

      // MASTER LED1 OFF
      digitalWrite(LED1, LOW);

      // MASTER LED3 OFF
      digitalWrite(LED3, LOW);
    }

    // BTN1 RELEASED
    else
    {
      Serial.println("SLEW BTN1 = RELEASED");

      // MASTER LED1 ON
      digitalWrite(LED1, HIGH);

      // MASTER LED3 ON
      digitalWrite(LED3, HIGH);
    }
  }

  // ===================================================
  // RESET FROM SLEW
  // ===================================================

  else if (packet.type == MSG_RESET)
  {
    resetCommunication();
  }
}

// =====================================================
// SEARCHING LED
// =====================================================

void searchingIndication()
{
  if (millis() - lastBlink >= BLINK_INTERVAL)
  {
    lastBlink = millis();

    blinkState = !blinkState;

    digitalWrite(LED1, blinkState);
    digitalWrite(LED2, blinkState);

    // LED3 always OFF while searching
    digitalWrite(LED3, LOW);
  }
}

// =====================================================
// CONNECTED LED
// =====================================================

void connectedIndication()
{
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, HIGH);
}

// =====================================================
// BTN2 RESET
// =====================================================

void checkResetButton()
{
  bool reading = digitalRead(BTN2);

  if (reading != lastBtn2Reading)
  {
    btn2DebounceTime = millis();
  }

  if ((millis() - btn2DebounceTime) > DEBOUNCE_TIME)
  {
    if (reading != stableBtn2State)
    {
      stableBtn2State = reading;

      // Button pressed
      if (stableBtn2State == LOW)
      {
        Serial.println();
        Serial.println("MASTER BTN2 PRESSED");
        Serial.println("RESETTING BOTH ESP32...");

        // Tell SLEW to reset
        sendToSlew(MSG_RESET);

        // Reset Master
        resetCommunication();
      }
    }
  }

  lastBtn2Reading = reading;
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println("       ESP-NOW MASTER");
  Serial.println("======================================");

  // ===================================================
  // GPIO
  // ===================================================

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

  // ===================================================
  // WIFI
  // ===================================================

  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  Serial.print("MASTER MAC: ");
  Serial.println(WiFi.macAddress());

  // ===================================================
  // ESP-NOW INIT
  // ===================================================

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAILED!");

    while (true)
    {
      digitalWrite(LED1, HIGH);
      delay(200);

      digitalWrite(LED1, LOW);
      delay(200);
    }
  }

  // ===================================================
  // ADD SLEW AS PEER
  // ===================================================

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    SLEW_MAC,
    6
  );

  peerInfo.channel = ESPNOW_CHANNEL;

  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("FAILED TO ADD SLEW PEER!");
  }
  else
  {
    Serial.println("SLEW PEER ADDED");
  }

  // ===================================================
  // RECEIVE CALLBACK
  // ===================================================

  esp_now_register_recv_cb(onDataReceive);

  Serial.println();
  Serial.println("ESP-NOW READY");
  Serial.println("SEARCHING FOR SLEW...");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  // Check reset button
  checkResetButton();

  // ===================================================
  // SEARCHING
  // ===================================================

  if (!connected)
  {
    searchingIndication();

    if (millis() - lastHello >= HELLO_INTERVAL)
    {
      lastHello = millis();

      sendToSlew(MSG_HELLO);

      Serial.println("Searching for SLEW...");
    }

    return;
  }

  // ===================================================
  // CONNECTED
  // ===================================================

  connectedIndication();

  // ===================================================
  // HEARTBEAT
  // ===================================================

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)
  {
    lastHeartbeat = millis();

    sendToSlew(MSG_HEARTBEAT);
  }

  // ===================================================
  // CONNECTION LOST
  // ===================================================

  if (millis() - lastMasterPacket > CONNECTION_TIMEOUT)
  {
    Serial.println();
    Serial.println("SLEW CONNECTION LOST!");

    resetCommunication();
  }
}







//slew--------------------------------------------------------------------------------------------




#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// SLEW PIN CONFIGURATION
// =====================================================

#define LED1 26
#define LED2 33

#define BTN1 13

// =====================================================
// ESP-NOW MAC ADDRESSES
// =====================================================

// SLEW
uint8_t SLEW_MAC[] = {
  0x08, 0x3A, 0xF2, 0x09, 0xBE, 0xF8
};

// MASTER
uint8_t MASTER_MAC[] = {
  0x80, 0xF3, 0xDA, 0x55, 0x40, 0x20
};

// =====================================================
// CHANNEL
// =====================================================

#define ESPNOW_CHANNEL 1

// =====================================================
// MESSAGE TYPES
// =====================================================

#define MSG_HELLO       1
#define MSG_HELLO_ACK   2
#define MSG_HEARTBEAT   3
#define MSG_BTN1_STATE  4
#define MSG_RESET       5

// =====================================================
// PACKET
// =====================================================

typedef struct {

  uint8_t type;

  uint8_t role;

  uint8_t buttonState;

  uint32_t counter;

} DataPacket;

// =====================================================
// ROLES
// =====================================================

#define ROLE_MASTER 1
#define ROLE_SLEW   2

// =====================================================
// STATUS
// =====================================================

volatile bool connected = false;

unsigned long lastMasterPacket = 0;
unsigned long lastHello = 0;
unsigned long lastHeartbeat = 0;

unsigned long lastBlink = 0;

bool blinkState = false;

// =====================================================
// TIMING
// =====================================================

const unsigned long HELLO_INTERVAL = 500;

const unsigned long HEARTBEAT_INTERVAL = 500;

const unsigned long CONNECTION_TIMEOUT = 2500;

const unsigned long BLINK_INTERVAL = 300;

// =====================================================
// BUTTON
// =====================================================

bool lastBtn1Reading = HIGH;
bool stableBtn1State = HIGH;

unsigned long btn1DebounceTime = 0;

const unsigned long DEBOUNCE_TIME = 40;

// =====================================================
// RESET
// =====================================================

void resetCommunication()
{
  connected = false;

  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("COMMUNICATION RESET");
  Serial.println("SEARCHING FOR MASTER...");
  Serial.println("================================");
}

// =====================================================
// SEND TO MASTER
// =====================================================

void sendToMaster(
  uint8_t type,
  uint8_t buttonState = 0)
{
  DataPacket packet;

  packet.type = type;

  packet.role = ROLE_SLEW;

  packet.buttonState = buttonState;

  packet.counter = millis();

  esp_err_t result = esp_now_send(
    MASTER_MAC,
    (uint8_t *)&packet,
    sizeof(packet)
  );

  if (result != ESP_OK)
  {
    Serial.print("ESP-NOW SEND ERROR: ");
    Serial.println(result);
  }
}

// =====================================================
// RECEIVE CALLBACK
// =====================================================

void onDataReceive(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len)
{
  if (len != sizeof(DataPacket))
  {
    return;
  }

  // Make sure packet is from MASTER
  if (memcmp(info->src_addr, MASTER_MAC, 6) != 0)
  {
    return;
  }

  DataPacket packet;

  memcpy(
    &packet,
    data,
    sizeof(packet)
  );

  lastMasterPacket = millis();

  // ===================================================
  // MASTER HELLO
  // ===================================================

  if (packet.type == MSG_HELLO)
  {
    Serial.println("MASTER FOUND");

    connected = true;

    // Send ACK
    sendToMaster(MSG_HELLO_ACK);

    // Connected indication
    digitalWrite(LED1, HIGH);
    digitalWrite(LED2, HIGH);

    Serial.println("MASTER <-> SLEW CONNECTED");
  }

  // ===================================================
  // MASTER ACK
  // ===================================================

  else if (packet.type == MSG_HELLO_ACK)
  {
    connected = true;

    digitalWrite(LED1, HIGH);
    digitalWrite(LED2, HIGH);

    Serial.println("MASTER ACK RECEIVED");
  }

  // ===================================================
  // HEARTBEAT
  // ===================================================

  else if (packet.type == MSG_HEARTBEAT)
  {
    connected = true;
  }

  // ===================================================
  // RESET FROM MASTER
  // ===================================================

  else if (packet.type == MSG_RESET)
  {
    Serial.println();
    Serial.println("RESET COMMAND FROM MASTER");

    resetCommunication();
  }
}

// =====================================================
// SEARCHING LED
// =====================================================

void searchingIndication()
{
  if (millis() - lastBlink >= BLINK_INTERVAL)
  {
    lastBlink = millis();

    blinkState = !blinkState;

    digitalWrite(LED1, blinkState);
    digitalWrite(LED2, blinkState);
  }
}

// =====================================================
// CONNECTED LED
// =====================================================

void connectedIndication()
{
  // LED1 + LED2 ON
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, HIGH);
}

// =====================================================
// BUTTON 1
// =====================================================

void checkButton1()
{
  bool reading = digitalRead(BTN1);

  // Detect change
  if (reading != lastBtn1Reading)
  {
    btn1DebounceTime = millis();
  }

  // Debounce
  if ((millis() - btn1DebounceTime) > DEBOUNCE_TIME)
  {
    if (reading != stableBtn1State)
    {
      stableBtn1State = reading;

      // =================================================
      // BUTTON PRESSED
      // =================================================

      if (stableBtn1State == LOW)
      {
        Serial.println("SLEW BTN1 PRESSED / HELD");

        // SLEW LED1 OFF
        digitalWrite(LED1, LOW);

        // Inform Master
        sendToMaster(
          MSG_BTN1_STATE,
          1
        );
      }

      // =================================================
      // BUTTON RELEASED
      // =================================================

      else
      {
        Serial.println("SLEW BTN1 RELEASED");

        // SLEW LED1 ON
        digitalWrite(LED1, HIGH);

        // Inform Master
        sendToMaster(
          MSG_BTN1_STATE,
          0
        );
      }
    }
  }

  lastBtn1Reading = reading;
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println("        ESP-NOW SLEW");
  Serial.println("======================================");

  // ===================================================
  // GPIO
  // ===================================================

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  pinMode(BTN1, INPUT_PULLUP);

  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);

  // ===================================================
  // WIFI
  // ===================================================

  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  Serial.print("SLEW MAC: ");
  Serial.println(WiFi.macAddress());

  // ===================================================
  // ESP-NOW INIT
  // ===================================================

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAILED!");

    while (true)
    {
      digitalWrite(LED1, HIGH);
      delay(200);

      digitalWrite(LED1, LOW);
      delay(200);
    }
  }

  // ===================================================
  // ADD MASTER AS PEER
  // ===================================================

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    MASTER_MAC,
    6
  );

  peerInfo.channel = ESPNOW_CHANNEL;

  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("FAILED TO ADD MASTER PEER!");
  }
  else
  {
    Serial.println("MASTER PEER ADDED");
  }

  // ===================================================
  // RECEIVE CALLBACK
  // ===================================================

  esp_now_register_recv_cb(onDataReceive);

  Serial.println();
  Serial.println("ESP-NOW READY");
  Serial.println("SEARCHING FOR MASTER...");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  // ===================================================
  // SEARCHING
  // ===================================================

  if (!connected)
  {
    searchingIndication();

    if (millis() - lastHello >= HELLO_INTERVAL)
    {
      lastHello = millis();

      sendToMaster(MSG_HELLO);

      Serial.println("Searching for MASTER...");
    }

    return;
  }

  // ===================================================
  // CONNECTED
  // ===================================================

  connectedIndication();

  // ===================================================
  // BUTTON 1
  // ===================================================

  checkButton1();

  // ===================================================
  // HEARTBEAT
  // ===================================================

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)
  {
    lastHeartbeat = millis();

    sendToMaster(MSG_HEARTBEAT);
  }

  // ===================================================
  // CONNECTION LOST
  // ===================================================

  if (millis() - lastMasterPacket > CONNECTION_TIMEOUT)
  {
    Serial.println();
    Serial.println("MASTER CONNECTION LOST!");

    resetCommunication();
  }
}
