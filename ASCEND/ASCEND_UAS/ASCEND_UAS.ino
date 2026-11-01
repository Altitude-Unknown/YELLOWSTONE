#include <Wire.h>
#include <MS5x.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <Adafruit_PM25AQI.h>
#include <ASCENDCore.h>

using namespace ascend;
RH_RF95 radio(8,3);
SFE_UBLOX_GNSS gps;
MS5x pressure(&Wire);
Adafruit_PM25AQI pm;
Logger logger;
bool gpsReady=false, pressureReady=false, pmReady=false, radioReady=false;
bool transmitting=false;
uint32_t lastSample=0, lastTx=0, txStarted=0, sequence=0;
uint32_t lastPmRetry=0;
uint8_t pressureAddress=0x76;

// Extension point: assign a documented type/unit, value, and EXTRA_VALID only
// after a successful fresh read. Zero/absent flags never mean a valid zero.
void readExtraSensor(Sample &sample) { (void)sample; }

void setup() {
  Serial.begin(115200);
  uint32_t start=millis(); while(!Serial && millis()-start<2000) {}
  Serial.println("ASCEND UAS v1");
  prepareSPI(); logger.begin('U');
  Wire.begin(); Wire.setClock(100000);
  gpsReady=gps.begin();
  if(gpsReady) {
    gps.setI2COutput(COM_TYPE_UBX);
    gps.setNavigationFrequency(1);
    gps.setDynamicModel(DYN_MODEL_AIRBORNE2g);
  }
  pressure.setI2Caddr(0x76); pressureReady=pressure.connect()==0;
  if(!pressureReady) { pressureAddress=0x77; pressure.setI2Caddr(pressureAddress); pressureReady=pressure.connect()==0; }
  pmReady=pm.begin_I2C(&Wire);
  radioReady=radioBegin(radio);
  Serial.print("GPS detected: "); Serial.println(gpsReady);
  Serial.print("Pressure detected: "); Serial.println(pressureReady);
  Serial.print("PMSA003I detected: "); Serial.println(pmReady);
  Serial.print("LoRa ready: "); Serial.println(radioReady);
  Serial.println(HEADER);
}

void loop() {
  uint32_t now=millis();
  if(pressureReady) pressure.checkUpdates();
  if(transmitting && radio.mode()!=RHGenericDriver::RHModeTx) transmitting=false;
  if(transmitting && uint32_t(now-txStarted)>8000) {
    radio.setModeIdle(); transmitting=false; Serial.println("LoRa TX timeout");
  }
  if(uint32_t(now-lastSample)<SAMPLE_MS) return;
  lastSample=now;
  Sample s; s.sequence=sequence++; s.uptime=now; s.session=logger.session;
  if(gpsReady && gps.getPVT(150)) {
    s.sats=gps.getSIV();
    uint8_t fixType=gps.getFixType();
    if(gps.getGnssFixOk() && (fixType==3 || fixType==4)) {
      s.flags|=GPS_VALID;
      s.latitude=gps.getLatitude(); s.longitude=gps.getLongitude();
      s.altitudeMm=gps.getAltitudeMSL();
    }
    if(gps.getDateValid() && gps.getTimeValid()) {
      s.flags|=UTC_VALID; s.year=gps.getYear(); s.month=gps.getMonth();
      s.day=gps.getDay(); s.hour=gps.getHour(); s.minute=gps.getMinute(); s.second=gps.getSecond();
    }
  }
  if(pressureReady && pressure.isReady()) {
    Wire.beginTransmission(pressureAddress);
    bool connected=Wire.endTransmission()==0;
    float p=pressure.GetPres(), t=pressure.GetTemp();
    if(connected && isfinite(p) && isfinite(t) && p>=800 && p<=120000) {
      s.pressurePa=p; s.barometerTempC=t; s.flags|=PRESSURE_VALID;
    }
  }
  if(!pmReady && uint32_t(now-lastPmRetry)>=10000) {
    lastPmRetry=now; pmReady=pm.begin_I2C(&Wire);
  }
  PM25_AQI_Data air;
  if(pmReady && pm.read(&air)) {
    s.pm1=air.pm10_env; s.pm25=air.pm25_env; s.pm10=air.pm100_env; s.flags|=PM_VALID;
  }
  readExtraSensor(s);
  logger.append(s);
  if(Serial) csv(Serial,s);
  if(radioReady && !transmitting && uint32_t(now-lastTx)>=TELEMETRY_MS) {
    lastTx=now;
    transmitting=radio.send(reinterpret_cast<const uint8_t *>(&s),sizeof(s));
    txStarted=millis();
  }
}
