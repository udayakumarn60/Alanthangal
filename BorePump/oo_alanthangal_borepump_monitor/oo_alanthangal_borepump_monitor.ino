#undef DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>
#include <ESP8266Relay.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "atgl-bore-motor-monitor"
#define MY_SSID       "atgl-s24-wifi-hotspot"

// TODO: Remove -test from the strings below before going production

#define CURRENT_TOPIC "bore-motor-current"
#define VOLTAGE_TOPIC "bore-motor-voltage"

//

#define SITE_ADDRESS   "/ATGL"
#define INFO_HANDLE    "/INFO"
#define CONFIRM_HANDLE "confirm"

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
// We are using an onboard LED on ESP-01. It is on GPIO01. The default definition for builtin LED is GPIO02

//#define ONBOARD_LED GPIO01
//Flasher onboardLED(ONBOARD_LED, 500, 500);  // This is our LED flasher on GPIO02 on an ESP-01
MyWifiSTA wifiStation(MY_SSID, "uday1-mifi", 15);
MyMQTTWrapper myMQTT(&mqttClient, MQTT_SERVER, CONTROLLER_ID , "uday", "swim2sea");

/*
 *  Our ESP01 module object is instantiated below
 */
ESP12Module myESP01(CONTROLLER_ID, SITE_ADDRESS, CONFIRM_HANDLE , INFO_HANDLE, &mqttClient);

char serialBuf[256];
int  serialIndex = 0;

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  // Serial.println("Starting up");
  while(!wifiStation.wifiConnected()){
    delay(500);
    wifiStation.reconnectWiFi();
  }

  // Serial.println("Wifi Connected");
  // Loop till MQTT is connected
  while (!myMQTT.connected())
    myMQTT.connectMe();

  // Serial.println("MQTT connected");
  
  // Let us advertise our controller name and site address
  myESP01.advertise();

//  // Let us advertise our controller capabilities
//
//  for (int i = 0 ; i < numRelays; i++){
//    char msgBuf[100];
//    sprintf(msgBuf, "%s/%s/%s/capability",INFO_HANDLE, CONTROLLER_ID, relays[i]->getTopic());
//#ifdef DEBUG
//    Serial.println(msgBuf);
//#endif
//    mqttClient.publish(msgBuf, relays[i]->capability());
//    delay(20);
//  }
//  // Let us subscribe for the module level topic which is /<information handle>/<controller name>
//  myESP01.subscribe();
  
  delay(1000);

  // Sync serial input to the beginning of newline
  while (1)
    if(Serial.available() && Serial.read() == '\n')
      break;
}

float voltageNow  = 0.0;
float currentNow  = 0.0;
float voltagePrev = 0.0;
float currentPrev = 0.0;
bool  initial = true;

bool processMessage(char *msgBuf) {
  char currentValue[20];
  char voltageValue[20];
  char wattageValue[20];
  bool voltageAvailable = false, currentAvailable = false;

  if (msgBuf[0] == 'V' && msgBuf[1] ==':') {
    strncpy(voltageValue, msgBuf+3, 6);
    voltageValue[5] = NULL;
    voltageNow = String(voltageValue).toFloat();
    voltageAvailable = true;
  }
  if (msgBuf[9] == 'A' && msgBuf[10] == ':') {
    strncpy(currentValue, msgBuf+12, 6);
    currentValue[5] = NULL;
    currentNow = String(currentValue).toFloat();
    currentAvailable = true;
  }
  if (voltageNow > 350.0)
    return false;
  if (voltageAvailable && currentAvailable) {
    if (initial or voltagePrev != voltageNow ) {
      sprintf(msgBuf,"%s/borepump/%s", SITE_ADDRESS, CURRENT_TOPIC);
      mqttClient.publish(msgBuf, currentValue);
    }
    if (initial or currentPrev != currentNow) {
      sprintf(msgBuf,"%s/borepump/%s", SITE_ADDRESS, VOLTAGE_TOPIC);   
      mqttClient.publish(msgBuf, voltageValue);
    }
    initial = false;
    voltagePrev = voltageNow;
    currentPrev = currentNow;
    
    return true;
  }
  else return false; // We got a garbled string and ignored it
}

long lastMillis = millis();

void loop() {
  // loop code, to run repeatedly:
  int numBytes;
  char msgBuf[100];
  long currentMillis = millis();
  
  // Flash the onboard LED
  // onboardLED.Update();

  // Check and keep Wifi alive
  wifiStation.keepalive();

  myMQTT.keepalive();

  // numBytes = Serial.available();
  // Serial.println(numBytes);
  //if (numBytes) {
  //  int i = 0;
  //  for(i = 0 ; i < numBytes; i++){
  while(Serial.available() && (serialIndex <= 255)) {
    serialBuf[serialIndex] = Serial.read();
    if (serialBuf[serialIndex] == '\n') {
      serialBuf[serialIndex] = NULL;
      serialIndex = 0;
      if (processMessage(serialBuf))
        // We have processed a reading successfully
        break;
      else
        // We got a garbled line. Let us try the next line
        continue;
    }
    else serialIndex++;
  }
  
  if ( currentMillis >= lastMillis + 1000) {
    if (voltageNow > 195.0 && voltageNow < 250.0) {
        sprintf(msgBuf,"%s/borepump/%s/available", SITE_ADDRESS, VOLTAGE_TOPIC);
        mqttClient.publish(msgBuf, "online");
    }
    else {
        sprintf(msgBuf,"%s/borepump/%s/available", SITE_ADDRESS, VOLTAGE_TOPIC);
        mqttClient.publish(msgBuf, "offline");      
    }
    
    lastMillis = currentMillis;
  }
}
