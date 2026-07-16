#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ICARUS receives ESP-NOW cutdown commands from SHERPA and fires the cutdown
// MOSFET outputs for a bounded burn window.

#define CUTDOWN_MAIN_PIN 10
#define CUTDOWN_BACKUP_PIN 11
#define LED_PIN 22
#define TEST_PIN 23

const uint32_t ICARUS_MAGIC = 0x49535543UL; // "ICUS"
const uint8_t ICARUS_VERSION = 1;
const uint8_t ICARUS_COMMAND_CUTDOWN = 1;
const uint8_t ICARUS_COMMAND_PING = 2;
const uint8_t ICARUS_MESSAGE_ACK = 2;
const uint8_t ESPNOW_CHANNEL = 1;
const uint32_t CUTDOWN_BURN_MS = 8000;
const uint8_t ACK_REPEAT_COUNT = 5;
const uint16_t ACK_REPEAT_DELAY_MS = 60;

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

struct IcarusAckPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t message;
  uint8_t command;
  uint8_t status;
  uint32_t sequence;
  uint32_t detail;
  uint16_t checksum;
} __attribute__((packed));

volatile bool pendingCutdown = false;
volatile bool pendingAck = false;
volatile uint32_t pendingSequence = 0;
volatile uint32_t pendingAckSequence = 0;
volatile uint8_t pendingAckCommand = 0;
uint32_t lastAcceptedSequence = 0;
uint32_t lastPingSequence = 0;
uint32_t cutdownStartedMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t rxPacketCount = 0;
uint32_t acceptedPacketCount = 0;
uint32_t duplicatePacketCount = 0;
uint32_t rejectedPacketCount = 0;
bool cutdownActive = false;

uint16_t packetChecksum(const IcarusCommandPacket &packet) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&packet);
  uint16_t checksum = 0x5A5A;
  for (size_t i = 0; i < sizeof(IcarusCommandPacket) - sizeof(packet.checksum); i++) {
    checksum = static_cast<uint16_t>((checksum << 3) | (checksum >> 13));
    checksum ^= bytes[i];
  }
  return checksum;
}

uint16_t ackChecksum(const IcarusAckPacket &packet) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&packet);
  uint16_t checksum = 0x3C3C;
  for (size_t i = 0; i < sizeof(IcarusAckPacket) - sizeof(packet.checksum); i++) {
    checksum = static_cast<uint16_t>((checksum << 4) | (checksum >> 12));
    checksum ^= bytes[i];
  }
  return checksum;
}

void setLed(bool on) {
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

void setCutdownOutputs(bool on) {
  digitalWrite(CUTDOWN_MAIN_PIN, on ? HIGH : LOW);
  digitalWrite(CUTDOWN_BACKUP_PIN, on ? HIGH : LOW);
}

bool validPacket(const IcarusCommandPacket &packet) {
  return packet.magic == ICARUS_MAGIC &&
      packet.version == ICARUS_VERSION &&
      (packet.command == ICARUS_COMMAND_CUTDOWN || packet.command == ICARUS_COMMAND_PING) &&
      packet.sequence != 0 &&
      packet.checksum == packetChecksum(packet);
}

void printMac(const uint8_t *mac) {
  for (uint8_t i = 0; i < 6; i++) {
    if (i) Serial.print(":");
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
  }
}

void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  rxPacketCount++;

  if (len != sizeof(IcarusCommandPacket)) {
    rejectedPacketCount++;
    return;
  }

  IcarusCommandPacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (!validPacket(packet)) {
    rejectedPacketCount++;
    return;
  }

  uint32_t lastSequence = packet.command == ICARUS_COMMAND_CUTDOWN
      ? lastAcceptedSequence : lastPingSequence;
  if (packet.sequence == lastSequence ||
      (packet.command == ICARUS_COMMAND_CUTDOWN && packet.sequence == pendingSequence)) {
    duplicatePacketCount++;
    return;
  }

  if (packet.command == ICARUS_COMMAND_CUTDOWN) {
    lastAcceptedSequence = packet.sequence;
    pendingSequence = packet.sequence;
    pendingCutdown = true;
    acceptedPacketCount++;
  } else {
    lastPingSequence = packet.sequence;
  }
  pendingAckSequence = packet.sequence;
  pendingAckCommand = packet.command;
  pendingAck = true;

  Serial.print("ICARUS,COMMAND_RX,command,");
  Serial.print(packet.command == ICARUS_COMMAND_CUTDOWN ? "CUTDOWN" : "PING");
  Serial.print(",seq,");
  Serial.print(packet.sequence);
  Serial.print(",from,");
  printMac(info->src_addr);
  Serial.println();
}

bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ICARUS,ERROR,ESPNOW_INIT_FAILED");
    return false;
  }

  if (esp_now_register_recv_cb(onEspNowReceive) != ESP_OK) {
    Serial.println("ICARUS,ERROR,ESPNOW_RECV_CB_FAILED");
    return false;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastPeer, sizeof(broadcastPeer));
  peer.ifidx = WIFI_IF_STA;
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ICARUS,ERROR,ESPNOW_ADD_PEER_FAILED");
    return false;
  }

  Serial.print("ICARUS,READY,MAC,");
  Serial.println(WiFi.macAddress());
  return true;
}

void sendAckToSherpa(uint8_t command, uint32_t sequence) {
  IcarusAckPacket packet;
  packet.magic = ICARUS_MAGIC;
  packet.version = ICARUS_VERSION;
  packet.message = ICARUS_MESSAGE_ACK;
  packet.command = command;
  packet.status = 1;
  packet.sequence = sequence;
  packet.detail = command == ICARUS_COMMAND_CUTDOWN ? acceptedPacketCount : rxPacketCount;
  packet.checksum = ackChecksum(packet);

  for (uint8_t i = 0; i < ACK_REPEAT_COUNT; i++) {
    esp_now_send(broadcastPeer, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
    delay(ACK_REPEAT_DELAY_MS);
  }

  Serial.print("ICARUS,ACK_SENT,command,");
  Serial.print(command == ICARUS_COMMAND_CUTDOWN ? "CUTDOWN" : "PING");
  Serial.print(",seq,");
  Serial.print(sequence);
  Serial.print(",detail,");
  Serial.println(packet.detail);
}

void startCutdown(uint32_t sequence) {
  if (cutdownActive) {
    Serial.print("ICARUS,IGNORED_ALREADY_FIRING,seq,");
    Serial.println(sequence);
    return;
  }

  cutdownActive = true;
  cutdownStartedMs = millis();
  setCutdownOutputs(true);
  setLed(true);

  Serial.print("ICARUS,CUTDOWN_ACTIVE,seq,");
  Serial.print(sequence);
  Serial.print(",duration_ms,");
  Serial.println(CUTDOWN_BURN_MS);
}

void updateCutdownWindow() {
  if (!cutdownActive) return;
  if (millis() - cutdownStartedMs < CUTDOWN_BURN_MS) return;

  setCutdownOutputs(false);
  setLed(false);
  cutdownActive = false;

  Serial.print("ICARUS,CUTDOWN_COMPLETE,seq,");
  Serial.println(lastAcceptedSequence);
}

void printHeartbeat() {
  uint32_t now = millis();
  if (now - lastHeartbeatMs < 2000) return;
  lastHeartbeatMs = now;

  Serial.print("ICARUS,HEARTBEAT,ms,");
  Serial.print(now);
  Serial.print(",active,");
  Serial.print(cutdownActive ? 1 : 0);
  Serial.print(",last_sequence,");
  Serial.print(lastAcceptedSequence);
  Serial.print(",rx,");
  Serial.print(rxPacketCount);
  Serial.print(",accepted,");
  Serial.print(acceptedPacketCount);
  Serial.print(",duplicates,");
  Serial.print(duplicatePacketCount);
  Serial.print(",rejected,");
  Serial.println(rejectedPacketCount);
}

void setup() {
  pinMode(CUTDOWN_MAIN_PIN, OUTPUT);
  pinMode(CUTDOWN_BACKUP_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TEST_PIN, INPUT_PULLUP);
  setCutdownOutputs(false);
  setLed(false);

  Serial.begin(115200);
  delay(1500);
  Serial.println("ICARUS,BOOT");

  if (!initEspNow()) {
    Serial.println("ICARUS,FAULT,ESPNOW_NOT_READY");
  }
}

void loop() {
  if (pendingCutdown) {
    noInterrupts();
    uint32_t sequence = pendingSequence;
    pendingCutdown = false;
    interrupts();
    startCutdown(sequence);
  }

  if (pendingAck) {
    noInterrupts();
    uint32_t sequence = pendingAckSequence;
    uint8_t command = pendingAckCommand;
    pendingAck = false;
    interrupts();
    sendAckToSherpa(command, sequence);
  }

  updateCutdownWindow();
  printHeartbeat();
}
