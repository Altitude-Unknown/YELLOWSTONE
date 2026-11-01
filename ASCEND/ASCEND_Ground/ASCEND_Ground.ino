#include <ASCENDCore.h>
using namespace ascend;
RH_RF95 radio(8,3);
Logger logger;
bool radioReady=false;
void setup() {
  Serial.begin(115200);
  uint32_t start=millis(); while(!Serial && millis()-start<2000) {}
  Serial.println("ASCEND ground v1: receive only");
  prepareSPI(); logger.begin('G'); radioReady=radioBegin(radio);
  Serial.print("LoRa ready: "); Serial.println(radioReady);
  Serial.println(HEADER);
  if(radioReady) radio.setModeRx();
}
void loop() {
  if(!radioReady || !radio.available()) return;
  uint8_t bytes[RH_RF95_MAX_MESSAGE_LEN], length=sizeof(bytes);
  if(!radio.recv(bytes,&length)) return;
  if(length!=sizeof(Sample)) return;
  Sample s; memcpy(&s,bytes,sizeof(s));
  if(s.magic!=0x4153 || s.version!=1) return;
  logger.append(s);
  if(Serial) csv(Serial,s);
  // No acknowledgments, commands, or retransmission requests.
  radio.setModeRx();
}
