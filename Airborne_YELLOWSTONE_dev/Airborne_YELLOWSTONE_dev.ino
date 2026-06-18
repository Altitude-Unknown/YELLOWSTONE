#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RH_RF95.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

// ---------------- LoRa ----------------
#define RFM95_CS   8
#define RFM95_INT  3
#define RFM95_RST  9
#define SD_CS      10

#define RF95_FREQ  915.0
#define TELEMETRY_INTERVAL_MS 2000

RH_RF95 rf95(RFM95_CS, RFM95_INT);

// ---------------- GPS ----------------
SFE_UBLOX_GNSS gps;

// ---------------- Payload ----------------
const uint16_t PAYLOAD_MAGIC = 0x5953; // "YS"
const uint8_t PAYLOAD_VERSION = 2;
char AIRBORNE_LOG_FILE[] = "AIRLOG.CSV";

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

Payload txData;

unsigned long lastSend = 0;
uint32_t txPacketCount = 0;
uint8_t consecutiveGpsMisses = 0;
uint8_t lastGpsSecond = 255;
bool sdReady = false;

void print2Digits(Print &out, uint8_t value) {
  if (value < 10) out.print("0");
  out.print(value);
}

void printTelemetryCsv(Print &out, const Payload &data, uint32_t packetNumber) {
  float lat = data.lat / 10000000.0f;
  float lon = data.lon / 10000000.0f;
  float heading = data.heading / 10.0f;
  float speed_mps = data.speed / 100.0f;
  float speed_mph = speed_mps * 2.23694f;

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

void logAirborneTelemetry(const Payload &data, uint32_t packetNumber) {
  if (!sdReady) return;

  File logFile = SD.open(AIRBORNE_LOG_FILE, FILE_WRITE);
  if (!logFile) {
    sdReady = false;
    Serial.println("Airborne SD log open failed");
    return;
  }

  printTelemetryCsv(logFile, data, packetNumber);
  logFile.close();
}

void initAirborneLog() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  sdReady = SD.begin(SD_CS);
  if (!sdReady) {
    Serial.println("Airborne SD not found; logging disabled");
    return;
  }

  if (!SD.exists(AIRBORNE_LOG_FILE)) {
    File logFile = SD.open(AIRBORNE_LOG_FILE, FILE_WRITE);
    if (logFile) {
      logFile.println("lat,lon,alt_m,speed_mps,speed_mph,heading_deg,packet,date_utc,time_utc,fix_type,sats,gps_valid");
      logFile.close();
    }
  }

  Serial.println("Airborne SD logging ready");
}

void configureGps() {
  gps.setI2COutput(COM_TYPE_UBX);
}

void configureLoRaLongRange() {
  rf95.setFrequency(RF95_FREQ);
  rf95.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
  rf95.setPreambleLength(12);
  rf95.setTxPower(23, false); // max power on PA_BOOST modules
}

void recoverGpsIfNeeded(bool freshGps, uint8_t gpsSecond) {
  bool gpsClockRunning = (gpsSecond != lastGpsSecond);
  lastGpsSecond = gpsSecond;

  if (freshGps || gpsClockRunning) {
    consecutiveGpsMisses = 0;
    return;
  }

  if (consecutiveGpsMisses < 255) consecutiveGpsMisses++;
  if (consecutiveGpsMisses < 5) return;

  Serial.println("GPS stale; attempting GPS recovery");
  if (gps.begin()) {
    configureGps();
    Serial.println("GPS recovery succeeded");
  } else {
    Serial.println("GPS recovery failed");
  }
  consecutiveGpsMisses = 0;
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  pinMode(RFM95_CS, OUTPUT);
  digitalWrite(RFM95_CS, HIGH);
  initAirborneLog();

  // GPS
  Wire.begin();
  if (!gps.begin()) {
    Serial.println("GPS not found");
    while (1);
  }

  configureGps();

  // LoRa reset
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  // LoRa init
  if (!rf95.init()) {
    Serial.println("LoRa init failed");
    while (1);
  }

  configureLoRaLongRange();

  Serial.println("LoRa ready: long-range mode");
}

// ---------------- Loop ----------------
void loop() {
  if (millis() - lastSend < TELEMETRY_INTERVAL_MS) return;
  lastSend = millis();

  bool freshGps = gps.getPVT(250);
  uint8_t fixType = gps.getFixType();

  // Build payload
  txData.magic = PAYLOAD_MAGIC;
  txData.version = PAYLOAD_VERSION;
  bool gpsValid = (fixType >= 3);
  txData.flags = gpsValid ? 0x01 : 0x00;
  txData.lat = gpsValid ? gps.getLatitude() : 0;
  txData.lon = gpsValid ? gps.getLongitude() : 0;
  txData.alt = gpsValid ? (gps.getAltitude() / 1000) : 0;          // mm -> meters
  txData.speed = gpsValid ? (gps.getGroundSpeed() / 10) : 0;       // mm/s -> cm/s
  txData.heading = gpsValid ? (gps.getHeading() / 10000) : 0;      // deg * 1e-5 -> tenths deg
  txData.year = gps.getYear();
  txData.month = gps.getMonth();
  txData.day = gps.getDay();
  txData.hour = gps.getHour();
  txData.minute = gps.getMinute();
  txData.second = gps.getSecond();
  txData.fixType = fixType;
  txData.sats = gps.getSIV();

  recoverGpsIfNeeded(freshGps, txData.second);

  // Send packet
  rf95.send((uint8_t *)&txData, sizeof(txData));
  rf95.waitPacketSent();
  txPacketCount++;
  logAirborneTelemetry(txData, txPacketCount);

  // Debug
  Serial.print("Sent: ");
  Serial.print(sizeof(txData));
  Serial.print(" bytes | Alt: ");
  Serial.print(txData.alt);
  Serial.print(" m | Speed: ");
  Serial.print(txData.speed);
  Serial.print(" cm/s | Heading: ");
  Serial.print(txData.heading / 10.0);
  Serial.print(" deg | Fix: ");
  Serial.print(fixType);
  Serial.print(" | Sats: ");
  Serial.print(txData.sats);
  Serial.print(" | UTC: ");
  Serial.print(txData.year);
  Serial.print("-");
  print2Digits(Serial, txData.month);
  Serial.print("-");
  print2Digits(Serial, txData.day);
  Serial.print(" ");
  print2Digits(Serial, txData.hour);
  Serial.print(":");
  print2Digits(Serial, txData.minute);
  Serial.print(":");
  print2Digits(Serial, txData.second);
  Serial.print(" | GPS fresh: ");
  Serial.print(freshGps ? "yes" : "no");
  Serial.println();
}
