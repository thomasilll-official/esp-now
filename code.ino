//master--------------------------------------------------------------------



#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// MASTER PINS
// =====================================================

#define LED1 26
#define LED2 33
#define LED3 32

#define BTN1 13
#define BTN2 14

// =====================================================
// SETTINGS
// =====================================================

const unsigned long SEARCH_BLINK_TIME = 300;
const unsigned long SEQUENCE_TIME = 1000;

const unsigned long HELLO_INTERVAL = 500;
const unsigned long HEARTBEAT_INTERVAL = 500;
const unsigned long CONNECTION_TIMEOUT = 2500;

const unsigned long DEBOUNCE_TIME = 40;

// =====================================================
// MAC ADDRESSES
// =====================================================

// SLEW
uint8_t SLEW_MAC[] = {
  0x08, 0x3A, 0xF2, 0x09, 0xBE, 0xF8
};

// MASTER
uint8_t MASTER_MAC[] = {
  0x80, 0xF3, 0xDA, 0x55, 0x40, 0x20
};

#define ESPNOW_CHANNEL 1

// =====================================================
// MESSAGE TYPES
// =====================================================

#define MSG_HELLO       1
#define MSG_HELLO_ACK   2
#define MSG_HEARTBEAT   3
#define MSG_BTN1_STATE  4
#define MSG_RESET       5

#define ROLE_MASTER 1
#define ROLE_SLEW   2

// =====================================================
// PACKET
// =====================================================

typedef struct
{
  uint8_t type;
  uint8_t role;
  uint8_t buttonState;
  uint32_t counter;
} DataPacket;

// =====================================================
// VARIABLES
// =====================================================

bool connected = false;

bool masterBtn1Pressed = false;

bool sequenceMode = false;

// IMPORTANT:
// true  = LED26 ON / LED33 OFF
// false = LED26 OFF / LED33 ON
bool sequenceLED26 = true;

bool searchState = false;

unsigned long lastPacket = 0;
unsigned long lastHello = 0;
unsigned long lastHeartbeat = 0;

unsigned long lastSearchBlink = 0;
unsigned long lastSequenceChange = 0;

// Button 1
bool lastBtn1 = HIGH;
bool stableBtn1 = HIGH;
unsigned long btn1Debounce = 0;

// Button 2
bool lastBtn2 = HIGH;
bool stableBtn2 = HIGH;
unsigned long btn2Debounce = 0;

// =====================================================
// SET MASTER LED STATE
// =====================================================

void setMasterLEDs(
  bool led26,
  bool led33,
  bool led32)
{
  digitalWrite(LED1, led26 ? HIGH : LOW);
  digitalWrite(LED2, led33 ? HIGH : LOW);
  digitalWrite(LED3, led32 ? HIGH : LOW);
}

// =====================================================
// RESET
// =====================================================

void resetCommunication()
{
  connected = false;
  masterBtn1Pressed = false;
  sequenceMode = false;

  sequenceLED26 = true;

  setMasterLEDs(false, false, false);

  Serial.println();
  Serial.println("================================");
  Serial.println("RESET");
  Serial.println("SEARCHING FOR SLEW...");
  Serial.println("================================");
}

// =====================================================
// SEND TO SLEW
// =====================================================

void sendToSlew(
  uint8_t type,
  uint8_t buttonState = 0)
{
  DataPacket packet;

  packet.type = type;
  packet.role = ROLE_MASTER;
  packet.buttonState = buttonState;
  packet.counter = millis();

  esp_now_send(
    SLEW_MAC,
    (uint8_t *)&packet,
    sizeof(packet)
  );
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
    return;

  if (memcmp(info->src_addr, SLEW_MAC, 6) != 0)
    return;

  DataPacket packet;

  memcpy(
    &packet,
    data,
    sizeof(packet)
  );

  lastPacket = millis();

  // ===================================================
  // HELLO
  // ===================================================

  if (packet.type == MSG_HELLO)
  {
    connected = true;
    sequenceMode = false;

    sendToSlew(MSG_HELLO_ACK);

    // Connected indication
    setMasterLEDs(true, true, false);

    Serial.println("SLEW CONNECTED");
  }

  // ===================================================
  // HELLO ACK
  // ===================================================

  else if (packet.type == MSG_HELLO_ACK)
  {
    connected = true;

    if (!sequenceMode)
    {
      setMasterLEDs(true, true, false);
    }
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

    // =================================================
    // BUTTON PRESSED
    // =================================================

    if (packet.buttonState == 1)
    {
      Serial.println("================================");
      Serial.println("SLEW BTN1 PRESSED");
      Serial.println("MASTER: LED26 ON");
      Serial.println("MASTER: LED33 OFF");
      Serial.println("MASTER: LED32 OFF");
      Serial.println("================================");

      masterBtn1Pressed = true;

      sequenceMode = false;

      // VERY IMPORTANT:
      // Explicitly set both LEDs opposite.
      //
      // LED26 = ON
      // LED33 = OFF
      //
      // LED32 = OFF

      setMasterLEDs(
        true,   // GPIO26 ON
        false,  // GPIO33 OFF
        false   // GPIO32 OFF
      );
    }

    // =================================================
    // BUTTON RELEASED
    // =================================================

    else
    {
      Serial.println("================================");
      Serial.println("SLEW BTN1 RELEASED");
      Serial.println("MASTER: LED32 ON");
      Serial.println("MASTER: LED26/33 BLINK");
      Serial.println("================================");

      masterBtn1Pressed = false;

      sequenceMode = true;

      // Start with LED26 ON and LED33 OFF
      sequenceLED26 = true;

      lastSequenceChange = millis();

      setMasterLEDs(
        true,   // GPIO26 ON
        false,  // GPIO33 OFF
        true    // GPIO32 ON
      );
    }
  }

  // ===================================================
  // RESET
  // ===================================================

  else if (packet.type == MSG_RESET)
  {
    resetCommunication();
  }
}

// =====================================================
// SEARCHING
// =====================================================

void searchingIndication()
{
  if (millis() - lastSearchBlink >= SEARCH_BLINK_TIME)
  {
    lastSearchBlink = millis();

    searchState = !searchState;

    if (searchState)
    {
      setMasterLEDs(true, false, false);
    }
    else
    {
      setMasterLEDs(false, true, false);
    }
  }
}

// =====================================================
// LED26 / LED33 SEQUENCE
// =====================================================

void sequenceIndication()
{
  if (!sequenceMode)
    return;

  // LED32 always ON
  digitalWrite(LED3, HIGH);

  if (millis() - lastSequenceChange >= SEQUENCE_TIME)
  {
    lastSequenceChange = millis();

    sequenceLED26 = !sequenceLED26;

    if (sequenceLED26)
    {
      // ===============================================
      // LED26 ON
      // LED33 OFF
      // ===============================================

      digitalWrite(LED1, HIGH);
      digitalWrite(LED2, LOW);
    }
    else
    {
      // ===============================================
      // LED26 OFF
      // LED33 ON
      // ===============================================

      digitalWrite(LED1, LOW);
      digitalWrite(LED2, HIGH);
    }
  }
}

// =====================================================
// MASTER BUTTON 1
// =====================================================

void checkMasterButton1()
{
  bool reading = digitalRead(BTN1);

  if (reading != lastBtn1)
  {
    btn1Debounce = millis();
  }

  if (millis() - btn1Debounce > DEBOUNCE_TIME)
  {
    if (reading != stableBtn1)
    {
      stableBtn1 = reading;

      if (stableBtn1 == LOW)
      {
        masterBtn1Pressed = true;
        sequenceMode = false;

        Serial.println("MASTER BTN1 PRESSED");

        setMasterLEDs(
          true,
          false,
          false
        );
      }
      else
      {
        masterBtn1Pressed = false;

        Serial.println("MASTER BTN1 RELEASED");

        if (sequenceMode)
        {
          sequenceLED26 = true;
          lastSequenceChange = millis();

          setMasterLEDs(
            true,
            false,
            true
          );
        }
        else
        {
          setMasterLEDs(
            true,
            true,
            false
          );
        }
      }
    }
  }

  lastBtn1 = reading;
}

// =====================================================
// MASTER BUTTON 2
// =====================================================

void checkMasterButton2()
{
  bool reading = digitalRead(BTN2);

  if (reading != lastBtn2)
  {
    btn2Debounce = millis();
  }

  if (millis() - btn2Debounce > DEBOUNCE_TIME)
  {
    if (reading != stableBtn2)
    {
      stableBtn2 = reading;

      if (stableBtn2 == LOW)
      {
        Serial.println("MASTER BTN2 PRESSED");

        sendToSlew(MSG_RESET);

        resetCommunication();
      }
    }
  }

  lastBtn2 = reading;
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
  Serial.println("          ESP-NOW MASTER");
  Serial.println("======================================");

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  setMasterLEDs(false, false, false);

  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  Serial.print("MASTER MAC: ");
  Serial.println(WiFi.macAddress());

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

  // Add SLEW
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
    Serial.println("FAILED TO ADD SLEW");
  }
  else
  {
    Serial.println("SLEW PEER ADDED");
  }

  esp_now_register_recv_cb(onDataReceive);

  Serial.println("ESP-NOW READY");
  Serial.println("SEARCHING...");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  checkMasterButton1();
  checkMasterButton2();

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
    }

    return;
  }

  // ===================================================
  // SLEW BTN1 PRESSED
  // ===================================================

  if (masterBtn1Pressed)
  {
    // ALWAYS FORCE:
    // GPIO26 ON
    // GPIO33 OFF
    // GPIO32 OFF

    setMasterLEDs(
      true,
      false,
      false
    );
  }

  // ===================================================
  // RELEASE SEQUENCE
  // ===================================================

  else if (sequenceMode)
  {
    sequenceIndication();
  }

  // ===================================================
  // NORMAL CONNECTED
  // ===================================================

  else
  {
    setMasterLEDs(
      true,
      true,
      false
    );
  }

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

  if (millis() - lastPacket > CONNECTION_TIMEOUT)
  {
    Serial.println("SLEW CONNECTION LOST!");

    resetCommunication();
  }
}







//slew-------------------------------------------------------------------------------------------

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// SLEW PINS
// =====================================================

#define LED1 26
#define LED2 33

#define BTN1 13

// =====================================================
// SETTINGS
// =====================================================

const unsigned long SEARCH_BLINK_TIME = 300;

const unsigned long CONNECT_BLINK_TIME = 250;

const int CONNECT_BLINK_COUNT = 5;

const unsigned long HELLO_INTERVAL = 500;
const unsigned long HEARTBEAT_INTERVAL = 500;
const unsigned long CONNECTION_TIMEOUT = 2500;

const unsigned long DEBOUNCE_TIME = 40;

// =====================================================
// MAC ADDRESSES
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

#define ROLE_MASTER 1
#define ROLE_SLEW   2

// =====================================================
// PACKET
// =====================================================

typedef struct
{
  uint8_t type;
  uint8_t role;
  uint8_t buttonState;
  uint32_t counter;

} DataPacket;

// =====================================================
// VARIABLES
// =====================================================

bool connected = false;

// =====================================================
// TIMERS
// =====================================================

unsigned long lastMasterPacket = 0;
unsigned long lastHello = 0;
unsigned long lastHeartbeat = 0;

unsigned long lastSearchBlink = 0;
unsigned long lastConnectBlink = 0;

// =====================================================
// SEARCHING STATE
// =====================================================

bool searchState = false;

// =====================================================
// CONNECTION SUCCESS BLINK
// =====================================================

bool connectionBlinkActive = false;

bool connectionLEDState = false;

int connectionBlinkCount = 0;

// =====================================================
// BUTTON
// =====================================================

bool lastBtn1 = HIGH;
bool stableBtn1 = HIGH;

unsigned long btn1Debounce = 0;

// =====================================================
// SET SLEW LEDS
// =====================================================

void setSlewLEDs(
  bool led26,
  bool led33)
{
  digitalWrite(
    LED1,
    led26 ? HIGH : LOW
  );

  digitalWrite(
    LED2,
    led33 ? HIGH : LOW
  );
}

// =====================================================
// RESET COMMUNICATION
// =====================================================

void resetCommunication()
{
  connected = false;

  connectionBlinkActive = false;

  connectionLEDState = false;

  connectionBlinkCount = 0;

  // IMPORTANT:
  // Always turn both LEDs OFF on reset.

  setSlewLEDs(
    false,
    false
  );

  Serial.println();
  Serial.println("================================");
  Serial.println("COMMUNICATION RESET");
  Serial.println("SLEW LEDs OFF");
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

  esp_now_send(
    MASTER_MAC,
    (uint8_t *)&packet,
    sizeof(packet)
  );
}

// =====================================================
// START CONNECTION BLINK
// =====================================================

void startConnectionBlink()
{
  connectionBlinkActive = true;

  connectionLEDState = false;

  connectionBlinkCount = 0;

  lastConnectBlink = millis();

  // IMPORTANT:
  // Start from OFF.

  setSlewLEDs(
    false,
    false
  );

  Serial.println();
  Serial.println("================================");
  Serial.println("MASTER CONNECTED");
  Serial.println("START 5 BLINKS");
  Serial.println("================================");
}

// =====================================================
// CONNECTION SUCCESS BLINK
// =====================================================

void connectionBlink()
{
  if (!connectionBlinkActive)
  {
    return;
  }

  if (millis() - lastConnectBlink >= CONNECT_BLINK_TIME)
  {
    lastConnectBlink = millis();

    connectionLEDState = !connectionLEDState;

    // =================================================
    // LED ON
    // =================================================

    if (connectionLEDState)
    {
      setSlewLEDs(
        true,
        true
      );
    }

    // =================================================
    // LED OFF
    // =================================================

    else
    {
      setSlewLEDs(
        false,
        false
      );

      connectionBlinkCount++;

      Serial.print("Connection blink: ");
      Serial.println(connectionBlinkCount);

      // =================================================
      // 5 COMPLETE BLINKS
      // =================================================

      if (connectionBlinkCount >= CONNECT_BLINK_COUNT)
      {
        connectionBlinkActive = false;

        // IMPORTANT:
        // Final state ALWAYS OFF.

        setSlewLEDs(
          false,
          false
        );

        Serial.println("5 BLINKS COMPLETE");
        Serial.println("SLEW LED26 = OFF");
        Serial.println("SLEW LED33 = OFF");
      }
    }
  }
}

// =====================================================
// RECEIVE DATA
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

  // Only accept MASTER
  if (memcmp(
        info->src_addr,
        MASTER_MAC,
        6) != 0)
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
    bool wasDisconnected = !connected;

    connected = true;

    // ACK MASTER
    sendToMaster(MSG_HELLO_ACK);

    // New connection
    if (wasDisconnected)
    {
      startConnectionBlink();
    }

    Serial.println("MASTER FOUND");
  }

  // ===================================================
  // MASTER HELLO ACK
  // ===================================================

  else if (packet.type == MSG_HELLO_ACK)
  {
    if (!connected)
    {
      connected = true;

      startConnectionBlink();
    }
  }

  // ===================================================
  // HEARTBEAT
  // ===================================================

  else if (packet.type == MSG_HEARTBEAT)
  {
    connected = true;
  }

  // ===================================================
  // RESET
  // ===================================================

  else if (packet.type == MSG_RESET)
  {
    Serial.println("RESET COMMAND FROM MASTER");

    resetCommunication();
  }
}

// =====================================================
// SEARCHING INDICATION
// =====================================================

void searchingIndication()
{
  if (millis() - lastSearchBlink >= SEARCH_BLINK_TIME)
  {
    lastSearchBlink = millis();

    searchState = !searchState;

    if (searchState)
    {
      // LED26 ON
      // LED33 OFF

      setSlewLEDs(
        true,
        false
      );
    }
    else
    {
      // LED26 OFF
      // LED33 ON

      setSlewLEDs(
        false,
        true
      );
    }
  }
}

// =====================================================
// BUTTON 1
// =====================================================

void checkButton1()
{
  bool reading = digitalRead(BTN1);

  if (reading != lastBtn1)
  {
    btn1Debounce = millis();
  }

  if (millis() - btn1Debounce > DEBOUNCE_TIME)
  {
    if (reading != stableBtn1)
    {
      stableBtn1 = reading;

      // =================================================
      // BUTTON PRESSED
      // =================================================

      if (stableBtn1 == LOW)
      {
        Serial.println("SLEW BTN1 PRESSED");

        // Slew LEDs ALWAYS OFF
        setSlewLEDs(
          false,
          false
        );

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

        // Slew LEDs ALWAYS OFF
        setSlewLEDs(
          false,
          false
        );

        sendToMaster(
          MSG_BTN1_STATE,
          0
        );
      }
    }
  }

  lastBtn1 = reading;
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
  Serial.println("             ESP-NOW SLEW");
  Serial.println("======================================");

  // ===================================================
  // GPIO
  // ===================================================

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  pinMode(
    BTN1,
    INPUT_PULLUP
  );

  // IMPORTANT:
  // Force LEDs OFF immediately.

  setSlewLEDs(
    false,
    false
  );

  // ===================================================
  // WIFI
  // ===================================================

  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  Serial.print("SLEW MAC: ");
  Serial.println(
    WiFi.macAddress()
  );

  // ===================================================
  // ESP-NOW
  // ===================================================

  if (esp_now_init() != ESP_OK)
  {
    Serial.println(
      "ESP-NOW INIT FAILED!"
    );

    while (true)
    {
      setSlewLEDs(
        true,
        false
      );

      delay(200);

      setSlewLEDs(
        false,
        false
      );

      delay(200);
    }
  }

  // ===================================================
  // ADD MASTER PEER
  // ===================================================

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    MASTER_MAC,
    6
  );

  peerInfo.channel = ESPNOW_CHANNEL;

  peerInfo.encrypt = false;

  if (esp_now_add_peer(
        &peerInfo) != ESP_OK)
  {
    Serial.println(
      "FAILED TO ADD MASTER PEER!"
    );
  }
  else
  {
    Serial.println(
      "MASTER PEER ADDED"
    );
  }

  // ===================================================
  // RECEIVE CALLBACK
  // ===================================================

  esp_now_register_recv_cb(
    onDataReceive
  );

  Serial.println();
  Serial.println("ESP-NOW READY");
  Serial.println("SLEW LEDs = OFF");
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

      sendToMaster(
        MSG_HELLO
      );
    }

    return;
  }

  // ===================================================
  // CONNECTION SUCCESS BLINK
  // ===================================================

  connectionBlink();

  // ===================================================
  // BUTTON
  // ===================================================

  checkButton1();

  // ===================================================
  // HEARTBEAT
  // ===================================================

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)
  {
    lastHeartbeat = millis();

    sendToMaster(
      MSG_HEARTBEAT
    );
  }

  // ===================================================
  // CONNECTION LOST
  // ===================================================

  if (millis() - lastMasterPacket >
      CONNECTION_TIMEOUT)
  {
    Serial.println();
    Serial.println(
      "MASTER CONNECTION LOST!"
    );

    resetCommunication();
  }
}
