#include <HardwareSerial.h>

#define YELLOWSTONE_BAUD 115200
#define YELLOWSTONE_RX_PIN 21
#define YELLOWSTONE_TX_PIN 20

HardwareSerial YellowstoneSerial(1);

char uartLine[96];
uint8_t uartLineLen = 0;
uint32_t rxCount = 0;
uint32_t lastHeartbeatMs = 0;

void sendAck(uint32_t sequence) {
  YellowstoneSerial.print("SHERPA,UART_ACK,");
  YellowstoneSerial.println(sequence);

  Serial.print("SHERPA,UART_TX,SHERPA,UART_ACK,");
  Serial.println(sequence);
}

bool parseAirborneTestLine(const char *line, uint32_t &sequence) {
  const char prefix[] = "AIRBORNE,UART_TEST,";
  if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) return false;

  char *end = nullptr;
  unsigned long parsed = strtoul(line + sizeof(prefix) - 1, &end, 10);
  if (end == line + sizeof(prefix) - 1 || *end != '\0') return false;
  sequence = static_cast<uint32_t>(parsed);
  return sequence != 0;
}

void processUartLine(const char *line) {
  rxCount++;
  Serial.print("SHERPA,UART_RX,");
  Serial.println(line);

  uint32_t sequence = 0;
  if (parseAirborneTestLine(line, sequence)) {
    sendAck(sequence);
  } else {
    Serial.print("SHERPA,IGNORED_UART,");
    Serial.println(line);
  }
}

void readYellowstoneUart() {
  while (YellowstoneSerial.available()) {
    char ch = static_cast<char>(YellowstoneSerial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      uartLine[uartLineLen] = '\0';
      if (uartLineLen > 0) processUartLine(uartLine);
      uartLineLen = 0;
      continue;
    }

    if (uartLineLen < sizeof(uartLine) - 1) {
      uartLine[uartLineLen++] = ch;
    } else {
      uartLineLen = 0;
      Serial.println("SHERPA,ERROR,UART_LINE_TOO_LONG");
    }
  }
}

void printHeartbeat() {
  uint32_t now = millis();
  if (now - lastHeartbeatMs < 2000) return;
  lastHeartbeatMs = now;

  Serial.print("SHERPA,HEARTBEAT,swapped,1,rx_count,");
  Serial.println(rxCount);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  YellowstoneSerial.begin(YELLOWSTONE_BAUD, SERIAL_8N1, YELLOWSTONE_RX_PIN, YELLOWSTONE_TX_PIN);

  Serial.println("SHERPA,BOOT,UART_DIAGNOSTIC_SWAPPED");
  Serial.println("SHERPA,YELLOWSTONE_UART_READY,rx_pin,21,tx_pin,20,baud,115200");
}

void loop() {
  readYellowstoneUart();
  printHeartbeat();
}
