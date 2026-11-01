#include <SPI.h>
#include <SdFat.h>
SdFat sd;
void setup() {
 Serial.begin(115200); while(!Serial) {}
 pinMode(8,OUTPUT); digitalWrite(8,HIGH);
 pinMode(9,OUTPUT); digitalWrite(9,LOW);
 pinMode(10,OUTPUT); digitalWrite(10,HIGH);
 delay(100);
 const uint32_t speeds[]={400000UL,1000000UL,4000000UL};
 for(uint32_t hz : speeds) {
  Serial.print("SPI Hz: "); Serial.println(hz);
  if(!sd.begin(SdSpiConfig(10,SHARED_SPI,hz))) { sd.initErrorPrint(&Serial); continue; }
  Serial.println("SD init PASS");
  auto f=sd.open("CHECK.TXT",O_RDWR|O_CREAT|O_TRUNC);
  if(!f) { Serial.println("Open FAIL"); continue; }
  const char expected[]="ASCEND SdFat write/read PASS";
  bool written=f.println(expected)>0 && f.sync();
  f.rewind(); char b[80]={0}; f.fgets(b,sizeof(b));
  Serial.print("Readback: "); Serial.print(b);
  Serial.println(written && strncmp(b,expected,strlen(expected))==0 ? "Write/read PASS" : "Write/read FAIL");
  f.close(); sd.ls(&Serial,LS_SIZE);
  auto log=sd.open("U0000.CSV",O_RDONLY);
  if(log) {
   Serial.println("ASCEND log header:");
   char line[320]; log.fgets(line,sizeof(line)); Serial.print(line);
   Serial.println("ASCEND log tail:");
   if(log.fileSize()>700) log.seekSet(log.fileSize()-700);
   while(log.available()) Serial.write(log.read());
   log.close();
  }
  break;
 }
}
void loop() {}
