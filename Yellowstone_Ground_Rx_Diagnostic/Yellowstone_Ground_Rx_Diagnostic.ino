#include <SPI.h>
#include <RH_RF95.h>

#define RFM95_CS   8
#define RFM95_INT  3
#define RFM95_RST  9
#define SD_CS      10
#define RF95_FREQ  915.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);

const uint16_t PAYLOAD_MAGIC = 0x5953; // "YS"
const uint8_t PAYLOAD_VERSION = 2;

struct Payload {
  uint16_t magic;
  uint8_t version;
  uint8_t flags;
  int32_t lat;
  int32_t lon;
  int16_t alt;
  uint16_t speed;
  uint16_t heading;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t fixType;
  uint8_t sats;
} __attribute__((packed));

uint32_t validPackets = 0;
uint32_t invalidPackets = 0;
uint32_t recvFailures = 0;

void resetLoRa() {
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
}

void configureLoRaLongRange() {
  rf95.setFrequency(RF95_FREQ);
  rf95.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
  rf95.setPreambleLength(12);
}

void printPayload(const Payload &data) {
  Serial.print("VALID packet=");
  Serial.print(validPackets);
  Serial.print(" rssi=");
  Serial.print(rf95.lastRssi());
  Serial.print(" lat=");
  Serial.print(data.lat / 10000000.0f, 7);
  Serial.print(" lon=");
  Serial.print(data.lon / 10000000.0f, 7);
  Serial.print(" alt=");
  Serial.print(data.alt);
  Serial.print(" fix=");
  Serial.print(data.fixType);
  Serial.print(" sats=");
  Serial.print(data.sats);
  Serial.print(" time=");
  Serial.print(data.hour);
  Serial.print(":");
  Serial.print(data.minute);
  Serial.print(":");
  Serial.println(data.second);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RFM95_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(RFM95_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 4000) {
    delay(10);
  }

  Serial.println();
  Serial.println("Yellowstone ground RX diagnostic");
  Serial.println("SD disabled for this test");

  resetLoRa();

  if (!rf95.init()) {
    Serial.println("LoRa init: FAIL");
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }

  Serial.println("LoRa init: PASS");

  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("LoRa setFrequency: FAIL");
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(250);
    }
  }

  configureLoRaLongRange();
  Serial.println("LoRa config: long-range 915.0 MHz");
  Serial.println("Waiting for packets...");
}

void loop() {
  static unsigned long lastHeartbeat = 0;
  static bool ledState = false;

  if (millis() - lastHeartbeat >= 1000) {
    lastHeartbeat = millis();
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
    Serial.print("heartbeat valid=");
    Serial.print(validPackets);
    Serial.print(" invalid=");
    Serial.print(invalidPackets);
    Serial.print(" recvFail=");
    Serial.println(recvFailures);
  }

  if (!rf95.available()) return;

  uint8_t buf[sizeof(Payload)];
  uint8_t len = sizeof(buf);
  if (!rf95.recv(buf, &len)) {
    recvFailures++;
    Serial.println("recv indicated available but recv failed");
    return;
  }

  if (len != sizeof(Payload)) {
    invalidPackets++;
    Serial.print("INVALID len=");
    Serial.print(len);
    Serial.print(" rssi=");
    Serial.println(rf95.lastRssi());
    return;
  }

  Payload data;
  memcpy(&data, buf, sizeof(data));

  if (data.magic != PAYLOAD_MAGIC || data.version != PAYLOAD_VERSION) {
    invalidPackets++;
    Serial.print("INVALID magic=0x");
    Serial.print(data.magic, HEX);
    Serial.print(" version=");
    Serial.print(data.version);
    Serial.print(" rssi=");
    Serial.println(rf95.lastRssi());
    return;
  }

  validPackets++;
  printPayload(data);
}
