#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RH_RF95.h>

// Yellowstone shared peripheral pins
#define RFM95_CS   8
#define RFM95_INT  3
#define RFM95_RST  9
#define SD_CS      10
#define SD_ALT_CS  4
#define RF95_FREQ  915.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);
uint32_t diagnosticPass = 0;

void setSpiDevicesDeselected() {
  pinMode(RFM95_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  pinMode(SD_ALT_CS, OUTPUT);
  digitalWrite(RFM95_CS, HIGH);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SD_ALT_CS, HIGH);
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

void printI2cLineLevels() {
  Serial.println();
  Serial.println("I2C line levels:");
#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.end();
  pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
  pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
  delay(2);

  bool sdaHigh = digitalRead(PIN_WIRE_SDA);
  bool sclHigh = digitalRead(PIN_WIRE_SCL);
  Serial.print("  SDA: ");
  Serial.println(sdaHigh ? "HIGH" : "LOW");
  Serial.print("  SCL: ");
  Serial.println(sclHigh ? "HIGH" : "LOW");

  if (!sdaHigh || !sclHigh) {
    Serial.println("  bus fault: one or both I2C lines are held low");
  }
#else
  Serial.println("  SDA/SCL pin constants unavailable for this board package");
#endif
  Wire.begin();
}

void scanI2c() {
  Serial.println();
  Serial.println("I2C scan:");

  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("  found 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      found++;
    } else if (error == 4) {
      Serial.print("  unknown error at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (found == 0) Serial.println("  no I2C devices found");
}

void testSdOnCs(uint8_t csPin) {
  Serial.print("  CS ");
  Serial.print(csPin);
  Serial.print(": ");

  digitalWrite(RFM95_CS, HIGH);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SD_ALT_CS, HIGH);

  if (SD.begin(csPin)) {
    Serial.println("SD.begin PASS");
    File root = SD.open("/");
    if (root) {
      Serial.println("    SD root open: PASS");
      root.close();
    } else {
      Serial.println("    SD root open: FAIL");
    }
  } else {
    Serial.println("SD.begin FAIL");
  }
}

void testSd() {
  Serial.println();
  Serial.println("SD test:");
  testSdOnCs(SD_CS);
  testSdOnCs(SD_ALT_CS);
}

void testLoRa() {
  Serial.println();
  Serial.println("LoRa test:");
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SD_ALT_CS, HIGH);
  resetLoRa();

  if (!rf95.init()) {
    Serial.println("  rf95.init: FAIL");
    return;
  }

  Serial.println("  rf95.init: PASS");

  if (rf95.setFrequency(RF95_FREQ)) {
    Serial.print("  setFrequency ");
    Serial.print(RF95_FREQ, 1);
    Serial.println(" MHz: PASS");
  } else {
    Serial.println("  setFrequency: FAIL");
  }
}

void runDiagnostics() {
  diagnosticPass++;
  Serial.println();
  Serial.print("Diagnostic pass ");
  Serial.println(diagnosticPass);

  setSpiDevicesDeselected();
  printI2cLineLevels();
  scanI2c();
  testSd();
  testLoRa();

  Serial.println();
  Serial.println("Diagnostic pass complete.");
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 4000) {
    delay(10);
  }

  Serial.println();
  Serial.println("Yellowstone board diagnostic");
  Serial.println("USB serial: PASS");

  runDiagnostics();

  Serial.println();
  Serial.println("Diagnostic setup complete. LED will blink once per second.");
}

void loop() {
  static unsigned long lastBlink = 0;
  static unsigned long lastDiagnostic = 0;
  static bool ledState = false;
  static uint32_t seconds = 0;

  if (millis() - lastBlink >= 1000) {
    lastBlink = millis();
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);

    seconds++;
    Serial.print("alive ");
    Serial.print(seconds);
    Serial.println("s");
  }

  if (millis() - lastDiagnostic >= 10000) {
    lastDiagnostic = millis();
    runDiagnostics();
  }
}
