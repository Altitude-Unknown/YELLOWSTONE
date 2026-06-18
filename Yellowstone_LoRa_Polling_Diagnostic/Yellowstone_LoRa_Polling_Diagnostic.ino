#include <SPI.h>
#include <RH_RF95.h>

#define RFM95_CS   8
#define RFM95_INT  3
#define RFM95_RST  9
#define SD_CS      10
#define RF95_FREQ  915.0

// SX1276/RFM95 registers
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_PKT_SNR_VALUE        0x19
#define REG_PKT_RSSI_VALUE       0x1A
#define REG_MODEM_CONFIG_1       0x1D
#define REG_MODEM_CONFIG_2       0x1E
#define REG_MODEM_CONFIG_3       0x26
#define REG_RSSI_WIDEBAND        0x2C
#define REG_VERSION              0x42
#define REG_DIO_MAPPING_1        0x40

#define IRQ_RX_TIMEOUT           0x80
#define IRQ_RX_DONE              0x40
#define IRQ_PAYLOAD_CRC_ERROR    0x20
#define IRQ_VALID_HEADER         0x10

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

uint32_t pollRxDone = 0;
uint32_t validPackets = 0;
uint32_t badPackets = 0;
uint32_t crcErrors = 0;
uint8_t lastIrq = 0;

uint8_t spiReadReg(uint8_t reg) {
  digitalWrite(RFM95_CS, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t value = SPI.transfer(0);
  digitalWrite(RFM95_CS, HIGH);
  return value;
}

void spiWriteReg(uint8_t reg, uint8_t value) {
  digitalWrite(RFM95_CS, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(value);
  digitalWrite(RFM95_CS, HIGH);
}

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

void printHex2(uint8_t value) {
  if (value < 16) Serial.print("0");
  Serial.print(value, HEX);
}

void printRegisters() {
  Serial.print("RegVersion=0x");
  printHex2(spiReadReg(REG_VERSION));
  Serial.print(" OpMode=0x");
  printHex2(spiReadReg(REG_OP_MODE));
  Serial.print(" Modem1=0x");
  printHex2(spiReadReg(REG_MODEM_CONFIG_1));
  Serial.print(" Modem2=0x");
  printHex2(spiReadReg(REG_MODEM_CONFIG_2));
  Serial.print(" Modem3=0x");
  printHex2(spiReadReg(REG_MODEM_CONFIG_3));
  Serial.print(" DioMap1=0x");
  printHex2(spiReadReg(REG_DIO_MAPPING_1));
  Serial.println();
}

void enterContinuousRx() {
  spiWriteReg(REG_IRQ_FLAGS, 0xFF);
  // LoRa + RX continuous. RadioHead has already configured modem/frequency.
  spiWriteReg(REG_OP_MODE, 0x85);
}

void readPolledPacket(uint8_t irq) {
  pollRxDone++;
  lastIrq = irq;

  if (irq & IRQ_PAYLOAD_CRC_ERROR) {
    crcErrors++;
    spiWriteReg(REG_IRQ_FLAGS, 0xFF);
    enterContinuousRx();
    return;
  }

  uint8_t len = spiReadReg(REG_RX_NB_BYTES);
  uint8_t currentAddr = spiReadReg(REG_FIFO_RX_CURRENT_ADDR);
  spiWriteReg(REG_FIFO_ADDR_PTR, currentAddr);

  uint8_t buf[sizeof(Payload)];
  uint8_t readLen = min((uint8_t)sizeof(buf), len);
  for (uint8_t i = 0; i < readLen; ++i) {
    buf[i] = spiReadReg(REG_FIFO);
  }

  int pktRssi = (int)spiReadReg(REG_PKT_RSSI_VALUE) - 157;
  int8_t rawSnr = (int8_t)spiReadReg(REG_PKT_SNR_VALUE);
  float snr = rawSnr / 4.0f;

  Serial.print("RxDone len=");
  Serial.print(len);
  Serial.print(" irq=0x");
  printHex2(irq);
  Serial.print(" rssi=");
  Serial.print(pktRssi);
  Serial.print(" snr=");
  Serial.print(snr, 1);

  if (len == sizeof(Payload)) {
    Payload data;
    memcpy(&data, buf, sizeof(data));
    if (data.magic == PAYLOAD_MAGIC && data.version == PAYLOAD_VERSION) {
      validPackets++;
      Serial.print(" VALID lat=");
      Serial.print(data.lat / 10000000.0f, 7);
      Serial.print(" lon=");
      Serial.print(data.lon / 10000000.0f, 7);
      Serial.print(" sats=");
      Serial.print(data.sats);
    } else {
      badPackets++;
      Serial.print(" BAD magic=0x");
      Serial.print(data.magic, HEX);
      Serial.print(" ver=");
      Serial.print(data.version);
    }
  } else {
    badPackets++;
    Serial.print(" BAD_LEN");
  }
  Serial.println();

  spiWriteReg(REG_IRQ_FLAGS, 0xFF);
  enterContinuousRx();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RFM95_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  pinMode(RFM95_INT, INPUT);
  digitalWrite(RFM95_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 4000) {
    delay(10);
  }

  Serial.println();
  Serial.println("Yellowstone LoRa polling diagnostic");
  Serial.println("Ignores RadioHead available()/DIO0 interrupt state");

  SPI.begin();
  resetLoRa();

  if (!rf95.init()) {
    Serial.println("RadioHead init: FAIL");
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }
  Serial.println("RadioHead init: PASS");

  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("setFrequency: FAIL");
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(250);
    }
  }
  configureLoRaLongRange();
  printRegisters();
  enterContinuousRx();
  Serial.println("Polling raw IRQ flags for RxDone...");
}

void loop() {
  static unsigned long lastHeartbeat = 0;
  static bool ledState = false;

  uint8_t irq = spiReadReg(REG_IRQ_FLAGS);
  if (irq & IRQ_RX_DONE) {
    readPolledPacket(irq);
  } else if (irq & IRQ_PAYLOAD_CRC_ERROR) {
    crcErrors++;
    spiWriteReg(REG_IRQ_FLAGS, IRQ_PAYLOAD_CRC_ERROR);
  } else if (irq & IRQ_RX_TIMEOUT) {
    spiWriteReg(REG_IRQ_FLAGS, IRQ_RX_TIMEOUT);
  }

  unsigned long now = millis();
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);

    Serial.print("heartbeat d3=");
    Serial.print(digitalRead(RFM95_INT));
    Serial.print(" irq=0x");
    printHex2(spiReadReg(REG_IRQ_FLAGS));
    Serial.print(" wbRssiRaw=");
    Serial.print(spiReadReg(REG_RSSI_WIDEBAND));
    Serial.print(" rxDone=");
    Serial.print(pollRxDone);
    Serial.print(" valid=");
    Serial.print(validPackets);
    Serial.print(" bad=");
    Serial.print(badPackets);
    Serial.print(" crc=");
    Serial.println(crcErrors);
  }
}
