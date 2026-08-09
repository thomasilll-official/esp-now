//master----------------------------------------------------------------------------------------------------------------------------------------------------------




#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// MASTER PINS
// =====================================================

#define LED1 11
#define LED2 12
#define LED3 13
#define LED4 2       // SLEW BUTTON STATUS

#define BTN1 5
#define BTN2 4       //spare(not in use)

// =====================================================
// SETTINGS
// =====================================================

const unsigned long SEARCH_BLINK_TIME = 100;
const unsigned long CONNECT_BLINK_TIME = 200;

const unsigned long SEQUENCE_TIME = 300;

const unsigned long HELLO_INTERVAL = 500;
const unsigned long HEARTBEAT_INTERVAL = 500;
const unsigned long CONNECTION_TIMEOUT = 2500;

const unsigned long DEBOUNCE_TIME = 40;

const int CONNECT_BLINK_COUNT = 3;

// =====================================================
// MAC ADDRESSES
// =====================================================

// SLEW
uint8_t SLEW_MAC[] = {
  0x08, 0x3A, 0xF2, 0x09, 0xBE, 0xF8
};

// MASTER
uint8_t MASTER_MAC[] = {
  0x1C, 0xDB, 0xD4, 0x40, 0x41, 0xF8
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
// SYSTEM STATES
// =====================================================

enum SystemState
{
  SEARCHING,
  CONNECT_BLINK,
  RESET_MODE,
  NORMAL_OPERATION,
  DISCONNECTED
};

SystemState systemState = SEARCHING;

// =====================================================
// SEARCHING
// =====================================================

unsigned long lastSearchBlink = 0;

bool searchLED1 = true;

// =====================================================
// CONNECTION BLINK
// =====================================================

unsigned long lastConnectBlink = 0;

int connectBlinkCount = 0;

bool connectLEDState = false;

// =====================================================
// DISCONNECT
// =====================================================
//
// ONLY LED1 AND LED2.
// LED3 OFF.
// LED4 OFF.
//
// LED1 -> LED2 -> LED1 -> LED2
// =====================================================

unsigned long lastDisconnectBlink = 0;

bool disconnectLED1 = true;

// =====================================================
// SEQUENCE
// =====================================================

bool masterBtn1Pressed = false;
bool sequenceMode = false;

bool sequenceLED1 = true;

unsigned long lastSequenceChange = 0;

// =====================================================
// COMMUNICATION
// =====================================================

bool connected = false;

unsigned long lastPacket = 0;
unsigned long lastHello = 0;
unsigned long lastHeartbeat = 0;

// =====================================================
// BUTTON 1
// =====================================================

bool lastBtn1 = HIGH;
bool stableBtn1 = HIGH;

unsigned long btn1Debounce = 0;

// =====================================================
// BUTTON 2
// =====================================================

bool lastBtn2 = HIGH;
bool stableBtn2 = HIGH;

unsigned long btn2Debounce = 0;

// =====================================================
// LED CONTROL
// =====================================================

void setMasterLEDs(
  bool led1,
  bool led2,
  bool led3)
{
  digitalWrite(LED1, led1 ? HIGH : LOW);
  digitalWrite(LED2, led2 ? HIGH : LOW);
  digitalWrite(LED3, led3 ? HIGH : LOW);
}

// =====================================================
// ENTER RESET MODE
// =====================================================
//
// LED1 = OFF
// LED2 = ON
// LED3 = OFF
// LED4 = OFF
// =====================================================

void enterResetMode()
{
  systemState = RESET_MODE;

  masterBtn1Pressed = false;
  sequenceMode = false;

  sequenceLED1 = true;

  // SLEW BUTTON STATUS LED OFF
  digitalWrite(LED4, LOW);

  setMasterLEDs(
    false,
    true,
    false
  );

  Serial.println();
  Serial.println("================================");
  Serial.println("READY - RESET MODE");
  Serial.println("LED1 = OFF");
  Serial.println("LED2 = ON");
  Serial.println("LED3 = OFF");
  Serial.println("LED4 = OFF");
  Serial.println("================================");
}

// =====================================================
// START CONNECTION BLINK
// =====================================================
//
// LED1 + LED2 blink together 3 times.
// LED3 OFF.
// LED4 OFF.
// =====================================================

void startConnectionBlink()
{
  systemState = CONNECT_BLINK;

  connectBlinkCount = 0;

  connectLEDState = false;

  lastConnectBlink = millis();

  // SLEW BUTTON STATUS OFF
  digitalWrite(LED4, LOW);

  setMasterLEDs(
    false,
    false,
    false
  );

  Serial.println();
  Serial.println("================================");
  Serial.println("SLEW CONNECTED");
  Serial.println("CONNECTION INDICATION: 3 BLINKS");
  Serial.println("================================");
}

// =====================================================
// CONNECTION BLINK HANDLER
// =====================================================

void connectionBlinkIndication()
{
  if (systemState != CONNECT_BLINK)
    return;

  if (millis() - lastConnectBlink >= CONNECT_BLINK_TIME)
  {
    lastConnectBlink = millis();

    connectLEDState = !connectLEDState;

    if (connectLEDState)
    {
      // LED1 + LED2 ON
      // LED3 OFF
      // LED4 OFF

      digitalWrite(LED4, LOW);

      setMasterLEDs(
        true,
        true,
        false
      );
    }
    else
    {
      // LED1 + LED2 OFF
      // LED3 OFF
      // LED4 OFF

      digitalWrite(LED4, LOW);

      setMasterLEDs(
        false,
        false,
        false
      );

      connectBlinkCount++;

      if (connectBlinkCount >= CONNECT_BLINK_COUNT)
      {
        enterResetMode();
      }
    }
  }
}

// =====================================================
// SEARCHING INDICATION
// =====================================================
//
// LED1 and LED2 alternate.
// LED3 OFF.
// LED4 OFF.
//
// 100ms:
// LED1 ON / LED2 OFF
//
// 100ms:
// LED1 OFF / LED2 ON
// =====================================================

void searchingIndication()
{
  if (systemState != SEARCHING)
    return;

  // LED4 always OFF while searching
  digitalWrite(LED4, LOW);

  if (millis() - lastSearchBlink >= SEARCH_BLINK_TIME)
  {
    lastSearchBlink = millis();

    searchLED1 = !searchLED1;

    if (searchLED1)
    {
      setMasterLEDs(
        true,
        false,
        false
      );
    }
    else
    {
      setMasterLEDs(
        false,
        true,
        false
      );
    }
  }
}

// =====================================================
// DISCONNECT INDICATION
// =====================================================
//
// ONLY LED1 AND LED2.
//
// Same indication as searching.
//
// LED1 -> LED2 -> LED1 -> LED2
//
// LED3 OFF.
// LED4 OFF.
// =====================================================

void disconnectIndication()
{
  if (systemState != DISCONNECTED)
    return;

  // LED4 always OFF during disconnect
  digitalWrite(LED4, LOW);

  if (millis() - lastDisconnectBlink >= SEARCH_BLINK_TIME)
  {
    lastDisconnectBlink = millis();

    disconnectLED1 = !disconnectLED1;

    if (disconnectLED1)
    {
      // LED1 ON
      // LED2 OFF
      // LED3 OFF
      // LED4 OFF

      setMasterLEDs(
        true,
        false,
        false
      );
    }
    else
    {
      // LED1 OFF
      // LED2 ON
      // LED3 OFF
      // LED4 OFF

      setMasterLEDs(
        false,
        true,
        false
      );
    }
  }
}

// =====================================================
// START DISCONNECT
// =====================================================

void startDisconnectMode()
{
  connected = false;

  systemState = DISCONNECTED;

  masterBtn1Pressed = false;
  sequenceMode = false;

  // LED4 OFF
  digitalWrite(LED4, LOW);

  // Start exactly like searching
  disconnectLED1 = true;

  lastDisconnectBlink = millis();

  // LED1 ON
  // LED2 OFF
  // LED3 OFF
  // LED4 OFF

  setMasterLEDs(
    true,
    false,
    false
  );

  Serial.println();
  Serial.println("================================");
  Serial.println("SLEW DISCONNECTED");
  Serial.println("DISCONNECT INDICATION");
  Serial.println("LED1 <-> LED2");
  Serial.println("100ms TIMER");
  Serial.println("LED3 = OFF");
  Serial.println("LED4 = OFF");
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
    if (!connected)
    {
      connected = true;

      startConnectionBlink();

      sendToSlew(MSG_HELLO_ACK);
    }
    else
    {
      sendToSlew(MSG_HELLO_ACK);
    }
  }

  // ===================================================
  // HELLO ACK
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
    if (!connected)
    {
      connected = true;

      startConnectionBlink();
    }

    lastPacket = millis();
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
      Serial.println("MASTER: LED1 ON");
      Serial.println("MASTER: LED2 OFF");
      Serial.println("MASTER: LED3 OFF");
      Serial.println("MASTER: LED4 ON");
      Serial.println("================================");

      // NEW:
      // SLEW BUTTON PRESSED = LED4 ON
      digitalWrite(LED4, HIGH);

      masterBtn1Pressed = true;

      sequenceMode = false;

      systemState = NORMAL_OPERATION;

      setMasterLEDs(
        true,
        false,
        false
      );
    }

    // =================================================
    // BUTTON RELEASED
    // =================================================

    else
    {
      Serial.println("================================");
      Serial.println("SLEW BTN1 RELEASED");
      Serial.println("MASTER: LED3 ON");
      Serial.println("MASTER: LED1/LED2 BLINK");
      Serial.println("MASTER: LED4 OFF");
      Serial.println("================================");

      // NEW:
      // SLEW BUTTON RELEASED = LED4 OFF
      digitalWrite(LED4, LOW);

      masterBtn1Pressed = false;

      sequenceMode = true;

      systemState = NORMAL_OPERATION;

      sequenceLED1 = true;

      lastSequenceChange = millis();

      setMasterLEDs(
        true,
        false,
        true
      );
    }
  }

  // ===================================================
  // RESET
  // ===================================================

  else if (packet.type == MSG_RESET)
  {
    // Reset also turns LED4 OFF
    digitalWrite(LED4, LOW);

    enterResetMode();
  }
}

// =====================================================
// RELEASE SEQUENCE
// =====================================================

void sequenceIndication()
{
  if (!sequenceMode)
    return;

  // LED3 ON
  digitalWrite(LED3, HIGH);

  // LED4 remains ON only if SLEW button is pressed.
  // Normally after release it is OFF.
  if (!masterBtn1Pressed)
  {
    digitalWrite(LED4, LOW);
  }

  if (millis() - lastSequenceChange >= SEQUENCE_TIME)
  {
    lastSequenceChange = millis();

    sequenceLED1 = !sequenceLED1;

    if (sequenceLED1)
    {
      digitalWrite(LED1, HIGH);
      digitalWrite(LED2, LOW);
    }
    else
    {
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
        if (systemState == RESET_MODE)
          return;

        masterBtn1Pressed = true;

        sequenceMode = false;

        systemState = NORMAL_OPERATION;

        setMasterLEDs(
          true,
          false,
          false
        );

        Serial.println("MASTER BTN1 PRESSED");
      }

      else
      {
        masterBtn1Pressed = false;

        Serial.println("MASTER BTN1 RELEASED");

        if (sequenceMode)
        {
          sequenceLED1 = true;

          lastSequenceChange = millis();

          setMasterLEDs(
            true,
            false,
            true
          );
        }
      }
    }
  }

  lastBtn1 = reading;
}

// =====================================================
// MASTER RESET BUTTON
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
        Serial.println();
        Serial.println("================================");
        Serial.println("MASTER RESET BUTTON PRESSED");
        Serial.println("================================");

        // LED4 OFF
        digitalWrite(LED4, LOW);

        // Send reset to SLEW
        sendToSlew(MSG_RESET);

        // Return to RESET MODE
        enterResetMode();
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

  // NEW LED4
  pinMode(LED4, OUTPUT);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  // ===================================================
  // POWER ON
  // ===================================================
  //
  // SEARCHING:
  // LED1 <-> LED2
  // LED3 OFF
  // LED4 OFF
  // ===================================================

  systemState = SEARCHING;

  searchLED1 = true;

  lastSearchBlink = millis();

  setMasterLEDs(
    true,
    false,
    false
  );

  digitalWrite(LED4, LOW);

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
      digitalWrite(LED4, LOW);

      delay(100);

      digitalWrite(LED1, LOW);

      delay(100);
    }
  }

  // ===================================================
  // ADD SLEW PEER
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
    Serial.println("FAILED TO ADD SLEW");
  }
  else
  {
    Serial.println("SLEW PEER ADDED");
  }

  esp_now_register_recv_cb(onDataReceive);

  Serial.println("ESP-NOW READY");
  Serial.println("SEARCHING FOR SLEW...");
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

  if (systemState == SEARCHING)
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
  // CONNECTION BLINK
  // ===================================================

  if (systemState == CONNECT_BLINK)
  {
    connectionBlinkIndication();

    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)
    {
      lastHeartbeat = millis();

      sendToSlew(MSG_HEARTBEAT);
    }

    return;
  }

  // ===================================================
  // DISCONNECTED
  // ===================================================

  if (systemState == DISCONNECTED)
  {
    // ONLY LED1 AND LED2 BLINK
    // LED3 OFF
    // LED4 OFF

    disconnectIndication();

    if (millis() - lastHello >= HELLO_INTERVAL)
    {
      lastHello = millis();

      sendToSlew(MSG_HELLO);
    }

    return;
  }

  // ===================================================
  // RESET MODE
  // ===================================================

  if (systemState == RESET_MODE)
  {
    // FORCE:
    //
    // LED1 OFF
    // LED2 ON
    // LED3 OFF
    // LED4 OFF

    setMasterLEDs(
      false,
      true,
      false
    );

    digitalWrite(LED4, LOW);

    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)
    {
      lastHeartbeat = millis();

      sendToSlew(MSG_HEARTBEAT);
    }

    if (millis() - lastPacket > CONNECTION_TIMEOUT)
    {
      startDisconnectMode();
    }

    return;
  }

  // ===================================================
  // NORMAL OPERATION
  // ===================================================

  if (systemState == NORMAL_OPERATION)
  {
    // =================================================
    // SLEW BUTTON PRESSED
    // =================================================

    if (masterBtn1Pressed)
    {
      setMasterLEDs(
        true,
        false,
        false
      );

      // SLEW BUTTON STATUS
      digitalWrite(LED4, HIGH);
    }

    // =================================================
    // RELEASE SEQUENCE
    // =================================================

    else if (sequenceMode)
    {
      sequenceIndication();

      // SLEW BUTTON RELEASED
      digitalWrite(LED4, LOW);
    }

    // =================================================
    // NORMAL CONNECTED
    // =================================================

    else
    {
      setMasterLEDs(
        true,
        true,
        false
      );

      // Button not pressed
      digitalWrite(LED4, LOW);
    }

    // =================================================
    // HEARTBEAT
    // =================================================

    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)
    {
      lastHeartbeat = millis();

      sendToSlew(MSG_HEARTBEAT);
    }

    // =================================================
    // CONNECTION LOST
    // =================================================

    if (millis() - lastPacket > CONNECTION_TIMEOUT)
    {
      startDisconnectMode();
    }
  }
}



//slew-------------------------------------------------------------------------------------------------------------------------------------------------------------


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
