#include <Wire.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <math.h>

// ASCEND Logger v11 bench diagnostic. No radio or optional sensors required.
// User corrected fitted part identification to MS5611 (2026-10-26).
SFE_UBLOX_GNSS gps;
uint16_t prom[8];
uint8_t baroAddress = 0;
bool gpsReady = false, baroReady = false;
uint32_t lastSample = 0;
uint32_t lastSatReport = 0;

bool command(uint8_t cmd) {
  Wire.beginTransmission(baroAddress);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

bool readBytes(uint8_t cmd, uint8_t *data, uint8_t count) {
  if (!command(cmd) || Wire.requestFrom(baroAddress, count) != count) return false;
  for (uint8_t i = 0; i < count; i++) data[i] = Wire.read();
  return true;
}

uint8_t crc4() {
  uint16_t remainder = 0;
  for (uint8_t i = 0; i < 16; i++) {
    uint16_t word = prom[i / 2];
    if (i / 2 == 7) word &= 0xFF00;
    remainder ^= (i & 1) ? (word & 0xFF) : (word >> 8);
    for (uint8_t bit = 0; bit < 8; bit++) {
      remainder = (remainder & 0x8000) ? (remainder << 1) ^ 0x3000 : remainder << 1;
    }
  }
  return (remainder >> 12) & 0xF;
}

bool initBaro() {
  for (uint8_t address = 0x76; address <= 0x77; address++) {
    baroAddress = address;
    if (!command(0x1E)) continue;
    delay(5);
    bool ok = true;
    for (uint8_t i = 0; i < 8; i++) {
      uint8_t data[2];
      if (!readBytes(0xA0 + 2 * i, data, 2)) { ok = false; break; }
      prom[i] = (uint16_t(data[0]) << 8) | data[1];
    }
    if (!ok) continue;
    bool coefficientsValid = true;
    for (uint8_t i = 1; i <= 6; i++) {
      if (prom[i] == 0 || prom[i] == 0xFFFF) coefficientsValid = false;
    }
    Serial.print("BARO address=0x"); Serial.println(address, HEX);
    Serial.print("PROM:");
    for (uint8_t i = 0; i < 8; i++) { Serial.print(' '); Serial.print(prom[i], HEX); }
    Serial.println();
    Serial.print("BARO PROM CRC stored="); Serial.print(prom[7] & 0xF);
    Serial.print(" calculated="); Serial.println(crc4());
    return coefficientsValid && crc4() == (prom[7] & 0xF);
  }
  return false;
}

bool conversion(uint8_t cmd, uint32_t &result) {
  if (!command(cmd)) return false;
  delay(10); // OSR4096 conversion time.
  uint8_t data[3];
  if (!readBytes(0, data, 3)) return false;
  result = (uint32_t(data[0]) << 16) | (uint32_t(data[1]) << 8) | data[2];
  return result != 0 && result != 0xFFFFFF;
}

void printPressure(uint32_t d1, uint32_t d2, bool ms5611) {
  const double dt = double(d2) - double(prom[5]) * 256.0;
  double temp = 2000.0 + dt * prom[6] / 8388608.0;
  double off = double(prom[2]) * (ms5611 ? 65536.0 : 131072.0)
             + dt * prom[4] / (ms5611 ? 128.0 : 64.0);
  double sens = double(prom[1]) * (ms5611 ? 32768.0 : 65536.0)
              + dt * prom[3] / (ms5611 ? 256.0 : 128.0);
  if (temp < 2000.0) {
    const double delta = (temp - 2000.0) * (temp - 2000.0);
    double off2 = delta * (ms5611 ? 2.5 : 61.0 / 16.0);
    double sens2 = delta * (ms5611 ? 1.25 : 2.0);
    if (temp < -1500.0) {
      const double cold = (temp + 1500.0) * (temp + 1500.0);
      off2 += cold * (ms5611 ? 7.0 : 15.0);
      sens2 += cold * (ms5611 ? 5.5 : 8.0);
    }
    temp -= dt * dt / 2147483648.0;
    off -= off2;
    sens -= sens2;
  }
  const double pa = (double(d1) * sens / 2097152.0 - off) / 32768.0;
  Serial.print(ms5611 ? " MS5611_hPa=" : " MS5607_hPa=");
  Serial.print(pa / 100.0, 2);
  Serial.print(" temp_C="); Serial.print(temp / 100.0, 2);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("ASCEND Logger v11 GPS/BARO diagnostic");
  Serial.println("Pressure model: MS5611 (confirmed fitted component).");
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(10, OUTPUT); digitalWrite(10, HIGH); // SD deselected.
  Wire.begin();
  Wire.end();
  pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
  pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
  delay(2);
  Serial.print("SDA: "); Serial.println(digitalRead(PIN_WIRE_SDA) ? "HIGH" : "LOW");
  Serial.print("SCL: "); Serial.println(digitalRead(PIN_WIRE_SCL) ? "HIGH" : "LOW");
  Wire.begin(); Wire.setClock(100000);
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C found 0x"); Serial.println(address, HEX);
    }
  }
  baroReady = initBaro();
  Serial.println(baroReady ? "BARO PROM/CRC PASS" : "BARO initialization FAIL");
  gpsReady = gps.begin(Wire, 0x42, 1500);
  Serial.println(gpsReady ? "GPS UBX communication PASS" : "GPS UBX communication FAIL");
  if (gpsReady) {
    const uint32_t keys[] = {UBLOX_CFG_SIGNAL_GPS_ENA, UBLOX_CFG_SIGNAL_GPS_L1CA_ENA,
                            UBLOX_CFG_SIGNAL_GAL_ENA, UBLOX_CFG_SIGNAL_GLO_ENA};
    const char *names[] = {"GPS", "GPS_L1CA", "Galileo", "GLONASS"};
    for (uint8_t i = 0; i < 4; i++) {
      Serial.print("GPS config "); Serial.print(names[i]); Serial.print('=');
      if (gps.getVal(keys[i], VAL_LAYER_RAM, 700) == SFE_UBLOX_STATUS_DATA_RECEIVED) {
        Serial.println(gps.getVal8(keys[i], VAL_LAYER_RAM, 700));
      } else Serial.println("read unavailable");
    }
  }
}

void loop() {
  if (millis() - lastSample < 2000) return;
  lastSample = millis();
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  Serial.print("alive_ms="); Serial.println(millis());
  if (baroReady) {
    uint32_t d1, d2;
    if (conversion(0x48, d1) && conversion(0x58, d2)) {
      Serial.print("BARO ADC PASS D1="); Serial.print(d1);
      Serial.print(" D2="); Serial.print(d2);
      printPressure(d1, d2, true);
      Serial.println();
    } else Serial.println("BARO ADC FAIL");
  }
  if (gpsReady) {
    if (gps.getPVT(1200)) {
      Serial.print("GPS fresh NAV-PVT PASS fix_type="); Serial.print(gps.getFixType(0));
      Serial.print(" fix_ok="); Serial.print(gps.getGnssFixOk(0));
      Serial.print(" satellites_used="); Serial.print(gps.getSIV(0));
      Serial.print(" date_valid="); Serial.print(gps.getDateValid(0));
      Serial.print(" time_valid="); Serial.println(gps.getTimeValid(0));
    } else Serial.println("GPS NAV-PVT timeout");
    if (millis() - lastSatReport >= 10000) {
      lastSatReport = millis();
      if (gps.getNAVSAT(1200) && gps.packetUBXNAVSAT != nullptr) {
        const auto &sat = gps.packetUBXNAVSAT->data;
        uint16_t signals = 0;
        uint8_t strongest = 0;
        for (uint16_t i = 0; i < sat.header.numSvs; i++) {
          if (sat.blocks[i].cno > 0) signals++;
          if (sat.blocks[i].cno > strongest) strongest = sat.blocks[i].cno;
        }
        Serial.print("GPS NAV-SAT listed="); Serial.print(sat.header.numSvs);
        Serial.print(" with_signal="); Serial.print(signals);
        Serial.print(" strongest_dBHz="); Serial.println(strongest);
      } else Serial.println("GPS NAV-SAT unavailable");
      UBX_MON_RF_data_t rf;
      if (gps.getRFinformation(&rf, 1200)) {
        for (uint8_t i = 0; i < rf.header.nBlocks && i < UBX_MON_RF_MAX_BLOCKS; i++) {
          Serial.print("GPS RF block="); Serial.print(rf.blocks[i].blockId);
          Serial.print(" noise="); Serial.print(rf.blocks[i].noisePerMS);
          Serial.print(" agc="); Serial.print(rf.blocks[i].agcCnt);
          Serial.print(" jam_state="); Serial.print(rf.blocks[i].flags.bits.jammingState);
          Serial.print(" jam_indicator="); Serial.println(rf.blocks[i].jamInd);
        }
      } else Serial.println("GPS RF diagnostics unavailable");
    }
  }
}
