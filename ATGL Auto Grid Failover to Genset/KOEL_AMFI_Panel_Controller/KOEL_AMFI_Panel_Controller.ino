#include <stdio.h>
#include "LEDFlasher.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Output Digital PINS

#define MAINS_RELAY     12
#define GENSET_RELAY    11
#define AUTOSTART_RELAY 4
#define BATTERY_RELAY   10

class Relay {
  uint8_t controlPin;
  bool contactorOn = false;
  uint8_t activate;
  uint8_t deActivate;

  public: 
  
  Relay::Relay(uint8_t relayPin, uint8_t activeState = HIGH){
    controlPin = relayPin;
    pinMode(controlPin,OUTPUT);
    contactorOn = false;
    activate = activeState;
    activate == HIGH? deActivate = LOW: deActivate = HIGH;
    digitalWrite(controlPin, deActivate);
  }

  bool setOn(){
    if (!contactorOn && controlPin){
      digitalWrite(controlPin, activate);
      contactorOn = true;
      return true;
    }
    else return false;
  }
  bool setOff(){
    if (contactorOn && controlPin){
      digitalWrite(controlPin, deActivate);
      contactorOn = false;
      return true;
    }
    else return false;
  }

  bool contactorStatus() {
    return contactorOn;
  }

  void poll() {

  }
};

LiquidCrystal_I2C lcd((uint8_t)0x27, 16, 2);

class AMFiDisplay{
  char *blankLine = "                ";
  char lastStatus[17] = "----------------";
  bool batteryInfoOn = false;
  char lastAMText[10];
  char lastMGText[10];
  public:

  AMFiDisplay::AMFiDisplay(LiquidCrystal_I2C *lcd){
    // TODO: Calling LCD init here makes Nano hang.
  }

  void displayMGSwitchState(char *text){
  //  if(!strcmp(text, lastAMText))
  //    return;
    lcd.setCursor(7,0);
    if(text) {
      lcd.print(text);
      // Serial.println(text);
    }
    else
      lcd.print("NULL. ");
    strcpy(lastAMText, text);
  }

  void displayAMSwitchState(char *text){
  //  if(!strcmp(text, lastMGText))
  //    return
    lcd.setCursor(0,0);
    if (text) {
      lcd.print(text);
      // Serial.println(text);
    }
    else
      lcd.print("NULL. ");
    strcpy(lastMGText, text);
  }


  void displayStatus(char *text, bool displayLast = false){
    if(!strcmp(text,lastStatus)){
      // Optimisation to reduce unnecessary LCD display writes
      return;
    }
    this->clearRow(1);
    batteryInfoOn = false;
    lcd.setCursor(0,1);
    if (displayLast) {
      lcd.print(lastStatus);
      return;
    }
    lcd.print(text);
    strcpy(lastStatus, text);
  }

  void displayBatteryInfo(char *batteryInfo){
    batteryInfoOn = true;
    lcd.setCursor(0,1);
    lcd.print(batteryInfo);
  }

  bool isBatteryInfoOn() {
    return batteryInfoOn;
  }

  void clearDisplay(){
    lcd.clear();
  }

  void clearRow(uint8_t rowNo){
    lcd.setCursor(0,rowNo%2);
    lcd.print(blankLine);
  }

  void loop() {
  }
};

typedef enum {NONE, MAINS, GENSET} MainsGenset_t;
typedef enum {AUTO, OFF, MANUAL} AutoManual_t;

class RotarySwitch{
  uint8_t nPositions = 2;
  uint8_t sensePin1;
  uint8_t sensePin2;
  uint8_t sensePin3;
  uint8_t sensePinValue[3] = {0,0,0};
  char *sensePinText[3] = {NULL, NULL, NULL};
  int8_t  currentPos = -1;
public:

  RotarySwitch::RotarySwitch(uint8_t nPos,
                           uint8_t sPin1,
                           uint8_t sPin2,
                           uint8_t sPin3,
                           char *pin1Text = "None",
                           char *pin2Text = "None",
                           char *pin3Text = "None",
                           uint8_t pin1Val = NONE,
                           uint8_t pin2Val = NONE,
                           uint8_t pin3Val = NONE
                          ){
    nPositions = nPos;
    sensePin1 = sPin1;
    sensePin2 = sPin2;
    sensePin3 = sPin3;
    pinMode(sPin1, INPUT_PULLUP);
    pinMode(sPin2, INPUT_PULLUP);
    if(nPositions == 3)
      pinMode(sPin3, INPUT_PULLUP);
    sensePinValue[0] = pin1Val;
    sensePinValue[1] = pin2Val;
    sensePinValue[2] = pin3Val;
    sensePinText[0] = pin1Text;
    sensePinText[1] = pin2Text;
    sensePinText[2] = pin3Text;
    uint8_t switchPos = sensePos();
  }

  int8_t sensePos(){
 
    if (!digitalRead(sensePin1))
      return currentPos = 0;
    if (!digitalRead(sensePin2))
      return currentPos = 1;
    if (!digitalRead(sensePin3))
      return(currentPos = 2);
    return currentPos;
  }

  uint8_t currentSwitchValue(){
    if (currentPos >=0)
      return sensePinValue[currentPos];
    else
      return NONE;
  }

  char *currentSwitchText() {
    if (currentPos >=0)
      return sensePinText[currentPos];
    else
      return NULL;
  }

  void poll() {
    sensePos();
  }
};

class VoltageSensor {
  float adc_voltage = 0.0;
  float in_voltage = 0.0;
  float ref_voltage = 5.0;
  int adc_value = 0;
  uint8_t readPin;

  public:

  VoltageSensor::VoltageSensor(uint8_t voltagePin){
    readPin = voltagePin;
    // pinMode(readPin, INPUT);
  }

  float readCurrentVoltage() {
    return in_voltage;
  }

  void senseVoltage(){
    adc_value = analogRead(readPin);
    // Determine voltage at ADC input
    adc_voltage  = (adc_value * ref_voltage) / 1024.0;
    // Calculate voltage at divider input
    in_voltage = (adc_voltage / 0.2) * (0.9123) ; 
  }
};

typedef enum {StartCharging, StopCharging, StartGenset, StopGenset, ConnectMains, IsolateMains, ConnectDG, IsolateDG} Command_t;
//#include "AMFi_Panel.h"
// #include "WiFiConnect.h"

// Connector to ESP01 on controller for WiFi and MQTT interface.
class WiFiConnect {  
  char serialBuf[100];
  int serialIndex = 0;
  int numBytes = 0;
  bool (*commandCB)(Command_t command) = NULL;

  // Member Function Definitions
  public:
    
    WiFiConnect::WiFiConnect() {
    }

    void sendBatteryVolts(float voltage){

    }

    bool scanAndProcess(){
      numBytes = Serial.available();
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
    }

    void processMessage(char *buf) {
      if ((strlen(buf) >= 2) && commandCB)
        switch (buf[0]) {
          case 'G':
            switch(buf[1]) {
              case '0':
                // Remote Switch Off Genset
                if((*commandCB)(IsolateDG)) {
                   Serial.println("G01");
                }
                else
                  Serial.println("G00");
                break;
              case '1':
                // Remove Switch On Genset
                if((*commandCB)(ConnectDG))
                  Serial.println("G11");
                else
                  Serial.println("G10");
                break;
            }
            break;
          case 'M':
          // Mains On Off Command from ESP-01 module
            switch(buf[1]) {
              case '0':
                // Remote Switch Off Mains
                if((*commandCB)(IsolateMains))
                  Serial.println("M01");
                else
                  Serial.println("M00");
                break;
              case '1':
                // Remove Switch On Mains
                if((*commandCB)(ConnectMains))
                  Serial.println("M11");
                else
                  Serial.println("M10");
                break;
            }
            break;
          case 'C':
          // Charging On Off Command from ESP-01 module
            switch(buf[1]) {
              case '0':
                // Remote Switch Off Charger
                if((*commandCB)(StopCharging))
                  Serial.println("C01");
                else
                  Serial.println("C00");
                break;
              case '1':
                // Remote Switch On Charger
                if((*commandCB)(StartCharging))
                  Serial.println("C11");
                else
                  Serial.println("C10");
                break;
            }
            break;
          case 'S':
          // Charging On Off Command from ESP-01 module
            switch(buf[1]) {
              case '0':
                // Remote Start Generator
                if((*commandCB)(StopGenset))
                  Serial.println("S01");
                else
                  Serial.println("S00");
                break;
              case '1':
                // Remote Stop Generator
                if((*commandCB)(StartGenset))
                  Serial.println("S11");
                else
                  Serial.println("S10");
                break;
            }
            break;
        }
    }

    void setCallback(bool (*processCommand)(Command_t command)){
      commandCB = processCommand;
    }
};

class MyTimer {
  long startTime = NULL;
  long duration = NULL;
  bool isTimerOn = true; // Timer will be alive on instantiation and expire immediately
  uint8_t mask = 0x00;

  public:
    MyTimer::MyTimer(uint8_t argMask, long interval = 120000) { // Default timer interval is 2 minutes
      duration = interval;
      mask = argMask;
    }

    void startTimer(){
      startTime = millis();
      isTimerOn = true;
    }

    void stopTimer() {
      startTime = NULL;
      isTimerOn = false;
    }

    bool isTimerStatusOn() {
      return(isTimerOn);
    }

    bool hasTimerExpired() {
      if ((millis() - startTime) > duration)
        return(true);
      else
        return(false);
    }

    uint8_t myMask() {
      return(mask);
    }
};

// Input Digital PINS
#define pinGENSET       8
#define pinMAINS        9
#define pinMANUAL       7
#define pinAUTO         6
#define pinOFF          5
#define pinMAINSON      2
#define pinPHASEOK      3
#define pinBATTERYVOLT  A0

Relay MainsContactor(MAINS_RELAY);
Relay GensetContactor(GENSET_RELAY);
Relay AutoStartRelay(AUTOSTART_RELAY, LOW);
Relay BatteryRelay(BATTERY_RELAY);
RotarySwitch MainsGenset(2 ,pinGENSET, pinMAINS, NULL,"MAINS ", "GENSET", "NA    ", MAINS, GENSET);
RotarySwitch AutoManual(3, pinMANUAL , pinAUTO, pinOFF, "MANUAL", "AUTO  ", "OFF   ", MANUAL, AUTO, OFF);
AMFiDisplay myDisplay(&lcd);
VoltageSensor BatteryVoltageSensor(pinBATTERYVOLT);

#define BatteryDisplayInterval  4000   // 4 seconds
#define BatteryStatusDisplayGap 30000  // 30 seconds
#define PublishInterval         2000   // 1 second
#define DisplayBlankInterval    300000 // 5 minutes - Display blanks, Any switch change, active timers or mains status change restores display
#define GensetRestartInterval   60000  // 60 seconds
#define MainsReconnectInterval  30000  // 30 seconds

// LCD Display Strings
const char *StrLoadOnMains      = "EB Contactor On ";
const char *StrLoadOffMains     = "EB Contactor Off";
const char *StrSwitchToMains    = "Switch To Mains ";
const char *StrLoadOnGenset     = "GS Contactor On ";
const char *StrLoadOffGenset    = "GS Contactor Off";
const char *StrStartingGenset   = "Starting Genset ";
const char *StrStoppingGenset   = "Stopping Genset ";
const char *StrMainsAvailable   = "EB Available!   ";
const char *StrPhaseError       = "EB Quality Error";
const char *StrAMFiOff          = "AMFi is Off     ";
const char *StrNoMains          = "EB Power Down   ";
const char *StrRunGenset        = "Pls Run Genset  ";
const char *StrRemoteChgrOn     = "Chgr Remote On  ";
const char *StrRemoteChgrOff    = "Chgr Remote Off ";
const char *StrRemoteStartGen   = "GS Remote Start ";
const char *StrRemoteStopGen    = "GS Remote Stop  ";
const char *StrRemoteMainsOn    = "MC Remote On    ";
const char *StrRemoteMainsOff   = "MC Remote Off   ";
const char *StrGensetPowerOn    = "GC Remote On    ";
const char *StrGensetPowerOff   = "GC Remote Off   ";
const char *StrDelayWait        = "Delay Wait...   ";
const char *StrReady            = "Ready...        ";

class AMFiPanel {
  Relay *mainsContactor;
  Relay *gensetContactor;
  Relay *autoStartRelay;
  Relay *batteryRelay;
  RotarySwitch *mainsGenset;
  RotarySwitch *autoManual;
  AMFiDisplay *myDisplay;
  VoltageSensor *batteryVoltage;
  MyTimer gsTimer = MyTimer(0x01);
  MyTimer mcTimer = MyTimer(0x02);
  uint8_t timerRunning = 0x00;
  uint8_t mainsSensePin;
  uint8_t phaseOKSensePin;
  uint8_t lastMGSwitchSetting = 255;
  uint8_t lastAMSwitchSetting = 255;
  bool lastMainsSetting = false;
  bool lastPhaseOKSetting = false;
  bool loadOnGenset = false;
  bool loadOnMains = false;
  bool dgSetOn = false;
  bool displayBlanked = false;
  long lastBatteryDisplay = NULL;
  long lastPublishTime = NULL;
  long lastDisplayUpdate = NULL;
  long lastGensetRun = NULL;
  long lastMainsConnect = NULL;
  bool chargingOn;
  float batteryVolts;
// Function definitions for AMFiPanel Class
  public:

  AMFiPanel::AMFiPanel(Relay *mains,
                       Relay *genset,
                       Relay *autostart,
                       Relay *battery,
                       RotarySwitch *MGSwitch,
                       RotarySwitch *AMOSwitch,
                       AMFiDisplay *display,
                       VoltageSensor *vsensor,
                       uint8_t mainsOk,
                       uint8_t phaseOk){
    mainsContactor = mains;
    gensetContactor = genset;
    autoStartRelay = autostart;
    batteryRelay = battery;
    mainsGenset = MGSwitch;
    autoManual = AMOSwitch;
    myDisplay = display;
    batteryVoltage = vsensor;
    mainsSensePin = mainsOk;
    phaseOKSensePin = phaseOk;
    pinMode(mainsSensePin, INPUT_PULLUP);
    pinMode(phaseOKSensePin, INPUT_PULLUP);
 //   Do not call delay() in constructor. Nano hangs if delay is called before setup()
 //   is complete
 //   delay(1000);
 //   lcd.init();
 //   myDisplay->clearDisplay();
  }

  bool mainsConnectOK() {
    long now = millis();
    if ((now - lastMainsConnect) > MainsReconnectInterval)
      return(true);
    return(false);
  }

  void setMainsOff() {
    mainsContactor->setOff();
    loadOnMains = false;
    lastMainsConnect = millis();
  }

  void setMainsOn() {
    mainsContactor->setOn();
    loadOnMains = true;
    lastMainsConnect = millis();
  }

  MainsGenset_t currentMGSwitchValue(){
    return mainsGenset->currentSwitchValue();
  }

  AutoManual_t currentAMSwitchValue(){
    return autoManual->currentSwitchValue();
  }

  bool setBatteryChargingOn(){
    return batteryRelay->setOn();
  }

  bool setBatteryChargingOff(){
    return batteryRelay->setOff();
  }

  void displayStatus(char *text) {
    myDisplay->displayStatus(text);
  }

  void setGensetOff(){
    gensetContactor->setOff();
    loadOnGenset = false;
  }

  void setGensetOn() {
    gensetContactor->setOn();
    loadOnGenset = true;
  }

  bool gensetStartOK() {
    if((millis() - lastGensetRun) > GensetRestartInterval) 
      return(true);
    return(false);
  }

  void startGenset() {
    autoStartRelay->setOn();
    dgSetOn = true;
    lastGensetRun = millis();
  }

  void stopGenset() {
    autoStartRelay->setOff();
    dgSetOn = false;
    lastGensetRun = millis();
  }

  bool isDGOn(){
    return dgSetOn;
  }
  
  void setLoadOnMains(){
    setGensetOff();
    myDisplay->displayStatus(StrLoadOffGenset);
    delay(1000);
    setMainsOn();
  }

  void setLoadOnGenset() {
    setMainsOff(); 
    myDisplay->displayStatus(StrLoadOffMains);
    delay(1000);
    setGensetOn();
  }

  bool checkMainsInput() {
    if (digitalRead(mainsSensePin) == HIGH) 
      return false; 
    else 
      return true; 
  }

  bool checkPhaseOKInput() {
    if (digitalRead(phaseOKSensePin) == HIGH) return false; else return true;
  }

  void switchLoadToMains() {
    // Disconnect Load from Genset
    gensetContactor->setOff();
    loadOnGenset = false;
    delay(1000);
    // Switch off the Genset
    myDisplay->displayStatus(StrStoppingGenset);
    this->stopGenset();
    delay(2000);
    // Switch On Mains
    this->setLoadOnMains();
    loadOnMains = true;
  }

  void switchLoadToGenset() {
    mainsContactor->setOff();
    loadOnMains = false;
    delay(1000);
    this->setLoadOnGenset();
    this->startGenset();
    myDisplay->displayStatus(StrStartingGenset);
    delay(3000);
    loadOnGenset = true;
  }

  setDisplayOn() {
    displayBlanked = false;
    lastDisplayUpdate = millis();
    lcd.display();
    lcd.backlight();
  }

  void disconnectLoad() {
    setGensetOff();
    this->stopGenset();
    setMainsOff();
  }

  void switchOffAMFi() {
    // Disconnect Load from Genset
    disconnectLoad();
    mcTimer.stopTimer();
    gsTimer.stopTimer();
    timerRunning = 0x00;
  }

  void updateBatteryInfo() {
    long currentTime = millis();
    long gap = currentTime - lastBatteryDisplay;
    batteryVoltage->senseVoltage();
    if (gap < BatteryDisplayInterval) {
      return;
    } else if (gap < BatteryStatusDisplayGap) {
        if(myDisplay->isBatteryInfoOn())
          myDisplay->displayStatus(NULL, true); // Restore Last Status
        return ;
    }
    batteryVolts = batteryVoltage->readCurrentVoltage();
    chargingOn = batteryRelay->contactorStatus();
    char sbuf[10], tmpbuf[17];
    dtostrf(batteryVolts,4,1, sbuf);
    sprintf(tmpbuf,"B:%sv  %s", sbuf, chargingOn?"Charge ":"Idle   ");
    myDisplay->displayBatteryInfo(tmpbuf);
    lastBatteryDisplay = currentTime;
  }

  void publishToESP01() {
    long currentTime = millis();
    long gap = currentTime - lastPublishTime;
    char printBuf[50];
    char fBuf[10];
    if (gap < PublishInterval)
      return;
    if (autoStartRelay->contactorStatus())
      Serial.println("S11");
    else
      Serial.println("S01");

    if (mainsContactor->contactorStatus())
      Serial.println("M11");
    else
      Serial.println("M01");
  
    if (batteryRelay->contactorStatus())
      Serial.println("C11");
    else
      Serial.println("C01");

    if (gensetContactor->contactorStatus()) 
      Serial.println("G11");
    else
      Serial.println("G01");

    sprintf(printBuf,"X%1d", mainsGenset->currentSwitchValue());
    Serial.println(printBuf);
    sprintf(printBuf,"Y%1d", autoManual->currentSwitchValue());
    Serial.println(printBuf);
    dtostrf(batteryVolts,4,1, fBuf);
    sprintf(printBuf,"B%s",fBuf);
    Serial.println(printBuf);
    lastPublishTime = currentTime;
  }

  float getBatteryVolts() {
    return batteryVolts;
  }

  bool perform() {
    static char statusLine[20];
    // Get current switch settings
    uint8_t currentMGSwitchSetting = mainsGenset->currentSwitchValue();
    uint8_t currentAMSwitchSetting = autoManual->currentSwitchValue();
    bool currentMainsSetting = this->checkMainsInput();
    bool currentPhaseOKSetting = this->checkPhaseOKInput();
    long currentTime = millis();
    long gap = currentTime - lastDisplayUpdate ;
    updateBatteryInfo();
    publishToESP01();
    if (lastMainsSetting == currentMainsSetting &&
        lastPhaseOKSetting == currentPhaseOKSetting &&
        lastMGSwitchSetting == currentMGSwitchSetting &&
        lastAMSwitchSetting == currentAMSwitchSetting) {
          // Nothing has changed. So blank display if required and check for active timers
          if (!displayBlanked && lastDisplayUpdate && (gap >= DisplayBlankInterval)) {
            lcd.noDisplay();
            lcd.noBacklight();
            displayBlanked = true;
          }
          if (!timerRunning)
            return false;
    }
    // Some inputs or settings have changed or a delay timer is running. 
    // If display is blanked, restore the display
    if (displayBlanked){
      displayBlanked = false;
      lcd.display();
      lcd.backlight();
      char *ptr;
      if (loadOnMains)
        ptr = StrLoadOnMains;
      else if (loadOnGenset)
        ptr = StrLoadOnGenset;
      else
        ptr = StrReady;
      displayStatus(ptr);
    }
    lastDisplayUpdate = currentTime;
    // Now find what changed and process it
    if (currentMainsSetting) {
      // Mains available
      if (currentPhaseOKSetting) {
        // Mains Quality Ok
        gsTimer.stopTimer();
        timerRunning &= ~gsTimer.myMask();
        switch (currentAMSwitchSetting) {
          case AUTO:
            if(!loadOnMains)
              if(!mainsConnectOK()) {
                mcTimer.startTimer();
                timerRunning |= mcTimer.myMask();
                strcpy(statusLine, StrDelayWait);
                break;
              } 
            switch(currentMGSwitchSetting){
              case MAINS:
                        if (!loadOnMains)
                          setMainsOn();
                        break;
              case GENSET:
                        if (loadOnGenset) {
                          displayStatus(StrSwitchToMains);
                          switchLoadToMains();
                        }
                        else {
                          setMainsOn();
                        }
                        break;
            }
            mcTimer.stopTimer();
            timerRunning &= ~mcTimer.myMask();
            strcpy(statusLine, StrLoadOnMains);
            break;
          case MANUAL:
            switch(currentMGSwitchSetting){
              case MAINS:
                        // Mains Supply Quality OK. Set Load On Mains
                        if (loadOnGenset) {
                          displayStatus(StrSwitchToMains);
                          switchLoadToMains();
                        }
                        else {
                          setMainsOn();
                        }
                        strcpy(statusLine, StrLoadOnMains);
                        break;
              case GENSET:
                        // Disconnect Mains
                        setMainsOff();
                        delay(1000);
                        // Keep the Genset contactor On in case User starts Genset
                        setGensetOn();
                        strcpy(statusLine, StrMainsAvailable);
                        break;
            }
            break;
          case OFF:
            switchOffAMFi();
            strcpy(statusLine,StrAMFiOff);
            break;
        }
        // Serial.println(statusLine);
        myDisplay->displayStatus(statusLine);
        // Preserve settings and mains power status for next round
        lastMGSwitchSetting = currentMGSwitchSetting;
        lastAMSwitchSetting = currentAMSwitchSetting;
        lastMainsSetting = currentMainsSetting;
        lastPhaseOKSetting = currentPhaseOKSetting;
        // We have done some work. Return true
        return true;
      } else {
      // Phase Error
        strcpy(statusLine, StrPhaseError);
      } 
    } else {
        // Mains not Available
        strcpy(statusLine, StrNoMains);
    }
    // Following code executes when there is a mains failure due to EB outage or 3 phase power quality
    mcTimer.stopTimer();
    timerRunning &= ~mcTimer.myMask();
    myDisplay->displayMGSwitchState(MainsGenset.currentSwitchText());
    myDisplay->displayAMSwitchState(AutoManual.currentSwitchText());
    switch (currentAMSwitchSetting) {
      case AUTO:
        switch (currentMGSwitchSetting) {
          case GENSET:
            if(!loadOnGenset) {
              if (gensetStartOK()) {
                switchLoadToGenset();
                strcpy(statusLine, StrLoadOnGenset);
                gsTimer.stopTimer();
                timerRunning &= ~gsTimer.myMask();
              } else {
                gsTimer.startTimer();
                timerRunning |= gsTimer.myMask();
                strcpy(statusLine, StrDelayWait);
              }
            }
            break;
          case MAINS:
            if(loadOnGenset)
              switchLoadToMains();
            setMainsOff();
            strcpy(statusLine, StrNoMains);
            break;
        }
        break;
      case OFF:
        switchOffAMFi();
        strcpy(statusLine, StrAMFiOff);
        break;
      case MANUAL:
        switch(currentMGSwitchSetting){
          case MAINS:
            if (loadOnGenset)
              switchLoadToMains();
            setMainsOff();
            strcpy(statusLine, StrNoMains);
            break;
          case GENSET:
            setGensetOn();
            strcpy(statusLine, StrRunGenset);
            break;
        }
        break;
    }
    // Serial.println(statusLine);
    myDisplay->displayStatus(statusLine);
    // Preserve settings and mains power status for next round
    lastMGSwitchSetting = currentMGSwitchSetting;
    lastAMSwitchSetting = currentAMSwitchSetting;
    lastMainsSetting = currentMainsSetting;
    lastPhaseOKSetting = currentPhaseOKSetting;
    // We have done some work. Return true
    return true;
  }
};


AMFiPanel myPanel(&MainsContactor, &GensetContactor, &AutoStartRelay, &BatteryRelay,
                  &MainsGenset, &AutoManual, &myDisplay, &BatteryVoltageSensor, pinMAINSON, pinPHASEOK);

WiFiConnect myWifiConnect; // Connection to ESP-01 via serial port

bool processCommand(Command_t command) {
  MainsGenset_t mgsetting = myPanel.currentMGSwitchValue();
  AutoManual_t amsetting = myPanel.currentAMSwitchValue();
  myPanel.setDisplayOn();
  switch(command){
    case StartCharging:
      myPanel.displayStatus  (StrRemoteChgrOn);
      return (myPanel.setBatteryChargingOn());
    case StopCharging:
      myPanel.displayStatus  (StrRemoteChgrOff);
      return (myPanel.setBatteryChargingOff());
    case StartGenset:
      if ((mgsetting == GENSET) && (amsetting == MANUAL)){
        myPanel.displayStatus(StrRemoteStartGen);
        myPanel.startGenset();
        return (true);
      }
      else return false;
    case StopGenset:
      if ((mgsetting == GENSET) && (amsetting == MANUAL)){
        myPanel.displayStatus(StrRemoteStopGen);
        myPanel.stopGenset();    
        return (true);
      }
      else return false;
    case ConnectMains:
      if ((mgsetting == MAINS) && (amsetting == MANUAL)){
        myPanel.displayStatus(StrRemoteMainsOn);
        myPanel.setMainsOn(); 
        return (true);
      }
      else return false;
    case IsolateMains:
      if ((mgsetting == MAINS) && (amsetting == MANUAL)){
        myPanel.displayStatus("StrRemoteMainsOff");
        myPanel.setMainsOff();    
        return (true);
      }
      else return false;
    case ConnectDG:
      if ((mgsetting == GENSET) && (amsetting == MANUAL)){
        myPanel.displayStatus(StrGensetPowerOn);
        myPanel.setGensetOn(); 
        return (true);
      }
      else return false;
    case IsolateDG:
      if ((mgsetting == GENSET) && (amsetting == MANUAL)){
        myPanel.displayStatus(StrGensetPowerOff);
        myPanel.setGensetOff();    
        return (true);
      }
      else return false;
  }
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.backlight();
  lcd.setContrast(200);
  lcd.print("AMFI Controller ");
  myWifiConnect.setCallback(processCommand);
  delay(5000);
  lcd.clear();
}

bool state = false;
Flasher led(LED_BUILTIN, 100, 1900);

void loop() {
  // put your main code here, to run repeatedly:
  MainsGenset.poll();
  AutoManual.poll();
  led.Update();
  myWifiConnect.scanAndProcess();
  if (myPanel.perform() ) {
    myDisplay.displayMGSwitchState(MainsGenset.currentSwitchText());
    myDisplay.displayAMSwitchState(AutoManual.currentSwitchText());
  }
  delay(250);
}
