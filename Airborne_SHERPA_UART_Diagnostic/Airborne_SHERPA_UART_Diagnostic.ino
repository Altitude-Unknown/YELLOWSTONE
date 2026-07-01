#define SHERPA_BAUD 115200

uint32_t sequence = 0;
uint32_t lastSendMs = 0;
char lineBuf[96];
uint8_t lineLen = 0;

void processSherpaLine(const char *line) {
  Serial.print("AIRBORNE,UART_RX,");
  Serial.println(line);
}

void readSherpaUart() {
  while (Serial5.available()) {
    char ch = static_cast<char>(Serial5.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      lineBuf[lineLen] = '\0';
      if (lineLen > 0) processSherpaLine(lineBuf);
      lineLen = 0;
      continue;
    }

    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = ch;
    } else {
      lineLen = 0;
      Serial.println("AIRBORNE,ERROR,UART_LINE_TOO_LONG");
    }
  }
}

void sendPing() {
  sequence++;
  Serial5.print("AIRBORNE,UART_TEST,");
  Serial5.println(sequence);

  Serial.print("AIRBORNE,UART_TX,AIRBORNE,UART_TEST,");
  Serial.println(sequence);
}

void setup() {
  Serial.begin(115200);
  Serial5.begin(SHERPA_BAUD);
  while (!Serial && millis() < 3000) {}

  Serial.println("AIRBORNE,BOOT,SHERPA_UART_DIAGNOSTIC");
  Serial.println("AIRBORNE,SERIAL5_READY,baud,115200");
}

void loop() {
  readSherpaUart();

  uint32_t now = millis();
  if (now - lastSendMs >= 1000) {
    lastSendMs = now;
    sendPing();
  }
}
