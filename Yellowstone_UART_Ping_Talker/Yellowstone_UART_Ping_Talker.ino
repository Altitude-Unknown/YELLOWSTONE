#define LINK_BAUD 115200

uint32_t sequence = 0;
uint32_t lastSendMs = 0;
char lineBuf[96];
uint8_t lineLen = 0;

void processLinkLine(const char *line) {
  Serial.print("TALKER,UART_RX,");
  Serial.println(line);
}

void readLink() {
  while (Serial5.available()) {
    char ch = static_cast<char>(Serial5.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      lineBuf[lineLen] = '\0';
      if (lineLen > 0) processLinkLine(lineBuf);
      lineLen = 0;
      continue;
    }

    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = ch;
    } else {
      lineLen = 0;
      Serial.println("TALKER,ERROR,UART_LINE_TOO_LONG");
    }
  }
}

void sendPing() {
  sequence++;
  Serial5.print("YELLOWSTONE,UART_PING,");
  Serial5.println(sequence);

  Serial.print("TALKER,UART_TX,YELLOWSTONE,UART_PING,");
  Serial.println(sequence);
}

void setup() {
  Serial.begin(115200);
  Serial5.begin(LINK_BAUD);
  while (!Serial && millis() < 3000) {}

  Serial.println("TALKER,BOOT,YELLOWSTONE_UART_PING_TALKER");
  Serial.println("TALKER,SERIAL5_READY,baud,115200");
}

void loop() {
  readLink();

  uint32_t now = millis();
  if (now - lastSendMs >= 1000) {
    lastSendMs = now;
    sendPing();
  }
}
