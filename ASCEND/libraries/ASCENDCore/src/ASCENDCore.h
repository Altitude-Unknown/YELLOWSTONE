#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RH_RF95.h>
#include <math.h>

namespace ascend {
constexpr uint8_t GPS_VALID=1, UTC_VALID=2, PRESSURE_VALID=4, PM_VALID=8, EXTRA_VALID=16;
constexpr uint32_t SAMPLE_MS=1000, TELEMETRY_MS=1000;
// Version 1 wire format: packed little-endian integers and IEEE754 float32.
// Both endpoints target SAMD21. Change version when changing the layout.
struct __attribute__((packed)) Sample {
  uint16_t magic=0x4153;
  uint8_t version=1, flags=0;
  uint16_t node=1, session=0;
  uint32_t sequence=0, uptime=0;
  int32_t latitude=0, longitude=0, altitudeMm=0;
  float pressurePa=0, barometerTempC=0;
  uint16_t pm1=0, pm25=0, pm10=0;
  uint16_t extraType=0;
  float extraValue=0;
  uint16_t year=0;
  uint8_t month=0, day=0, hour=0, minute=0, second=0, sats=0;
};
static_assert(sizeof(Sample)==56, "Wire layout changed");
const char HEADER[]="project,platform,node_id,session_id,sequence,uptime_ms,timestamp_utc,latitude,longitude,altitude_msl_m,pressure_hpa,barometer_temperature_c,pm1_ugm3,pm25_ugm3,pm10_ugm3,extra_sensor_type,extra_value,satellites,qc_flag";

inline void csv(Print &out, const Sample &s) {
  out.print("ASCEND,UAS,"); out.print(s.node); out.print(',');
  out.print(s.session); out.print(','); out.print(s.sequence); out.print(',');
  out.print(s.uptime); out.print(',');
  if (s.flags & UTC_VALID) {
    char utc[25]; snprintf(utc,sizeof(utc),"%04u-%02u-%02uT%02u:%02u:%02uZ",s.year,s.month,s.day,s.hour,s.minute,s.second);
    out.print(utc);
  }
  out.print(','); if(s.flags & GPS_VALID) out.print(s.latitude/1e7,7);
  out.print(','); if(s.flags & GPS_VALID) out.print(s.longitude/1e7,7);
  out.print(','); if(s.flags & GPS_VALID) out.print(s.altitudeMm/1000.0,3);
  out.print(','); if(s.flags & PRESSURE_VALID) out.print(s.pressurePa/100.0,2);
  out.print(','); if(s.flags & PRESSURE_VALID) out.print(s.barometerTempC,2);
  out.print(','); if(s.flags & PM_VALID) out.print(s.pm1);
  out.print(','); if(s.flags & PM_VALID) out.print(s.pm25);
  out.print(','); if(s.flags & PM_VALID) out.print(s.pm10);
  out.print(','); if(s.flags & EXTRA_VALID) out.print(s.extraType);
  out.print(','); if(s.flags & EXTRA_VALID) out.print(s.extraValue,4);
  out.print(','); out.print(s.sats); out.print(',');
  if(!(s.flags & UTC_VALID)) out.print("NO_UTC;");
  if(!(s.flags & GPS_VALID)) out.print("NO_FIX;");
  if(!(s.flags & PRESSURE_VALID)) out.print("NO_PRESSURE;");
  if(!(s.flags & PM_VALID)) out.print("NO_PM;");
  out.println();
}

inline void prepareSPI() {
  pinMode(8,OUTPUT); digitalWrite(8,HIGH);
  pinMode(10,OUTPUT); digitalWrite(10,HIGH);
  pinMode(9,OUTPUT); digitalWrite(9,LOW);
}
inline bool radioBegin(RH_RF95 &radio) {
  digitalWrite(9,HIGH); delay(10);
  if(!radio.init() || !radio.setFrequency(915.0)) return false;
  radio.setModemConfig(RH_RF95::Bw125Cr45Sf128);
  radio.setPreambleLength(12);
  radio.setTxPower(13,false);
  radio.setModeIdle();
  return true;
}
class Logger {
 public:
  File file;
  bool ready=false;
  uint16_t session=0;
  void begin(char prefix) {
    if(!SD.begin(10)) { Serial.println("SD unavailable"); return; }
    char name[13];
    for(uint16_t i=0;i<10000;i++) {
      snprintf(name,sizeof(name),"%c%04u.CSV",prefix,i);
      if(SD.exists(name)) continue;
      file=SD.open(name,FILE_WRITE);
      if(!file) break;
      session=i;
      file.println(HEADER); file.flush();
      ready=!file.getWriteError();
      Serial.print("Log: "); Serial.println(name);
      return;
    }
    Serial.println("SD log creation failed");
  }
  void append(const Sample &s) {
    if(!ready) return;
    csv(file,s); file.flush();
    if(file.getWriteError()) { ready=false; file.close(); Serial.println("SD write failed"); }
  }
};
} // namespace ascend
