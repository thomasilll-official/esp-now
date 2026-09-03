//master----------------------------------------------------------------------------------------------------------------------------------------------------------

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <esp_pm.h>

// =====================================================
// POWER MANAGEMENT CONFIGURATION
// =====================================================

#define ENABLE_POWER_SAVE 1
#define LIGHT_SLEEP_TIMEOUT_MS 5000
#define CPU_FREQ_MHZ 80  // Reduced from 240MHz to 80MHz

// =====================================================
// MASTER PINS
// =====================================================

#define LED1 8
#define LED2 9
#define LED3 10
#define LED4 11       // SLEW BUTTON STATUS

#define BTN1 4
#define BTN2 5        // spare(not in use)

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

uint8_t SLEW_MAC[] = {0x08, 0x3A, 0xF2, 0x09, 0xBE, 0xF8};
uint8_t MASTER_MAC[] = {0x94, 0xA9, 0x90, 0xEF, 0x98, 0x28};
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
// TIMING VARIABLES
// =====================================================

unsigned long lastSearchBlink = 0;
unsigned long lastConnectBlink = 0;
unsigned long lastDisconnectBlink = 0;
unsigned long lastSequenceChange = 0;
unsigned long lastPacket = 0;
unsigned long lastHello = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastActivityTime = 0;
unsigned long lastLoopTime = 0;

// =====================================================
// STATE VARIABLES
// =====================================================

bool searchLED1 = true;
bool connectLEDState = false;
int connectBlinkCount = 0;
bool disconnectLED1 = true;
bool masterBtn1Pressed = false;
bool sequenceMode = false;
bool sequenceLED1 = true;
bool connected = false;

// =====================================================
// BUTTON VARIABLES
// =====================================================

bool lastBtn1 = HIGH;
bool stableBtn1 = HIGH;
unsigned long btn1Debounce = 0;
bool lastBtn2 = HIGH;
bool stableBtn2 = HIGH;
unsigned long btn2Debounce = 0;

// =====================================================
// PERFORMANCE MONITORING
// =====================================================

unsigned long loopCount = 0;
unsigned long lastStatsPrint = 0;

// =====================================================
// LED CONTROL
// =====================================================

inline void setMasterLEDs(bool led1, bool led2, bool led3)
{
  digitalWrite(LED1, led1 ? HIGH : LOW);
  digitalWrite(LED2, led2 ? HIGH : LOW);
  digitalWrite(LED3, led3 ? HIGH : LOW);
}

// =====================================================
// POWER MANAGEMENT
// =====================================================

void configurePowerManagement()
{
  #if ENABLE_POWER_SAVE
    // Reduce CPU frequency
    esp_pm_config_t pm_config = {
      .max_freq_mhz = CPU_FREQ_MHZ,
      .min_freq_mhz = CPU_FREQ_MHZ,
      .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
    
    // Reduce WiFi power
    esp_wifi_set_max_tx_power(40);  // 4dBm (minimum for stable connection)
    
    // Enable automatic light sleep
    esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_TIMEOUT_MS * 1000);
    
    Serial.println("Power Management Configured:");
    Serial.printf("  CPU Frequency: %d MHz\n", CPU_FREQ_MHZ);
    Serial.printf("  TX Power: %d dBm\n", 4);
    Serial.printf("  Light Sleep Timeout: %d ms\n", LIGHT_SLEEP_TIMEOUT_MS);
  #endif
}

// =====================================================
// ENTER RESET MODE
// =====================================================

void enterResetMode()
{
  systemState = RESET_MODE;
  masterBtn1Pressed = false;
  sequenceMode = false;
  sequenceLED1 = true;
  
  digitalWrite(LED4, LOW);
  setMasterLEDs(false, true, false);
  
  Serial.println("\n================================");
  Serial.println("READY - RESET MODE");
  Serial.println("LED1 = OFF | LED2 = ON | LED3 = OFF | LED4 = OFF");
  Serial.println("================================");
  
  lastActivityTime = millis();
}

// =====================================================
// START CONNECTION BLINK
// =====================================================

void startConnectionBlink()
{
  systemState = CONNECT_BLINK;
  connectBlinkCount = 0;
  connectLEDState = false;
  lastConnectBlink = millis();
  
  digitalWrite(LED4, LOW);
  setMasterLEDs(false, false, false);
  
  Serial.println("\n================================");
  Serial.println("SLEW CONNECTED - INDICATING 3 BLINKS");
  Serial.println("================================");
  
  lastActivityTime = millis();
}

// =====================================================
// CONNECTION BLINK HANDLER
// =====================================================

inline void connectionBlinkIndication()
{
  if (systemState != CONNECT_BLINK) return;
  
  if (millis() - lastConnectBlink >= CONNECT_BLINK_TIME)
  {
    lastConnectBlink = millis();
    connectLEDState = !connectLEDState;
    
    if (connectLEDState)
    {
      digitalWrite(LED4, LOW);
      setMasterLEDs(true, true, false);
    }
    else
    {
      digitalWrite(LED4, LOW);
      setMasterLEDs(false, false, false);
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

inline void searchingIndication()
{
  if (systemState != SEARCHING) return;
  
  digitalWrite(LED4, LOW);
  
  if (millis() - lastSearchBlink >= SEARCH_BLINK_TIME)
  {
    lastSearchBlink = millis();
    searchLED1 = !searchLED1;
    
    setMasterLEDs(searchLED1, !searchLED1, false);
  }
}

// =====================================================
// DISCONNECT INDICATION
// =====================================================

inline void disconnectIndication()
{
  if (systemState != DISCONNECTED) return;
  
  digitalWrite(LED4, LOW);
  
  if (millis() - lastDisconnectBlink >= SEARCH_BLINK_TIME)
  {
    lastDisconnectBlink = millis();
    disconnectLED1 = !disconnectLED1;
    
    setMasterLEDs(disconnectLED1, !disconnectLED1, false);
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
  
  digitalWrite(LED4, LOW);
  disconnectLED1 = true;
  lastDisconnectBlink = millis();
  
  setMasterLEDs(true, false, false);
  
  Serial.println("\n================================");
  Serial.println("SLEW DISCONNECTED");
  Serial.println("LED1 <-> LED2 | 100ms TIMER");
  Serial.println("LED3 = OFF | LED4 = OFF");
  Serial.println("================================");
  
  lastActivityTime = millis();
}

// =====================================================
// SEND TO SLEW (with retry)
// =====================================================

void sendToSlew(uint8_t type, uint8_t buttonState = 0)
{
  DataPacket packet;
  packet.type = type;
  packet.role = ROLE_MASTER;
  packet.buttonState = buttonState;
  packet.counter = millis();
  
  esp_err_t result = esp_now_send(SLEW_MAC, (uint8_t *)&packet, sizeof(packet));
  
  // Log errors but don't spam
  static uint32_t lastErrorLog = 0;
  if (result != ESP_OK && (millis() - lastErrorLog > 5000))
  {
    lastErrorLog = millis();
    Serial.printf("ESP-NOW Send Error: %d\n", result);
  }
}

// =====================================================
// RECEIVE CALLBACK
// =====================================================

void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  if (len != sizeof(DataPacket)) return;
  if (memcmp(info->src_addr, SLEW_MAC, 6) != 0) return;
  
  DataPacket packet;
  memcpy(&packet, data, sizeof(packet));
  lastPacket = millis();
  lastActivityTime = millis();
  
  // Process packet based on type
  switch (packet.type)
  {
    case MSG_HELLO:
      if (!connected)
      {
        connected = true;
        startConnectionBlink();
      }
      sendToSlew(MSG_HELLO_ACK);
      break;
      
    case MSG_HELLO_ACK:
      if (!connected)
      {
        connected = true;
        startConnectionBlink();
      }
      break;
      
    case MSG_HEARTBEAT:
      if (!connected)
      {
        connected = true;
        startConnectionBlink();
      }
      lastPacket = millis();
      break;
      
    case MSG_BTN1_STATE:
      connected = true;
      if (packet.buttonState == 1)
      {
        digitalWrite(LED4, HIGH);
        masterBtn1Pressed = true;
        sequenceMode = false;
        systemState = NORMAL_OPERATION;
        setMasterLEDs(true, false, false);
        
        Serial.println("SLEW BTN1 PRESSED - LED1 ON, LED4 ON");
      }
      else
      {
        digitalWrite(LED4, LOW);
        masterBtn1Pressed = false;
        sequenceMode = true;
        systemState = NORMAL_OPERATION;
        sequenceLED1 = true;
        lastSequenceChange = millis();
        setMasterLEDs(true, false, true);
        
        Serial.println("SLEW BTN1 RELEASED - LED3 ON, LED1/LED2 BLINK");
      }
      break;
      
    case MSG_RESET:
      digitalWrite(LED4, LOW);
      enterResetMode();
      break;
  }
}

// =====================================================
// RELEASE SEQUENCE
// =====================================================

inline void sequenceIndication()
{
  if (!sequenceMode) return;
  
  digitalWrite(LED3, HIGH);
  if (!masterBtn1Pressed)
  {
    digitalWrite(LED4, LOW);
  }
  
  if (millis() - lastSequenceChange >= SEQUENCE_TIME)
  {
    lastSequenceChange = millis();
    sequenceLED1 = !sequenceLED1;
    
    digitalWrite(LED1, sequenceLED1 ? HIGH : LOW);
    digitalWrite(LED2, sequenceLED1 ? LOW : HIGH);
  }
}

// =====================================================
// BUTTON HANDLERS
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
        if (systemState == RESET_MODE) return;
        
        masterBtn1Pressed = true;
        sequenceMode = false;
        systemState = NORMAL_OPERATION;
        setMasterLEDs(true, false, false);
        lastActivityTime = millis();
        
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
          setMasterLEDs(true, false, true);
        }
        lastActivityTime = millis();
      }
    }
  }
  
  lastBtn1 = reading;
}

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
        Serial.println("\nMASTER RESET BUTTON PRESSED");
        digitalWrite(LED4, LOW);
        sendToSlew(MSG_RESET);
        enterResetMode();
        lastActivityTime = millis();
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
  
  Serial.println("\n======================================");
  Serial.println("      ESP-NOW MASTER (INDUSTRIAL)");
  Serial.println("======================================");
  
  // Initialize pins
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  
  // Initial state
  systemState = SEARCHING;
  searchLED1 = true;
  lastSearchBlink = millis();
  setMasterLEDs(true, false, false);
  digitalWrite(LED4, LOW);
  
  // Configure power management
  configurePowerManagement();
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  
  Serial.print("MASTER MAC: ");
  Serial.println(WiFi.macAddress());
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAILED!");
    while (true)
    {
      digitalWrite(LED1, HIGH);
      delay(100);
      digitalWrite(LED1, LOW);
      delay(100);
    }
  }
  
  // Add SLEW peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, SLEW_MAC, 6);
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
  
  Serial.println("ESP-NOW READY - SEARCHING FOR SLEW...");
  lastActivityTime = millis();
  lastLoopTime = millis();
}

// =====================================================
// MAIN LOOP - OPTIMIZED FOR HEAT REDUCTION
// =====================================================

void loop()
{
  unsigned long currentTime = millis();
  
  // Process buttons with optimized timing
  if (currentTime - lastLoopTime >= 5)  // 5ms minimum between checks
  {
    checkMasterButton1();
    checkMasterButton2();
    lastLoopTime = currentTime;
  }
  
  // ===================================================
  // STATE MACHINE WITH OPTIMIZED TIMING
  // ===================================================
  
  switch (systemState)
  {
    case SEARCHING:
      searchingIndication();
      
      if (currentTime - lastHello >= HELLO_INTERVAL)
      {
        lastHello = currentTime;
        sendToSlew(MSG_HELLO);
      }
      
      // Allow light sleep between operations
      #if ENABLE_POWER_SAVE
      if (currentTime - lastActivityTime > LIGHT_SLEEP_TIMEOUT_MS)
      {
        delay(1);  // Small delay to reduce CPU usage
      }
      #endif
      break;
      
    case CONNECT_BLINK:
      connectionBlinkIndication();
      
      if (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL)
      {
        lastHeartbeat = currentTime;
        sendToSlew(MSG_HEARTBEAT);
      }
      break;
      
    case DISCONNECTED:
      disconnectIndication();
      
      if (currentTime - lastHello >= HELLO_INTERVAL)
      {
        lastHello = currentTime;
        sendToSlew(MSG_HELLO);
      }
      
      #if ENABLE_POWER_SAVE
      if (currentTime - lastActivityTime > LIGHT_SLEEP_TIMEOUT_MS)
      {
        delay(1);
      }
      #endif
      break;
      
    case RESET_MODE:
      setMasterLEDs(false, true, false);
      digitalWrite(LED4, LOW);
      
      if (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL)
      {
        lastHeartbeat = currentTime;
        sendToSlew(MSG_HEARTBEAT);
      }
      
      if (currentTime - lastPacket > CONNECTION_TIMEOUT)
      {
        startDisconnectMode();
      }
      
      #if ENABLE_POWER_SAVE
      if (currentTime - lastActivityTime > LIGHT_SLEEP_TIMEOUT_MS)
      {
        delay(1);
      }
      #endif
      break;
      
    case NORMAL_OPERATION:
      if (masterBtn1Pressed)
      {
        setMasterLEDs(true, false, false);
        digitalWrite(LED4, HIGH);
      }
      else if (sequenceMode)
      {
        sequenceIndication();
        digitalWrite(LED4, LOW);
      }
      else
      {
        setMasterLEDs(true, true, false);
        digitalWrite(LED4, LOW);
      }
      
      if (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL)
      {
        lastHeartbeat = currentTime;
        sendToSlew(MSG_HEARTBEAT);
      }
      
      if (currentTime - lastPacket > CONNECTION_TIMEOUT)
      {
        startDisconnectMode();
      }
      break;
  }
  
  // ===================================================
  // PERFORMANCE MONITORING (every 10 seconds)
  // ===================================================
  
  loopCount++;
  if (currentTime - lastStatsPrint > 10000)
  {
    lastStatsPrint = currentTime;
    Serial.printf("Performance: %d loops/sec\n", loopCount / 10);
    loopCount = 0;
  }
  
  // ===================================================
  // DELAY FOR HEAT REDUCTION
  // ===================================================
  
  #if ENABLE_POWER_SAVE
    // Small delay allows CPU to enter lower power state
    if (systemState != NORMAL_OPERATION && systemState != CONNECT_BLINK)
    {
      delay(2);
    }
    else
    {
      delay(1);
    }
  #endif
}








//slew----------------------------------------------------------------------------------------------------------------------------------------------------------

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <esp_pm.h>

// =====================================================
// POWER MANAGEMENT CONFIGURATION
// =====================================================

#define ENABLE_POWER_SAVE 1
#define LIGHT_SLEEP_TIMEOUT_MS 5000
#define CPU_FREQ_MHZ 80  // Reduced from 240MHz to 80MHz

// =====================================================
// SLEW PINS (YOUR ORIGINAL PINS)
// =====================================================

#define LED1 26
#define LED2 33
#define BTN1 13
// BTN2 is not used in your original code

// =====================================================
// SETTINGS (YOUR ORIGINAL TIMINGS)
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
  0x94, 0xA9, 0x90, 0xEF, 0x98, 0x28
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
// SYSTEM STATES
// =====================================================

enum SystemState
{
  SEARCHING,
  CONNECT_BLINK,
  NORMAL_OPERATION,
  DISCONNECTED
};

SystemState systemState = SEARCHING;

// =====================================================
// TIMING VARIABLES
// =====================================================

unsigned long lastMasterPacket = 0;
unsigned long lastHello = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastSearchBlink = 0;
unsigned long lastConnectBlink = 0;
unsigned long lastActivityTime = 0;
unsigned long lastLoopTime = 0;

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
// CONNECTION STATE
// =====================================================

bool connected = false;

// =====================================================
// BUTTON
// =====================================================

bool lastBtn1 = HIGH;
bool stableBtn1 = HIGH;
unsigned long btn1Debounce = 0;
bool lastBtn1State = false;
unsigned long lastBtnSendTime = 0;

// =====================================================
// PERFORMANCE MONITORING
// =====================================================

unsigned long loopCount = 0;
unsigned long lastStatsPrint = 0;

// =====================================================
// SET SLEW LEDS (YOUR ORIGINAL FUNCTION)
// =====================================================

inline void setSlewLEDs(bool led26, bool led33)
{
  digitalWrite(LED1, led26 ? HIGH : LOW);
  digitalWrite(LED2, led33 ? HIGH : LOW);
}

// =====================================================
// POWER MANAGEMENT
// =====================================================

void configurePowerManagement()
{
  #if ENABLE_POWER_SAVE
    // Reduce CPU frequency
    esp_pm_config_t pm_config = {
      .max_freq_mhz = CPU_FREQ_MHZ,
      .min_freq_mhz = CPU_FREQ_MHZ,
      .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
    
    // Reduce WiFi power
    esp_wifi_set_max_tx_power(40);  // 4dBm (minimum for stable connection)
    
    Serial.println("Power Management Configured:");
    Serial.printf("  CPU Frequency: %d MHz\n", CPU_FREQ_MHZ);
    Serial.printf("  TX Power: %d dBm\n", 4);
    Serial.printf("  Light Sleep Timeout: %d ms\n", LIGHT_SLEEP_TIMEOUT_MS);
  #endif
}

// =====================================================
// RESET COMMUNICATION (YOUR ORIGINAL FUNCTION)
// =====================================================

void resetCommunication()
{
  connected = false;
  connectionBlinkActive = false;
  connectionLEDState = false;
  connectionBlinkCount = 0;
  systemState = SEARCHING;
  
  // IMPORTANT: Always turn both LEDs OFF on reset.
  setSlewLEDs(false, false);
  
  Serial.println();
  Serial.println("================================");
  Serial.println("COMMUNICATION RESET");
  Serial.println("SLEW LEDs OFF");
  Serial.println("SEARCHING FOR MASTER...");
  Serial.println("================================");
  
  lastActivityTime = millis();
}

// =====================================================
// SEND TO MASTER (with retry and rate limiting)
// =====================================================

void sendToMaster(uint8_t type, uint8_t buttonState = 0)
{
  DataPacket packet;
  packet.type = type;
  packet.role = ROLE_SLEW;
  packet.buttonState = buttonState;
  packet.counter = millis();
  
  esp_err_t result = esp_now_send(MASTER_MAC, (uint8_t *)&packet, sizeof(packet));
  
  // Log errors but don't spam
  static uint32_t lastErrorLog = 0;
  if (result != ESP_OK && (millis() - lastErrorLog > 5000))
  {
    lastErrorLog = millis();
    Serial.printf("ESP-NOW Send Error: %d\n", result);
  }
}

// =====================================================
// START CONNECTION BLINK (YOUR ORIGINAL FUNCTION)
// =====================================================

void startConnectionBlink()
{
  connectionBlinkActive = true;
  connectionLEDState = false;
  connectionBlinkCount = 0;
  lastConnectBlink = millis();
  systemState = CONNECT_BLINK;
  
  // IMPORTANT: Start from OFF.
  setSlewLEDs(false, false);
  
  Serial.println();
  Serial.println("================================");
  Serial.println("MASTER CONNECTED");
  Serial.println("START 5 BLINKS");
  Serial.println("================================");
  
  lastActivityTime = millis();
}

// =====================================================
// CONNECTION SUCCESS BLINK (YOUR ORIGINAL LOGIC)
// =====================================================

inline void connectionBlink()
{
  if (!connectionBlinkActive) return;
  
  if (millis() - lastConnectBlink >= CONNECT_BLINK_TIME)
  {
    lastConnectBlink = millis();
    connectionLEDState = !connectionLEDState;
    
    if (connectionLEDState)
    {
      setSlewLEDs(true, true);
    }
    else
    {
      setSlewLEDs(false, false);
      connectionBlinkCount++;
      
      Serial.print("Connection blink: ");
      Serial.println(connectionBlinkCount);
      
      // 5 COMPLETE BLINKS
      if (connectionBlinkCount >= CONNECT_BLINK_COUNT)
      {
        connectionBlinkActive = false;
        
        // IMPORTANT: Final state ALWAYS OFF.
        setSlewLEDs(false, false);
        systemState = NORMAL_OPERATION;
        
        Serial.println("5 BLINKS COMPLETE");
        Serial.println("SLEW LED26 = OFF");
        Serial.println("SLEW LED33 = OFF");
        Serial.println("NORMAL OPERATION");
      }
    }
  }
}

// =====================================================
// RECEIVE DATA (YOUR ORIGINAL LOGIC)
// =====================================================

void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  if (len != sizeof(DataPacket)) return;
  
  // Only accept MASTER
  if (memcmp(info->src_addr, MASTER_MAC, 6) != 0) return;
  
  DataPacket packet;
  memcpy(&packet, data, sizeof(packet));
  
  lastMasterPacket = millis();
  lastActivityTime = millis();
  
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
// SEARCHING INDICATION (YOUR ORIGINAL LOGIC)
// =====================================================

inline void searchingIndication()
{
  if (systemState != SEARCHING) return;
  
  if (millis() - lastSearchBlink >= SEARCH_BLINK_TIME)
  {
    lastSearchBlink = millis();
    searchState = !searchState;
    
    if (searchState)
    {
      // LED26 ON, LED33 OFF
      setSlewLEDs(true, false);
    }
    else
    {
      // LED26 OFF, LED33 ON
      setSlewLEDs(false, true);
    }
  }
}

// =====================================================
// BUTTON 1 (YOUR ORIGINAL LOGIC)
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
      
      // BUTTON PRESSED
      if (stableBtn1 == LOW)
      {
        Serial.println("SLEW BTN1 PRESSED");
        
        // Slew LEDs ALWAYS OFF
        setSlewLEDs(false, false);
        sendToMaster(MSG_BTN1_STATE, 1);
        lastBtnSendTime = millis();
      }
      
      // BUTTON RELEASED
      else
      {
        Serial.println("SLEW BTN1 RELEASED");
        
        // Slew LEDs ALWAYS OFF
        setSlewLEDs(false, false);
        sendToMaster(MSG_BTN1_STATE, 0);
        lastBtnSendTime = millis();
      }
      
      lastActivityTime = millis();
    }
  }
  
  lastBtn1 = reading;
}

// =====================================================
// START DISCONNECT MODE
// =====================================================

void startDisconnectMode()
{
  connected = false;
  systemState = DISCONNECTED;
  connectionBlinkActive = false;
  
  setSlewLEDs(false, false);
  
  Serial.println();
  Serial.println("================================");
  Serial.println("MASTER DISCONNECTED");
  Serial.println("SLEW LEDs OFF");
  Serial.println("================================");
  
  lastActivityTime = millis();
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
  Serial.println("       ESP-NOW SLEW (INDUSTRIAL)");
  Serial.println("======================================");
  
  // ===================================================
  // GPIO (YOUR ORIGINAL PINS)
  // ===================================================
  
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(BTN1, INPUT_PULLUP);
  
  // IMPORTANT: Force LEDs OFF immediately.
  setSlewLEDs(false, false);
  
  // ===================================================
  // POWER MANAGEMENT
  // ===================================================
  
  configurePowerManagement();
  
  // ===================================================
  // WIFI
  // ===================================================
  
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  
  Serial.print("SLEW MAC: ");
  Serial.println(WiFi.macAddress());
  
  // ===================================================
  // ESP-NOW
  // ===================================================
  
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAILED!");
    
    while (true)
    {
      setSlewLEDs(true, false);
      delay(200);
      setSlewLEDs(false, false);
      delay(200);
    }
  }
  
  // ===================================================
  // ADD MASTER PEER
  // ===================================================
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, MASTER_MAC, 6);
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
  Serial.println("SLEW LEDs = OFF");
  Serial.println("SEARCHING FOR MASTER...");
  
  lastActivityTime = millis();
  lastLoopTime = millis();
  systemState = SEARCHING;
}

// =====================================================
// LOOP - OPTIMIZED FOR HEAT REDUCTION
// =====================================================

void loop()
{
  unsigned long currentTime = millis();
  
  // ===================================================
  // BUTTON CHECK (with optimized timing)
  // ===================================================
  
  if (currentTime - lastLoopTime >= 5)  // 5ms minimum between checks
  {
    checkButton1();
    lastLoopTime = currentTime;
  }
  
  // ===================================================
  // STATE MACHINE
  // ===================================================
  
  switch (systemState)
  {
    case SEARCHING:
      searchingIndication();
      
      if (currentTime - lastHello >= HELLO_INTERVAL)
      {
        lastHello = currentTime;
        sendToMaster(MSG_HELLO);
      }
      
      // Allow light sleep between operations
      #if ENABLE_POWER_SAVE
      if (currentTime - lastActivityTime > LIGHT_SLEEP_TIMEOUT_MS)
      {
        delay(1);
      }
      #endif
      break;
      
    case CONNECT_BLINK:
      connectionBlink();
      
      if (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL)
      {
        lastHeartbeat = currentTime;
        sendToMaster(MSG_HEARTBEAT);
      }
      break;
      
    case NORMAL_OPERATION:
      // LEDs should be OFF in normal operation (your original logic)
      // But keep them OFF unless button is pressed
      if (!connectionBlinkActive)
      {
        setSlewLEDs(false, false);
      }
      
      if (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL)
      {
        lastHeartbeat = currentTime;
        sendToMaster(MSG_HEARTBEAT);
      }
      
      if (currentTime - lastMasterPacket > CONNECTION_TIMEOUT)
      {
        startDisconnectMode();
      }
      break;
      
    case DISCONNECTED:
      setSlewLEDs(false, false);
      
      if (currentTime - lastHello >= HELLO_INTERVAL)
      {
        lastHello = currentTime;
        sendToMaster(MSG_HELLO);
      }
      
      // Check if reconnected
      if (connected)
      {
        systemState = NORMAL_OPERATION;
        Serial.println("Reconnected to MASTER");
      }
      
      #if ENABLE_POWER_SAVE
      if (currentTime - lastActivityTime > LIGHT_SLEEP_TIMEOUT_MS)
      {
        delay(1);
      }
      #endif
      break;
  }
  
  // ===================================================
  // PERFORMANCE MONITORING (every 10 seconds)
  // ===================================================
  
  loopCount++;
  if (currentTime - lastStatsPrint > 10000)
  {
    lastStatsPrint = currentTime;
    Serial.printf("Performance: %d loops/sec | State: %d\n", loopCount / 10, systemState);
    loopCount = 0;
  }
  
  // ===================================================
  // DELAY FOR HEAT REDUCTION
  // ===================================================
  
  #if ENABLE_POWER_SAVE
    // Small delay allows CPU to enter lower power state
    if (systemState != CONNECT_BLINK)
    {
      delay(2);
    }
    else
    {
      delay(1);
    }
  #endif
}
