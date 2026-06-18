#include <SPI.h>
#include <SD.h>
#include <RH_RF95.h>

#define RFM95_CS   8
#define RFM95_INT  3
#define RFM95_RST  9
#define SD_CS      10
#define RF95_FREQ  915.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);

const uint16_t PAYLOAD_MAGIC = 0x5953; // "YS"
const uint8_t PAYLOAD_VERSION = 2;
char GROUND_LOG_FILE[] = "GNDLOG.CSV";

struct Payload {
  uint16_t magic;
  uint8_t version;
  uint8_t flags;
  int32_t lat;        // degrees * 10^7
  int32_t lon;        // degrees * 10^7
  int16_t alt;        // meters
  uint16_t speed;     // ground speed in cm/s
  uint16_t heading;   // tenths of a degree
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t fixType;
  uint8_t sats;
} __attribute__((packed));

Payload rxData;
uint32_t packetCount = 0;
uint32_t badPacketCount = 0;
bool sdReady = false;

void configureLoRaLongRange() {
  rf95.setFrequency(RF95_FREQ);
  rf95.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
  rf95.setPreambleLength(12);
}

void print2Digits(Print &out, uint8_t value) {
  if (value < 10) out.print("0");
  out.print(value);
}

void printTelemetryCsv(Print &out, const Payload &data, int rssi, uint32_t packetNumber) {
  float lat = data.lat / 10000000.0f;
  float lon = data.lon / 10000000.0f;
  float heading = data.heading / 10.0f;
  float speed_mps = data.speed / 100.0f;      // cm/s -> m/s
  float speed_mph = speed_mps * 2.23694f;     // m/s -> mph

  out.print(lat, 7);
  out.print(",");
  out.print(lon, 7);
  out.print(",");
  out.print(data.alt);
  out.print(",");
  out.print(speed_mps, 2);
  out.print(",");
  out.print(speed_mph, 2);
  out.print(",");
  out.print(heading, 1);
  out.print(",");
  out.print(rssi);
  out.print(",");
  out.print(packetNumber);
  out.print(",");
  out.print(data.year);
  out.print("-");
  print2Digits(out, data.month);
  out.print("-");
  print2Digits(out, data.day);
  out.print(",");
  print2Digits(out, data.hour);
  out.print(":");
  print2Digits(out, data.minute);
  out.print(":");
  print2Digits(out, data.second);
  out.print(",");
  out.print(data.fixType);
  out.print(",");
  out.print(data.sats);
  out.print(",");
  out.println((data.flags & 0x01) ? "1" : "0");
}

void logGroundTelemetry(const Payload &data, int rssi, uint32_t packetNumber) {
  if (!sdReady) return;

  File logFile = SD.open(GROUND_LOG_FILE, FILE_WRITE);
  if (!logFile) {
    sdReady = false;
    Serial.println("Ground SD log open failed");
    return;
  }

  printTelemetryCsv(logFile, data, rssi, packetNumber);
  logFile.close();
}

void initGroundLog() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  sdReady = SD.begin(SD_CS);
  if (!sdReady) {
    Serial.println("Ground SD not found; logging disabled");
    return;
  }

  if (!SD.exists(GROUND_LOG_FILE)) {
    File logFile = SD.open(GROUND_LOG_FILE, FILE_WRITE);
    if (logFile) {
      logFile.println("lat,lon,alt_m,speed_mps,speed_mph,heading_deg,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid");
      logFile.close();
    }
  }

  Serial.println("Ground SD logging ready");
}

void setup() {
  Serial.begin(115200);

  pinMode(RFM95_CS, OUTPUT);
  digitalWrite(RFM95_CS, HIGH);
  initGroundLog();

  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  if (!rf95.init()) {
    Serial.println("LoRa init failed");
    while (1);
  }

  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("Set freq failed");
    while (1);
  }

  configureLoRaLongRange();

  Serial.println("Ground station ready");
  Serial.println("LoRa ready: long-range mode");
  Serial.println("lat,lon,alt_m,speed_mps,speed_mph,heading_deg,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid");
}

void loop() {
  if (!rf95.available()) return;

  uint8_t buf[sizeof(Payload)];
  uint8_t len = sizeof(buf);

  if (!rf95.recv(buf, &len)) return;
  if (len != sizeof(Payload)) {
    badPacketCount++;
    return;
  }

  memcpy(&rxData, buf, sizeof(rxData));

  if (rxData.magic != PAYLOAD_MAGIC || rxData.version != PAYLOAD_VERSION) {
    badPacketCount++;
    return;
  }

  packetCount++;

  int rssi = rf95.lastRssi();

  printTelemetryCsv(Serial, rxData, rssi, packetCount);
  logGroundTelemetry(rxData, rssi, packetCount);
}
