#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RH_RF95.h>
#include <MS5x.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

// ---------------- LoRa ----------------
#define RFM95_CS   8
#define RFM95_INT  3
#define RFM95_RST  9
#define SD_CS      10

#define RF95_FREQ  915.0
#define TELEMETRY_INTERVAL_MS 8000
#define SHERPA_BAUD 115200

// Yellowstone routes the SHERPA UART through SAMD21 PB22/PB23:
//   PB22 / package pin 37 / Arduino D30 = Serial5 TX
//   PB23 / package pin 38 / Arduino D31 = Serial5 RX
// The Feather M0 core's default Serial1 is PA10/PA11, not this connector.
#define SHERPA_SERIAL Serial5

RH_RF95 rf95(RFM95_CS, RFM95_INT);

// ---------------- GPS ----------------
SFE_UBLOX_GNSS gps;
MS5x pressureSensor(&Wire);

// The portable u-blox navigation model is limited to 12 km. A balloon needs
// an airborne model: AIRBORNE2g supports the expected HAB dynamics and a
// 50 km altitude envelope, which covers a 100,000 ft (30.5 km) flight.
const dynModel GPS_DYNAMIC_MODEL = DYN_MODEL_AIRBORNE2g;

// ---------------- Payload ----------------
const uint16_t PAYLOAD_MAGIC = 0x5953; // "YS"
const uint8_t PAYLOAD_VERSION = 3;
const uint8_t FLAG_GPS_VALID = 0x01;
const uint8_t FLAG_PRESSURE_VALID = 0x02;
const uint16_t COMMAND_MAGIC = 0x5943; // "YC"
const uint8_t COMMAND_VERSION = 1;
const uint8_t COMMAND_TYPE_CUTDOWN = 1;
const uint8_t COMMAND_TYPE_PING = 2;
const uint16_t ACK_MAGIC = 0x5941; // "YA"
const uint8_t ACK_VERSION = 2;
const uint8_t ACK_STAGE_AIRBORNE = 1;
const uint8_t ACK_STAGE_SHERPA = 2;
const uint8_t ACK_STAGE_ICARUS = 3;
const uint8_t ACK_REPEAT_COUNT = 3;
const uint16_t ACK_REPEAT_INTERVAL_MS = 700;
const uint16_t ACK_TURNAROUND_DELAY_MS = 5000;
char AIRBORNE_LOG_FILE[] = "AIRLOG3.CSV";
char AIRBORNE_EVENT_FILE[] = "AIREVT.CSV";

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

struct CommandPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t command;
  uint32_t sequence;
  uint16_t checksum;
} __attribute__((packed));

struct AckPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t command;
  uint8_t stage;
  uint8_t status;
  uint32_t sequence;
  uint32_t detail;
  uint16_t checksum;
} __attribute__((packed));

Payload txData;

unsigned long lastSend = 0;
uint32_t txPacketCount = 0;
uint32_t lastCutdownSequence = 0;
uint32_t lastPingSequence = 0;
char sherpaLine[64];
uint8_t sherpaLineLen = 0;
struct QueuedAck {
  AckPacket packet;
  uint8_t repeatsRemaining;
};
const uint8_t ACK_QUEUE_SIZE = 8;
QueuedAck ackQueue[ACK_QUEUE_SIZE];
uint8_t ackQueueHead = 0;
uint8_t ackQueueTail = 0;
uint8_t ackQueueCount = 0;
uint32_t nextAckTxMs = 0;
uint8_t consecutiveGpsMisses = 0;
uint8_t lastGpsSecond = 255;
bool sdReady = false;
bool pressureReady = false;
uint8_t pressureAddress = 0;
float lastPressureAltitudeM = 0.0f;
uint32_t lastPressureSampleMs = 0;

void print2Digits(Print &out, uint8_t value) {
  if (value < 10) out.print("0");
  out.print(value);
}

int16_t clampInt16(float value) {
  if (value > 32767.0f) return 32767;
  if (value < -32768.0f) return -32768;
  return static_cast<int16_t>(lroundf(value));
}

// 1976 Standard Atmosphere, inverted through 32 km. The 100,000 ft target is
// 30.48 km, so it remains within this range.
float pressureAltitudeMeters(float pressurePa) {
  const float P0 = 101325.0f;
  const float P11 = 22632.06f;
  const float P20 = 5474.889f;
  const float P32 = 868.019f;
  const float G_OVER_R = 0.03416319f;

  if (pressurePa >= P11) {
    return 44330.77f * (1.0f - powf(pressurePa / P0, 0.1902632f));
  }
  if (pressurePa >= P20) {
    return 11000.0f - (216.65f / G_OVER_R) * logf(pressurePa / P11);
  }
  if (pressurePa >= P32) {
    return 20000.0f + (216.65f / 0.001f) *
        (powf(pressurePa / P20, -0.001f / G_OVER_R) - 1.0f);
  }
  return 32000.0f;
}

void readPressureTelemetry(Payload &data) {
  data.pressurePa = 0;
  data.pressureAltM = 0;
  data.verticalSpeedCms = 0;
  data.pressureTempCentiC = 0;

  if (!pressureReady) return;

  pressureSensor.checkUpdates();
  if (!pressureSensor.isReady()) return;

  float pressurePa = pressureSensor.GetPres();
  float temperatureC = pressureSensor.GetTemp();
  if (pressurePa < 800.0f || pressurePa > 120000.0f) return;

  float altitudeM = pressureAltitudeMeters(pressurePa);
  uint32_t sampleMs = millis();
  float verticalSpeedCms = 0.0f;
  if (lastPressureSampleMs != 0) {
    uint32_t elapsedMs = sampleMs - lastPressureSampleMs;
    if (elapsedMs > 0) {
      verticalSpeedCms = (altitudeM - lastPressureAltitudeM) * 100000.0f / elapsedMs;
    }
  }

  lastPressureAltitudeM = altitudeM;
  lastPressureSampleMs = sampleMs;
  data.pressurePa = static_cast<uint32_t>(lroundf(pressurePa));
  data.pressureAltM = clampInt16(altitudeM);
  data.verticalSpeedCms = clampInt16(verticalSpeedCms);
  data.pressureTempCentiC = clampInt16(temperatureC * 100.0f);
  data.flags |= FLAG_PRESSURE_VALID;
}

bool tryPressureSensorAddress(uint8_t addr) {
  pressureSensor.setI2Caddr(addr);
  int status = pressureSensor.connect();
  if (status == 0) {
    pressureAddress = addr;
    return true;
  }

  Serial.print("No barometer at 0x");
  Serial.print(addr, HEX);
  Serial.print(" connect code = ");
  Serial.println(status);
  return false;
}

void printTelemetryCsv(Print &out, const Payload &data, uint32_t packetNumber) {
  float lat = data.lat / 10000000.0f;
  float lon = data.lon / 10000000.0f;
  float heading = data.heading / 10.0f;
  float gps_alt_ft = data.gpsAltM * 3.28084f;
  float speed_mps = data.speedCms / 100.0f;
  float speed_mph = speed_mps * 2.23694f;
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

const char *commandName(uint8_t command) {
  if (command == COMMAND_TYPE_CUTDOWN) return "CUTDOWN";
  if (command == COMMAND_TYPE_PING) return "PING";
  return "UNKNOWN";
}

void logAirborneEvent(const char *event, uint8_t command, uint32_t sequence,
                      const char *stage, uint32_t value) {
  if (!sdReady) return;
  File logFile = SD.open(AIRBORNE_EVENT_FILE, FILE_WRITE);
  if (!logFile) {
    Serial.println("Airborne event log open failed");
    return;
  }
  logFile.print(millis());
  logFile.print(",");
  logFile.print(event);
  logFile.print(",");
  logFile.print(commandName(command));
  logFile.print(",");
  logFile.print(sequence);
  logFile.print(",");
  logFile.print(stage);
  logFile.print(",");
  logFile.println(value);
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
      logFile.println("lat,lon,gps_alt_m,gps_alt_ft,ground_speed_mps,ground_speed_mph,vertical_speed_mps,vertical_speed_fpm,heading_deg,pressure_pa,pressure_hpa,pressure_inhg,pressure_alt_m,pressure_alt_ft,pressure_temp_c,pressure_temp_f,packet,date_utc,time_utc,fix_type,sats,gps_valid,pressure_valid");
      logFile.close();
    }
  }

  if (!SD.exists(AIRBORNE_EVENT_FILE)) {
    File eventFile = SD.open(AIRBORNE_EVENT_FILE, FILE_WRITE);
    if (eventFile) {
      eventFile.println("millis,event,command,sequence,stage,value");
      eventFile.close();
    }
  }

  Serial.println("Airborne SD logging ready");
}

void configureGps() {
  gps.setI2COutput(COM_TYPE_UBX);

  // Apply this on every boot/recovery instead of saving it to GPS flash.
  // That guarantees the flight-safe model even if another tool changes the
  // receiver configuration, without adding unnecessary flash write cycles.
  if (!gps.setDynamicModel(GPS_DYNAMIC_MODEL)) {
    Serial.println("GPS airborne dynamic-model setup failed");
    return;
  }

  if (gps.getDynamicModel() != GPS_DYNAMIC_MODEL) {
    Serial.println("GPS airborne dynamic-model verification failed");
    return;
  }

  Serial.println("GPS dynamic model: AIRBORNE2g (50 km envelope)");
}

void initPressureSensor() {
  pressureReady = tryPressureSensorAddress(0x76) || tryPressureSensorAddress(0x77);
  if (!pressureReady) {
    Serial.println("MS5x pressure sensor not found at 0x76 or 0x77; pressure telemetry disabled");
    return;
  }

  Serial.print("MS5x pressure sensor ready at 0x");
  Serial.println(pressureAddress, HEX);
}

void configureLoRaLongRange() {
  rf95.setFrequency(RF95_FREQ);
  rf95.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
  rf95.setPreambleLength(12);
  rf95.setTxPower(23, false); // max power on PA_BOOST modules
}

uint16_t commandChecksum(const CommandPacket &command) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&command);
  uint16_t checksum = 0xA5A5;
  for (size_t i = 0; i < sizeof(CommandPacket) - sizeof(command.checksum); i++) {
    checksum = static_cast<uint16_t>((checksum << 5) | (checksum >> 11));
    checksum ^= bytes[i];
  }
  return checksum;
}

uint16_t ackChecksum(const AckPacket &packet) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&packet);
  uint16_t checksum = 0xB4B4;
  for (size_t i = 0; i < sizeof(AckPacket) - sizeof(packet.checksum); i++) {
    checksum = static_cast<uint16_t>((checksum << 6) | (checksum >> 10));
    checksum ^= bytes[i];
  }
  return checksum;
}

bool validCommandPacket(const CommandPacket &command) {
  return command.magic == COMMAND_MAGIC &&
      command.version == COMMAND_VERSION &&
      (command.command == COMMAND_TYPE_CUTDOWN || command.command == COMMAND_TYPE_PING) &&
      command.checksum == commandChecksum(command);
}

uint8_t parseCommandName(const char *name) {
  if (strcmp(name, "CUTDOWN") == 0) return COMMAND_TYPE_CUTDOWN;
  if (strcmp(name, "PING") == 0) return COMMAND_TYPE_PING;
  return 0;
}

bool parseHopAckLine(const char *line, uint8_t &command, uint8_t &stage,
                     uint32_t &sequence, uint8_t &status, uint32_t &detail) {
  char source[9] = {};
  char commandText[9] = {};
  unsigned long parsedSequence = 0;
  unsigned int parsedStatus = 0;
  unsigned long parsedDetail = 0;
  if (sscanf(line, "%8[^,],ACK,%8[^,],%lu,status,%u,detail,%lu",
             source, commandText, &parsedSequence, &parsedStatus, &parsedDetail) != 5) return false;
  command = parseCommandName(commandText);
  if (command == 0 || parsedSequence == 0 || parsedStatus > 255) return false;
  if (strcmp(source, "SHERPA") == 0) stage = ACK_STAGE_SHERPA;
  else if (strcmp(source, "ICARUS") == 0) stage = ACK_STAGE_ICARUS;
  else return false;
  sequence = static_cast<uint32_t>(parsedSequence);
  status = static_cast<uint8_t>(parsedStatus);
  detail = static_cast<uint32_t>(parsedDetail);
  return true;
}

void queueGroundAck(uint8_t command, uint8_t stage, uint32_t sequence,
                    uint8_t status, uint32_t detail) {
  if (ackQueueCount >= ACK_QUEUE_SIZE) {
    Serial.println("ACK queue full");
    logAirborneEvent("ACK_QUEUE_FULL", command, sequence, "AIRBORNE", stage);
    return;
  }
  bool queueWasEmpty = ackQueueCount == 0;
  QueuedAck &queued = ackQueue[ackQueueTail];
  queued.packet.magic = ACK_MAGIC;
  queued.packet.version = ACK_VERSION;
  queued.packet.command = command;
  queued.packet.stage = stage;
  queued.packet.status = status;
  queued.packet.sequence = sequence;
  queued.packet.detail = detail;
  queued.packet.checksum = ackChecksum(queued.packet);
  queued.repeatsRemaining = ACK_REPEAT_COUNT;
  ackQueueTail = (ackQueueTail + 1) % ACK_QUEUE_SIZE;
  ackQueueCount++;
  if (queueWasEmpty) nextAckTxMs = millis() + ACK_TURNAROUND_DELAY_MS;

  Serial.print("ACK queued: command=");
  Serial.print(commandName(command));
  Serial.print(" stage=");
  Serial.print(stage);
  Serial.print(" seq=");
  Serial.println(sequence);
}

void processSherpaLine(const char *line) {
  Serial.print("SHERPA UART RX: ");
  Serial.println(line);

  uint8_t command = 0;
  uint8_t stage = 0;
  uint8_t status = 0;
  uint32_t sequence = 0;
  uint32_t detail = 0;
  if (parseHopAckLine(line, command, stage, sequence, status, detail)) {
    logAirborneEvent("ACK_RX", command, sequence,
                     stage == ACK_STAGE_SHERPA ? "SHERPA" : "ICARUS", status);
    queueGroundAck(command, stage, sequence, status, detail);
  }
}

void pollSherpaUart() {
  while (SHERPA_SERIAL.available()) {
    char ch = static_cast<char>(SHERPA_SERIAL.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      sherpaLine[sherpaLineLen] = '\0';
      if (sherpaLineLen > 0) processSherpaLine(sherpaLine);
      sherpaLineLen = 0;
      continue;
    }

    if (sherpaLineLen < sizeof(sherpaLine) - 1) {
      sherpaLine[sherpaLineLen++] = ch;
    } else {
      sherpaLineLen = 0;
      Serial.println("SHERPA UART line too long");
    }
  }
}

void serviceIcarusAckTx() {
  if (ackQueueCount == 0) return;
  uint32_t now = millis();
  if (now - nextAckTxMs >= 0x80000000UL) return;

  QueuedAck &queued = ackQueue[ackQueueHead];
  rf95.send(reinterpret_cast<uint8_t *>(&queued.packet), sizeof(queued.packet));
  rf95.waitPacketSent(2000);
  queued.repeatsRemaining--;
  nextAckTxMs = now + ACK_REPEAT_INTERVAL_MS;

  Serial.print("ACK sent to ground: seq=");
  Serial.print(queued.packet.sequence);
  Serial.print(" remaining=");
  Serial.println(queued.repeatsRemaining);

  if (queued.repeatsRemaining == 0) {
    logAirborneEvent("ACK_TX", queued.packet.command, queued.packet.sequence,
                     "GROUND", queued.packet.stage);
    ackQueueHead = (ackQueueHead + 1) % ACK_QUEUE_SIZE;
    ackQueueCount--;
  }
}

void forwardCommandToSherpa(uint8_t command, uint32_t sequence) {
  SHERPA_SERIAL.print("SHERPA,");
  SHERPA_SERIAL.print(commandName(command));
  SHERPA_SERIAL.print(",");
  SHERPA_SERIAL.println(sequence);

  Serial.print("Command forwarded to SHERPA: ");
  Serial.print(commandName(command));
  Serial.print(" seq=");
  Serial.println(sequence);
  logAirborneEvent("UART_TX", command, sequence, "SHERPA", 1);
}

void pollGroundCommand() {
  if (!rf95.available()) return;

  uint8_t buf[sizeof(CommandPacket)];
  uint8_t len = sizeof(buf);
  if (!rf95.recv(buf, &len)) return;
  if (len != sizeof(CommandPacket)) return;

  CommandPacket command;
  memcpy(&command, buf, sizeof(command));
  if (!validCommandPacket(command)) return;
  uint32_t &lastSequence = command.command == COMMAND_TYPE_CUTDOWN
      ? lastCutdownSequence : lastPingSequence;
  if (command.sequence == lastSequence) return;

  lastSequence = command.sequence;
  logAirborneEvent("LORA_RX", command.command, command.sequence, "GROUND", rf95.lastRssi());
  queueGroundAck(command.command, ACK_STAGE_AIRBORNE, command.sequence, 1, rf95.lastRssi());
  forwardCommandToSherpa(command.command, command.sequence);
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
  SHERPA_SERIAL.begin(SHERPA_BAUD);

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
  initPressureSensor();

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
  Serial.println("SHERPA UART ready");
}

// ---------------- Loop ----------------
void loop() {
  pollSherpaUart();
  serviceIcarusAckTx();
  pollGroundCommand();

  if (millis() - lastSend < TELEMETRY_INTERVAL_MS) {
    pollSherpaUart();
    serviceIcarusAckTx();
    return;
  }

  bool freshGps = gps.getPVT(250);
  uint8_t fixType = gps.getFixType();

  // Build payload
  txData.magic = PAYLOAD_MAGIC;
  txData.version = PAYLOAD_VERSION;
  bool gpsValid = (fixType >= 3);
  txData.flags = gpsValid ? FLAG_GPS_VALID : 0;
  txData.lat = gpsValid ? gps.getLatitude() : 0;
  txData.lon = gpsValid ? gps.getLongitude() : 0;
  txData.gpsAltM = gpsValid ? (gps.getAltitude() / 1000) : 0;      // mm -> meters
  txData.speedCms = gpsValid ? (gps.getGroundSpeed() / 10) : 0;    // mm/s -> cm/s
  txData.heading = gpsValid ? (gps.getHeading() / 10000) : 0;      // deg * 1e-5 -> tenths deg
  txData.year = gps.getYear();
  txData.month = gps.getMonth();
  txData.day = gps.getDay();
  txData.hour = gps.getHour();
  txData.minute = gps.getMinute();
  txData.second = gps.getSecond();
  txData.fixType = fixType;
  txData.sats = gps.getSIV();
  readPressureTelemetry(txData);

  recoverGpsIfNeeded(freshGps, txData.second);

  // Send packet
  rf95.send((uint8_t *)&txData, sizeof(txData));
  rf95.waitPacketSent();
  txPacketCount++;
  lastSend = millis();
  pollGroundCommand();
  logAirborneTelemetry(txData, txPacketCount);

  // Debug
  Serial.print("Sent: ");
  Serial.print(sizeof(txData));
  Serial.print(" bytes | Alt: ");
  Serial.print(txData.gpsAltM);
  Serial.print(" m | Speed: ");
  Serial.print(txData.speedCms);
  Serial.print(" cm/s | Heading: ");
  Serial.print(txData.heading / 10.0);
  Serial.print(" deg | Pressure: ");
  Serial.print(txData.pressurePa);
  Serial.print(" Pa | Pressure altitude: ");
  Serial.print(txData.pressureAltM);
  Serial.print(" m | Vertical speed: ");
  Serial.print(txData.verticalSpeedCms / 100.0f);
  Serial.print(" m/s | Fix: ");
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
