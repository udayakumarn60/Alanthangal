#undef DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "atgl-12v-battery-voltage-current-sensor"
#define MY_SSID       "atgl-s24-wifi-hotspot"
unsigned long wattHours;
unsigned long ampHours;

// TODO: Remove -test from the strings below before going production

#define BATTERY_TOPIC "dw-battery"

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

bool onTrigInProgress = false;
bool offTrigInProgress = false;
bool currentFound = false;
bool voltageFound = false;
bool whFound      = false;
bool ahFound      = false;
typedef enum {MODE1, MODE2, MODE3, MODE4} sensorMode_t;
sensorMode_t presentMode = MODE1;

bool contactorStatus = false;

void processCallback(char* topic, byte* payload, unsigned int t_length) {
  int i = 0;
  char payloadBuf[30];
  char buf[5];
  for (i = 0; i <t_length; i++)
    payloadBuf[i] = payload[i];
  payloadBuf[t_length] = NULL;
  // Serial.print(topic);Serial.print(" "); Serial.println(payloadBuf);
  if (String(topic) == String(SITE_ADDRESS) + '/' + String(BATTERY_TOPIC)) {
    if(!strcmp(payloadBuf, "Mode")){
        // Serial.println("HERE");
        // Serial.println((String(SITE_ADDRESS) + "/confirm/" + String(BATTERY_TOPIC) + "/mode").c_str());
        sprintf(buf,"%d", presentMode + 1);
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(BATTERY_TOPIC) + "/mode").c_str(), buf);      
    }
  }
}

char serialBuf[100];
int  serialIndex = 0;

void subscribeMyTopics(){
/*  myMQTT.subscribe((String(SITE_ADDRESS) + '/' + String(GRID_CONTACTOR)).c_str()); */
  myMQTT.subscribe((String(SITE_ADDRESS) + '/' + String(BATTERY_TOPIC)).c_str());
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  // Serial.println("In Setup");
  // Setup the callback dispatcher  

  // Connect to WiFi
  Serial.println();
  Serial.println("Hello ESP");
  while(!wifiStation.wifiConnected()){
    delay(200);
    wifiStation.reconnectWiFi();
  }
  // Serial.println("Wifi Connected");

  // Loop till MQTT is connected
  while (!myMQTT.connected())
    myMQTT.connectMe();
  // Serial.println("MQTT Connected");
  
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

  myMQTT.setCallback(processCallback);
  subscribeMyTopics();
  // Sync serial input to the beginning of newline
  while (1)
    if(Serial.available() && Serial.read() == '\n')
      break;
  Serial.println("Setup Done");
}

sensorMode_t detectSensorMode(char *msgBuf){
  String testString(msgBuf);
  currentFound = false;
  voltageFound = false;
  whFound      = false;
  ahFound      = false;
  currentFound = testString.indexOf('C') >= 0 ;
  voltageFound = testString.indexOf('V') > 0 ;
  whFound      = testString.indexOf("Wh") > 0 ;
  ahFound      = testString.indexOf("Ah") > 0 ;
  // Serial.print(currentFound);Serial.print(voltageFound);
  // Serial.print(whFound);Serial.println(ahFound);
  if (currentFound && voltageFound && !whFound && !ahFound)
    return(presentMode = MODE1);
  else if (currentFound && voltageFound && whFound && ahFound)
    return(presentMode = MODE2);
  else if (testString.indexOf("O/C") > 0)
    return(presentMode = MODE3);
  else
    return(presentMode = MODE4);
}

void processMessage(char *msgBuf) {
//  char voltageValue[20];
//  char currentValue[20];
  char printBuf[100];
  char *topic = NULL, *currentPtr = NULL, *voltagePtr = NULL;
  int i, len = strlen(msgBuf);
  detectSensorMode(msgBuf);
  for (i = 0; i < len ; i++) {
    switch(msgBuf[i])
    {
      case 'C':
        currentPtr = msgBuf+1;
        break;
      case 'V':
        voltagePtr = msgBuf+i+1;
        break;
      case 'D':
        msgBuf[i] = '\0';
        break;
    }
  }
  mqttClient.publish((String(SITE_ADDRESS) + '/' + String(BATTERY_TOPIC) + "/current").c_str(), currentPtr);
  mqttClient.publish((String(SITE_ADDRESS) + '/' + String(BATTERY_TOPIC) + "/voltage").c_str(), voltagePtr);

}

void loop() {
  // loop code, to run repeatedly:
  int numBytes;
  char msgBuf[100];
  
  // Flash the onboard LED
//  onboardLED.Update();

  // Check and keep Wifi alive
  wifiStation.keepalive();

  if(!myMQTT.keepalive())
    subscribeMyTopics();

  numBytes = Serial.available();
  // Serial.println(numBytes);
  if (numBytes) {
    int i = 0;
    for(i = 0 ; i < numBytes; i++){
      serialBuf[serialIndex] = Serial.read();
      if (serialBuf[serialIndex] == '\n') {
        serialBuf[serialIndex] = NULL;
        serialIndex = 0;
        Serial.println(serialBuf);
        processMessage(serialBuf);
        break;
      }
      else serialIndex++;
    }
  }
}
