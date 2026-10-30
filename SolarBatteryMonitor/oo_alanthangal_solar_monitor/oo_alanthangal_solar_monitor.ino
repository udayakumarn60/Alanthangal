#undef DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>
#include <ESP8266Relay.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "atgl-solar-monotor"
#define MY_SSID       "atgl-s24-wifi-hotspot"

// TODO: Remove -test from the strings below before going production

#define CURRENT_TOPIC "battery-current"
#define VOLTAGE_TOPIC "battery-voltage"
#define WATTAGE_TOPIC "battery-wattage"

//

#define SITE_ADDRESS   "/ATGL"
#define INFO_HANDLE    "/INFO"
#define CONFIRM_HANDLE "confirm"

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
// We are using an onboard LED on ESP-01. It is on GPIO01. The default definition for builtin LED is GPIO02

#define ONBOARD_LED GPIO02
//Flasher onboardLED(ONBOARD_LED, 100, 400);  // This is our LED flasher on GPIO01 on an ESP-01
MyWifiSTA wifiStation(MY_SSID, "uday1-mifi", 15);
MyMQTTWrapper myMQTT(&mqttClient, MQTT_SERVER, CONTROLLER_ID , "uday", "swim2sea");

/*
 *  Our ESP01 module object is instantiated below
 */
ESP12Module myESP01(CONTROLLER_ID, SITE_ADDRESS, CONFIRM_HANDLE , INFO_HANDLE, &mqttClient);

char serialBuf[100];
int  serialIndex = 0;

void setup() {
  Serial.begin(9600);

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

void processMessage(char *msgBuf) {
  char currentValue[20];
  char voltageValue[20];
  char wattageValue[20];

  if (msgBuf[0] == 'C') {
    strncpy(currentValue, msgBuf+1, 6);
    currentValue[6] = NULL;
  }
  if (msgBuf[7] == 'V') {
    strncpy(voltageValue, msgBuf+8, 6);
    voltageValue[6] = NULL;
  }
  if (msgBuf[14] == 'W') {
    strncpy(wattageValue, msgBuf+15, 6);
    wattageValue[6] = NULL;
  }
  sprintf(msgBuf,"%s/solar-monitor/%s", SITE_ADDRESS, CURRENT_TOPIC);
  mqttClient.publish(msgBuf, currentValue);
  sprintf(msgBuf,"%s/solar-monitor/%s", SITE_ADDRESS, VOLTAGE_TOPIC);   
  mqttClient.publish(msgBuf, voltageValue);
  sprintf(msgBuf,"%s/solar-monitor/%s", SITE_ADDRESS, WATTAGE_TOPIC);   
  mqttClient.publish(msgBuf, wattageValue);
}

void loop() {
  // loop code, to run repeatedly:
  int numBytes;
  char msgBuf[100];
  
  // Flash the onboard LED
//  onboardLED.Update();

  // Check and keep Wifi alive
  wifiStation.keepalive();

  myMQTT.keepalive();

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
}
