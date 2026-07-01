#define LINK_BAUD 115200

char lineBuf[96];
uint8_t lineLen = 0;
uint32_t rxCount = 0;
uint32_t ackCount = 0;
uint32_t lastHeartbeatMs = 0;

bool parsePingLine(const char *line, uint32_t &sequence) {
  const char prefix[] = "YELLOWSTONE,UART_PING,";
  if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) return false;

  char *end = nullptr;
  unsigned long parsed = strtoul(line + sizeof(prefix) - 1, &end, 10);
  if (end == line + sizeof(prefix) - 1 || *end != '\0') return false;
  sequence = static_cast<uint32_t>(parsed);
  return sequence != 0;
}

void sendAck(uint32_t sequence) {
  Serial5.print("YELLOWSTONE,UART_ACK,");
  Serial5.println(sequence);

  ackCount++;
  Serial.print("RESPONDER,UART_TX,YELLOWSTONE,UART_ACK,");
  Serial.println(sequence);
}

void processLinkLine(const char *line) {
  rxCount++;
  Serial.print("RESPONDER,UART_RX,");
  Serial.println(line);

  uint32_t sequence = 0;
  if (parsePingLine(line, sequence)) {
    sendAck(sequence);
  } else {
    Serial.print("RESPONDER,IGNORED_UART,");
    Serial.println(line);
  }
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
      Serial.println("RESPONDER,ERROR,UART_LINE_TOO_LONG");
    }
  }
}

void printHeartbeat() {
  uint32_t now = millis();
  if (now - lastHeartbeatMs < 2000) return;
  lastHeartbeatMs = now;

  Serial.print("RESPONDER,HEARTBEAT,rx_count,");
  Serial.print(rxCount);
  Serial.print(",ack_count,");
  Serial.println(ackCount);
}

void setup() {
  Serial.begin(115200);
  Serial5.begin(LINK_BAUD);
  while (!Serial && millis() < 3000) {}

  Serial.println("RESPONDER,BOOT,YELLOWSTONE_UART_PING_RESPONDER");
  Serial.println("RESPONDER,SERIAL5_READY,baud,115200");
}

void loop() {
  readLink();
  printHeartbeat();
}
