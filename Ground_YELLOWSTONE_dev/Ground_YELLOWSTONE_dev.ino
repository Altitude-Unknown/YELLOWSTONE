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
const uint16_t COMMAND_MAGIC = 0x5943; // "YC"
const uint8_t COMMAND_VERSION = 1;
const uint8_t COMMAND_TYPE_CUTDOWN = 1;
const uint8_t COMMAND_TYPE_PING = 2;
const uint16_t ACK_MAGIC = 0x5941; // "YA"
const uint8_t ACK_VERSION = 2;
const uint8_t ACK_STAGE_AIRBORNE = 1;
const uint8_t ACK_STAGE_SHERPA = 2;
const uint8_t ACK_STAGE_ICARUS = 3;
const uint8_t COMMAND_REPEAT_COUNT = 3;
const uint16_t COMMAND_REPEAT_DELAY_MS = 250;
const uint16_t COMMAND_TX_TIMEOUT_MS = 5000;
char GROUND_LOG_FILE[] = "GNDLOG.CSV";
char GROUND_EVENT_FILE[] = "GNDEVT.CSV";

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

Payload rxData;
uint32_t packetCount = 0;
uint32_t badPacketCount = 0;
uint32_t commandSequence = 0;
char serialCommandLine[40];
uint8_t serialCommandLen = 0;
bool sdReady = false;

const char *commandName(uint8_t command) {
  if (command == COMMAND_TYPE_CUTDOWN) return "CUTDOWN";
  if (command == COMMAND_TYPE_PING) return "PING";
  return "UNKNOWN";
}

const char *stageName(uint8_t stage) {
  if (stage == ACK_STAGE_AIRBORNE) return "AIRBORNE";
  if (stage == ACK_STAGE_SHERPA) return "SHERPA";
  if (stage == ACK_STAGE_ICARUS) return "ICARUS";
  return "UNKNOWN";
}

void logGroundEvent(const char *event, uint8_t command, uint32_t sequence,
                    const char *stage, uint32_t value) {
  if (!sdReady) return;
  File logFile = SD.open(GROUND_EVENT_FILE, FILE_WRITE);
  if (!logFile) {
    Serial.println("STATUS,GROUND_EVENT_LOG_FAILED");
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

bool validAckPacket(const AckPacket &packet) {
  return packet.magic == ACK_MAGIC &&
      packet.version == ACK_VERSION &&
      (packet.command == COMMAND_TYPE_CUTDOWN || packet.command == COMMAND_TYPE_PING) &&
      packet.stage >= ACK_STAGE_AIRBORNE && packet.stage <= ACK_STAGE_ICARUS &&
      packet.sequence != 0 &&
      packet.checksum == ackChecksum(packet);
}

void sendCommand(uint8_t commandType) {
  CommandPacket command;
  command.magic = COMMAND_MAGIC;
  command.version = COMMAND_VERSION;
  command.command = commandType;
  uint32_t uptimeSequence = millis();
  if (uptimeSequence > commandSequence) {
    commandSequence = uptimeSequence;
  } else {
    commandSequence++;
  }
  if (commandSequence == 0) commandSequence = 1;
  command.sequence = commandSequence;
  command.checksum = commandChecksum(command);
  logGroundEvent("TX_REQUEST", commandType, command.sequence, "GROUND", 0);

  uint8_t sentCount = 0;
  uint8_t timeoutCount = 0;
  for (uint8_t i = 0; i < COMMAND_REPEAT_COUNT; i++) {
    if (!rf95.send(reinterpret_cast<uint8_t *>(&command), sizeof(command))) {
      timeoutCount++;
      rf95.setModeRx();
      delay(COMMAND_REPEAT_DELAY_MS);
      continue;
    }

    if (rf95.waitPacketSent(COMMAND_TX_TIMEOUT_MS)) {
      sentCount++;
    } else {
      timeoutCount++;
      rf95.setModeRx();
    }
    delay(COMMAND_REPEAT_DELAY_MS);
  }

  Serial.print("STATUS,COMMAND_SENT,");
  Serial.print(commandName(commandType));
  Serial.print(",");
  Serial.print(command.sequence);
  Serial.print(",sent,");
  Serial.print(sentCount);
  Serial.print(",timeouts,");
  Serial.println(timeoutCount);
  logGroundEvent("TX_COMPLETE", commandType, command.sequence, "GROUND", sentCount);
}

void processSerialCommand(const char *line) {
  if (strcmp(line, "CMD,CUTDOWN") == 0) {
    sendCommand(COMMAND_TYPE_CUTDOWN);
    return;
  }
  if (strcmp(line, "CMD,PING") == 0) {
    sendCommand(COMMAND_TYPE_PING);
    return;
  }

  Serial.print("STATUS,UNKNOWN_COMMAND,");
  Serial.println(line);
}

void readSerialCommands() {
  while (Serial.available()) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      serialCommandLine[serialCommandLen] = '\0';
      if (serialCommandLen > 0) processSerialCommand(serialCommandLine);
      serialCommandLen = 0;
      continue;
    }

    if (serialCommandLen < sizeof(serialCommandLine) - 1) {
      serialCommandLine[serialCommandLen++] = ch;
    } else {
      serialCommandLen = 0;
      Serial.println("STATUS,COMMAND_TOO_LONG");
    }
  }
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

  if (!SD.exists(GROUND_EVENT_FILE)) {
    File eventFile = SD.open(GROUND_EVENT_FILE, FILE_WRITE);
    if (eventFile) {
      eventFile.println("millis,event,command,sequence,stage,value");
      eventFile.close();
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
  readSerialCommands();

  if (!rf95.available()) return;

  uint8_t buf[sizeof(Payload) > sizeof(AckPacket) ? sizeof(Payload) : sizeof(AckPacket)];
  uint8_t len = sizeof(buf);

  if (!rf95.recv(buf, &len)) return;

  if (len == sizeof(AckPacket)) {
    AckPacket ack;
    memcpy(&ack, buf, sizeof(ack));
    if (!validAckPacket(ack)) {
      badPacketCount++;
      return;
    }

    Serial.print("STATUS,COMMAND_ACK,");
    Serial.print(commandName(ack.command));
    Serial.print(",");
    Serial.print(ack.sequence);
    Serial.print(",stage,");
    Serial.print(stageName(ack.stage));
    Serial.print(",status,");
    Serial.print(ack.status);
    Serial.print(",detail,");
    Serial.print(ack.detail);
    Serial.print(",rssi,");
    Serial.println(rf95.lastRssi());
    logGroundEvent("ACK_RX", ack.command, ack.sequence, stageName(ack.stage), ack.status);
    return;
  }

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
