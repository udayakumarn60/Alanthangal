/*
* PZEM-017 Example
*
* This example demonstrates how to use the PZEMPlus library with PZEM-017
* energy monitoring device. It shows both individual measurement methods
* and the efficient batch reading method.
*
* Author: Lucas Hudson
* GitHub: https://github.com/lucashudson-eng/PZEMPlus
*
* License: GPL-3.0
*/

#define PZEM_017
#define PZEM_RX (uint8_t)4
#define PZEM_TX (uint8_t)5
#include <PZEMPlus.h>
#include <RH_ASK.h>
#include <SPI.h>

// #define PZEM_RX 2
// #define PZEM_TX 3

// In case of using MAX485
#define PZEM_RS485_EN 2

#if defined(__AVR_ATmega328P__)
SoftwareSerial PZEM_SERIAL(PZEM_RX, PZEM_TX);
#else
HardwareSerial PZEM_SERIAL(2);
#endif

#if defined(PZEM_RX) && defined(PZEM_TX) && !defined(__AVR_ATmega328P__)
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);
#else
PZEMPlus pzem(PZEM_SERIAL);
#endif

RH_ASK driver;

void setup(){
  Serial.begin(115200);

  pzem.begin();

#if defined(PZEM_RS485_EN)
  pzem.setEnable(PZEM_RS485_EN);
#endif

  // Configure timeouts
  pzem.setTimeouts(100); // 100ms timeout to wait for response

  // Set current range to 50, 100, 200 or 300A
  // pzem.setCurrentRange(100);

  // Serial.print("Address: ");
  // Serial.println(pzem.getAddress());
  // Serial.print("Current range: ");
  // Serial.println(pzem.getCurrentRange());
  // Serial.print("High voltage alarm threshold: ");
  // Serial.println(pzem.getHighVoltageAlarm());
  // Serial.print("Low voltage alarm threshold: ");
  // Serial.println(pzem.getLowVoltageAlarm());

  // Set address to 0x01 (0x01 to 0xF7)
  // pzem.setAddress(0x01);

  // Set current range to 50, 100, 200 or 300A
  pzem.setCurrentRange(100);

  // Set low and high voltage alarm thresholds
  pzem.setLowVoltageAlarm(48.75);
  pzem.setHighVoltageAlarm(59.5);

  // Reset energy counter
  pzem.resetEnergy();
  // Sync serial input to the beginning of newline
  while (1)
    if(Serial.available() && Serial.read() == '\n')
      break;
  if (!driver.init())
    Serial.println("RH_ASK driver init failed.");
  driver.send("SB Alarm Reset", 14);
  driver.waitPacketSent();
  Serial.println("PZEM-017 started");
//  Serial.println("Waiting for measurements...");
}

void processMessage(char *messageBuf) {
  int index = 0;
  char *paramPtr ;
  switch(messageBuf[index]) {
    case 'S':
      paramPtr = messageBuf + index + 3;
      char cmd = messageBuf[index +1];
      if ( cmd == 'C') {
          // uint16_t currentRange = String(paramPtr).toInt();
          if(pzem.setCurrentRange(100)) // Always Set to 100
            Serial.println("Set CurrentRange OK");
          else
            Serial.println("Error SC");
      } else if (cmd == 'L') {
          float lvAlarm = String(paramPtr).toFloat();
          if(pzem.setLowVoltageAlarm(lvAlarm))
            Serial.println("Set LoVoltAlarm OK");
          else
            Serial.println("Error LA");
      } else if (cmd == 'H') {
          float hvAlarm = String(paramPtr).toFloat();
          if(pzem.setHighVoltageAlarm(hvAlarm))
            Serial.println("Set HiVoltAlarm OK");
          else
            Serial.println("Error HA");
      } else if (cmd == 'R') {
          if(pzem.resetEnergy())
            Serial.println("Set ResetEnergy OK");
          else
            Serial.println("Error RE");
      } else if (cmd == 'T') {
          float lvAlarm = pzem.getLowVoltageAlarm();
          float hvAlarm = pzem.getHighVoltageAlarm();
          float currentRange = pzem.getCurrentRange();
          Serial.print("Status "); Serial.print(currentRange); 
          Serial.print(' '); Serial.print(lvAlarm);
          Serial.print(' '); Serial.println(hvAlarm);
      } else if (cmd == 'A') {
          driver.send("SB Alarm Reset", 14);
          driver.waitPacketSent();
          Serial.println("Set ResetAlarm OK");
      }
      break;
    default:
      Serial.println("Error CM");
      break;
  }
}

static char serialBuf[50];
void loop(){
  uint32_t startTime = millis();
  int  serialIndex = 0, numBytes = 0;

  numBytes = Serial.available();
  // Serial.println(numBytes);
  if (numBytes) {
    int i = 0;
    for(i = 0 ; i < numBytes; i++){
      serialBuf[serialIndex] = Serial.read();
      if (serialBuf[serialIndex] == '\n') {
        serialBuf[serialIndex] = NULL;
        serialIndex = 0;
        processMessage(serialBuf);
        break;
      }
      else serialIndex++;
    }
  }

  delay(200);

  startTime = millis();
  // float voltage, current, power, energy;

  bool highVoltageAlarm = pzem.readHighVoltageAlarm();
  if (highVoltageAlarm){
    Serial.println("Larm BHi Active");
    driver.send("SB HiVolt Alarm On", 18);
    driver.waitPacketSent();
  }
  bool lowVoltageAlarm = pzem.readLowVoltageAlarm();
  if (lowVoltageAlarm) {
    Serial.println("Larm BLow Active");
    driver.send("SB LoVolt Alarm On", 18);
    driver.waitPacketSent();
  }
  float voltage, current, power, energy;
  if (pzem.readAll(&voltage, &current, &power, &energy)){
    char voltageBuf[10];
    char currentBuf[10];
    char energyBuf[15];
    char printBuf[50];
    uint32_t readAllTime = millis() - startTime;
    // Serial.println(voltage);
    sprintf(printBuf, "V%sA%sE%s", dtostrf(voltage, 6, 2, voltageBuf),
                                   dtostrf(current, 6, 2, currentBuf),
                                   dtostrf(energy , 8, 2, energyBuf));
    for(int i=0; i < strlen(printBuf); i++)
      if (printBuf[i] == ' ') printBuf[i]= '0';
    Serial.println(printBuf);
  }
  else{
    Serial.println("Error RD");
  }

  delay(2000);
}
