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
const uint8_t PAYLOAD_VERSION = 3;
const uint8_t FLAG_GPS_VALID = 0x01;
const uint8_t FLAG_PRESSURE_VALID = 0x02;
char GROUND_LOG_FILE[] = "GNDLOG.CSV";

struct Payload {
  uint16_t magic;
  uint8_t version;
  uint8_t flags;
  int32_t lat;        // degrees * 10^7
  int32_t lon;        // degrees * 10^7
  int16_t gpsAltM;    // meters MSL
  uint16_t speedCms;  // ground speed in cm/s
  uint16_t heading;   // tenths of a degree
  uint32_t pressurePa;       // pascals
  int16_t pressureAltM;      // ISA pressure altitude, meters
  int16_t verticalSpeedCms;  // pressure-altitude vertical speed, cm/s
  int16_t pressureTempCentiC; // sensor temperature, centi-degrees C
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
  float gps_alt_ft = data.gpsAltM * 3.28084f;
  float speed_mps = data.speedCms / 100.0f;   // cm/s -> m/s
  float speed_mph = speed_mps * 2.23694f;     // m/s -> mph
  float vertical_speed_mps = data.verticalSpeedCms / 100.0f;
  float vertical_speed_fpm = vertical_speed_mps * 196.8504f;
  float pressure_hpa = data.pressurePa / 100.0f;
  float pressure_inhg = data.pressurePa * 0.0002952998f;
  float pressure_alt_ft = data.pressureAltM * 3.28084f;
  float pressure_temp_c = data.pressureTempCentiC / 100.0f;
  float pressure_temp_f = pressure_temp_c * 1.8f + 32.0f;

  out.print(lat, 7);
  out.print(",");
  out.print(lon, 7);
  out.print(",");
  out.print(data.gpsAltM);
  out.print(",");
  out.print(gps_alt_ft, 1);
  out.print(",");
  out.print(speed_mps, 2);
  out.print(",");
  out.print(speed_mph, 2);
  out.print(",");
  out.print(vertical_speed_mps, 2);
  out.print(",");
  out.print(vertical_speed_fpm, 0);
  out.print(",");
  out.print(heading, 1);
  out.print(",");
  out.print(data.pressurePa);
  out.print(",");
  out.print(pressure_hpa, 2);
  out.print(",");
  out.print(pressure_inhg, 4);
  out.print(",");
  out.print(data.pressureAltM);
  out.print(",");
  out.print(pressure_alt_ft, 1);
  out.print(",");
  out.print(pressure_temp_c, 2);
  out.print(",");
  out.print(pressure_temp_f, 2);
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
  out.print((data.flags & FLAG_GPS_VALID) ? "1" : "0");
  out.print(",");
  out.println((data.flags & FLAG_PRESSURE_VALID) ? "1" : "0");
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
      logFile.println("lat,lon,gps_alt_m,gps_alt_ft,ground_speed_mps,ground_speed_mph,vertical_speed_mps,vertical_speed_fpm,heading_deg,pressure_pa,pressure_hpa,pressure_inhg,pressure_alt_m,pressure_alt_ft,pressure_temp_c,pressure_temp_f,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid,pressure_valid");
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
  Serial.println("lat,lon,gps_alt_m,gps_alt_ft,ground_speed_mps,ground_speed_mph,vertical_speed_mps,vertical_speed_fpm,heading_deg,pressure_pa,pressure_hpa,pressure_inhg,pressure_alt_m,pressure_alt_ft,pressure_temp_c,pressure_temp_f,rssi,packet,date_utc,time_utc,fix_type,sats,gps_valid,pressure_valid");
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
