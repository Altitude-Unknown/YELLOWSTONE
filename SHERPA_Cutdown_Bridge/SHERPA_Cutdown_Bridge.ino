#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// SHERPA is an ESP32-C3 board plugged into Airborne YELLOWSTONE.
// Airborne sends: SHERPA,CUTDOWN,<sequence>
// SHERPA forwards a compact ESP-NOW packet for ICARUS.

#define YELLOWSTONE_BAUD 115200
#define YELLOWSTONE_RX_PIN 20  // ESP32-C3 RXD, connected to SHERPA RX-YELLOWSTONE
#define YELLOWSTONE_TX_PIN 21  // ESP32-C3 TXD, connected to SHERPA TX-YELLOWSTONE
#define LED_ARM_PIN 5
#define LED_WIFI_PIN 7
#define LED_FAULT_PIN 10

const uint32_t ICARUS_MAGIC = 0x49535543UL; // "ICUS"
const uint8_t ICARUS_VERSION = 1;
const uint8_t ICARUS_COMMAND_CUTDOWN = 1;
const uint8_t ESPNOW_CHANNEL = 1;
const uint8_t ESPNOW_REPEATS = 5;
const uint16_t ESPNOW_REPEAT_DELAY_MS = 100;

HardwareSerial YellowstoneSerial(1);

const uint8_t broadcastPeer[ESP_NOW_ETH_ALEN] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

struct IcarusCommandPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t command;
  uint32_t sequence;
  uint16_t checksum;
} __attribute__((packed));

char commandLine[64];
uint8_t commandLineLen = 0;
char usbLine[64];
uint8_t usbLineLen = 0;
uint32_t lastForwardedSequence = 0;
uint32_t lastHeartbeatMs = 0;
volatile uint32_t espNowSendOk = 0;
volatile uint32_t espNowSendFail = 0;

uint16_t packetChecksum(const IcarusCommandPacket &packet) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&packet);
  uint16_t checksum = 0x5A5A;
  for (size_t i = 0; i < sizeof(IcarusCommandPacket) - sizeof(packet.checksum); i++) {
    checksum = static_cast<uint16_t>((checksum << 3) | (checksum >> 13));
    checksum ^= bytes[i];
  }
  return checksum;
}

void setLed(uint8_t pin, bool on) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, on ? HIGH : LOW);
}

void blinkLed(uint8_t pin, uint8_t count, uint16_t ms) {
  for (uint8_t i = 0; i < count; i++) {
    setLed(pin, true);
    delay(ms);
    setLed(pin, false);
    delay(ms);
  }
}

void onEspNowSend(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  (void)info;
  if (status == ESP_NOW_SEND_SUCCESS) {
    espNowSendOk++;
  } else {
    espNowSendFail++;
  }
}

bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("SHERPA,ERROR,ESPNOW_INIT_FAILED");
    setLed(LED_FAULT_PIN, true);
    return false;
  }

  esp_now_register_send_cb(onEspNowSend);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastPeer, sizeof(broadcastPeer));
  peer.ifidx = WIFI_IF_STA;
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("SHERPA,ERROR,ESPNOW_ADD_PEER_FAILED");
    setLed(LED_FAULT_PIN, true);
    return false;
  }

  setLed(LED_WIFI_PIN, true);
  Serial.print("SHERPA,READY,MAC,");
  Serial.println(WiFi.macAddress());
  return true;
}

void sendCutdownToIcarus(uint32_t sequence) {
  if (sequence == 0 || sequence == lastForwardedSequence) {
    Serial.print("SHERPA,IGNORED_DUPLICATE,");
    Serial.println(sequence);
    return;
  }

  IcarusCommandPacket packet;
  packet.magic = ICARUS_MAGIC;
  packet.version = ICARUS_VERSION;
  packet.command = ICARUS_COMMAND_CUTDOWN;
  packet.sequence = sequence;
  packet.checksum = packetChecksum(packet);

  lastForwardedSequence = sequence;
  uint32_t okBefore = espNowSendOk;
  uint32_t failBefore = espNowSendFail;

  for (uint8_t i = 0; i < ESPNOW_REPEATS; i++) {
    esp_err_t result = esp_now_send(broadcastPeer, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
    if (result != ESP_OK) {
      Serial.print("SHERPA,ERROR,ESPNOW_SEND_RESULT,");
      Serial.println(result);
    }
    delay(ESPNOW_REPEAT_DELAY_MS);
  }

  blinkLed(LED_ARM_PIN, 2, 50);

  Serial.print("SHERPA,CUTDOWN_FORWARDED,");
  Serial.print(sequence);
  Serial.print(",ok_delta,");
  Serial.print(espNowSendOk - okBefore);
  Serial.print(",fail_delta,");
  Serial.println(espNowSendFail - failBefore);
}

bool parseCutdownLine(const char *line, uint32_t &sequence) {
  const char prefix[] = "SHERPA,CUTDOWN,";
  if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) return false;

  char *end = nullptr;
  unsigned long parsed = strtoul(line + sizeof(prefix) - 1, &end, 10);
  if (end == line + sizeof(prefix) - 1 || *end != '\0') return false;
  sequence = static_cast<uint32_t>(parsed);
  return sequence != 0;
}

void processYellowstoneLine(const char *line) {
  Serial.print("SHERPA,UART_RX,");
  Serial.println(line);

  uint32_t sequence = 0;
  if (parseCutdownLine(line, sequence)) {
    sendCutdownToIcarus(sequence);
    return;
  }

  Serial.print("SHERPA,IGNORED_UART,");
  Serial.println(line);
}

void readYellowstoneUart() {
  while (YellowstoneSerial.available()) {
    char ch = static_cast<char>(YellowstoneSerial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      commandLine[commandLineLen] = '\0';
      if (commandLineLen > 0) processYellowstoneLine(commandLine);
      commandLineLen = 0;
      continue;
    }

    if (commandLineLen < sizeof(commandLine) - 1) {
      commandLine[commandLineLen++] = ch;
    } else {
      commandLineLen = 0;
      Serial.println("SHERPA,ERROR,UART_LINE_TOO_LONG");
      setLed(LED_FAULT_PIN, true);
    }
  }
}

void processUsbLine(const char *line) {
  uint32_t sequence = 0;
  if (parseCutdownLine(line, sequence)) {
    Serial.print("SHERPA,USB_TEST_RX,");
    Serial.println(line);
    sendCutdownToIcarus(sequence);
    return;
  }

  if (strcmp(line, "SHERPA,STATUS") == 0) {
    Serial.print("SHERPA,STATUS,mac,");
    Serial.print(WiFi.macAddress());
    Serial.print(",last_sequence,");
    Serial.print(lastForwardedSequence);
    Serial.print(",espnow_ok,");
    Serial.print(espNowSendOk);
    Serial.print(",espnow_fail,");
    Serial.println(espNowSendFail);
    return;
  }

  Serial.print("SHERPA,IGNORED_USB,");
  Serial.println(line);
}

void readUsbDebug() {
  while (Serial.available()) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      usbLine[usbLineLen] = '\0';
      if (usbLineLen > 0) processUsbLine(usbLine);
      usbLineLen = 0;
      continue;
    }

    if (usbLineLen < sizeof(usbLine) - 1) {
      usbLine[usbLineLen++] = ch;
    } else {
      usbLineLen = 0;
      Serial.println("SHERPA,ERROR,USB_LINE_TOO_LONG");
      setLed(LED_FAULT_PIN, true);
    }
  }
}

void printHeartbeat() {
  uint32_t now = millis();
  if (now - lastHeartbeatMs < 2000) return;
  lastHeartbeatMs = now;

  Serial.print("SHERPA,HEARTBEAT,ms,");
  Serial.print(now);
  Serial.print(",last_sequence,");
  Serial.print(lastForwardedSequence);
  Serial.print(",espnow_ok,");
  Serial.print(espNowSendOk);
  Serial.print(",espnow_fail,");
  Serial.println(espNowSendFail);
}

void setup() {
  setLed(LED_ARM_PIN, false);
  setLed(LED_WIFI_PIN, false);
  setLed(LED_FAULT_PIN, false);

  Serial.begin(115200);
  delay(1500);
  Serial.println("SHERPA,BOOT");

  YellowstoneSerial.begin(YELLOWSTONE_BAUD, SERIAL_8N1, YELLOWSTONE_RX_PIN, YELLOWSTONE_TX_PIN);
  Serial.println("SHERPA,YELLOWSTONE_UART_READY");

  initEspNow();
}

void loop() {
  readYellowstoneUart();
  readUsbDebug();
  printHeartbeat();
}
