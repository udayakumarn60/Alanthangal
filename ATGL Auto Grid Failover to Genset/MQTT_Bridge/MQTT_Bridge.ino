#undef DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "atgl-genset-mqtt-bridge"
#define MY_SSID       "atgl-s24-wifi-hotspot"

#define GEN_Topic "dg-contactor-switch" // DG Contactor on AMFi panel
#define EB_Topic  "eb-contactor-switch" // Mains Contactor on AMFi panel
#define BAT_Topic "bat-relay-switch"    // Battery Charging Relay
#define VOLT_TOPIC "dg-bat-voltage"     // DG Set Battery Voltage
#define GS_Topic "dg-starter-switch"    // DG Starter Switch

#define MG_ROTARY "mg-rotary-switch"    // Mains Genset Rotary Switch
#define AM_ROTARY "am-rotary-switch"    // Auto Manual Rotary Switch
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

bool dg_contactor_state = false;
bool eb_contactor_state = false;
bool bat_relay_state = false;
bool gs_relay_state = false;

void processCallback(char* topic, byte* payload, unsigned int t_length) {
  int i = 0;
  char payloadBuf[20];
  for (i = 0; i <t_length; i++)
    payloadBuf[i] = payload[i];
  payloadBuf[t_length] = NULL;
  Serial.print(topic);Serial.print(" "); Serial.println(payloadBuf);
  // Proceess GENSET Command from MQTT
  if (String(topic) == String(SITE_ADDRESS) + '/' + String(GEN_Topic)) {
    if(!strcmp(payloadBuf, "Status")){
      if (dg_contactor_state)
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GEN_Topic)).c_str(), "On");
      else
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GEN_Topic)).c_str(), "Off");
      return;
    }
    if (!strcmp(payloadBuf, "On")) {
      Serial.println("G1");
    } else if (!strcmp(payloadBuf, "Off")) {
      Serial.println("G0");
    }
  } else
  // Process Mains Command from MQTT
  if (String(topic) == String(SITE_ADDRESS) + '/' + String(EB_Topic)) {
    if(!strcmp(payloadBuf, "Status")){
      if (eb_contactor_state)
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(EB_Topic)).c_str(), "On");
      else
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(EB_Topic)).c_str(), "Off");
      return;
    }
    if (!strcmp(payloadBuf, "On")) {
      Serial.println("M1");
    } else if (!strcmp(payloadBuf, "Off")) {
      Serial.println("M0");
    }
  } else
  // Process Battery Charger Command from MQTT
  if (String(topic) == String(SITE_ADDRESS) + '/' + String(BAT_Topic)) {
    if(!strcmp(payloadBuf, "Status")){
      if (bat_relay_state)
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(BAT_Topic)).c_str(), "On");
      else
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(BAT_Topic)).c_str(), "Off");
      return;
    }
    if (!strcmp(payloadBuf, "On")) {
      Serial.println("C1");
    } else if (!strcmp(payloadBuf, "Off")) {
      Serial.println("C0");
    }
  } else
  // Process Generator Start/Stop Command from MQTT
  if (String(topic) == String(SITE_ADDRESS) + '/' + String(GS_Topic)) {
    if(!strcmp(payloadBuf, "Status")){
      if (gs_relay_state)
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GS_Topic)).c_str(), "On");
      else
        mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GS_Topic)).c_str(), "Off");
      return;
    }
    if (!strcmp(payloadBuf, "On")) {
      Serial.println("S1");
    } else if (!strcmp(payloadBuf, "Off")) {
      Serial.println("S0");
    }
  }
}

char serialBuf[100];
int  serialIndex = 0;

// Subscribe to our relevant MQTT topics
void subscribeMyTopics(){
  myMQTT.subscribe((String(SITE_ADDRESS) + '/' + String(GEN_Topic)).c_str());
  myMQTT.subscribe((String(SITE_ADDRESS) + '/' + String(EB_Topic)).c_str());
  myMQTT.subscribe((String(SITE_ADDRESS) + '/' + String(BAT_Topic)).c_str());
  myMQTT.subscribe((String(SITE_ADDRESS) + '/' + String(GS_Topic)).c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  // Serial.println("In Setup");
  // Setup the callback dispatcher  

  // Connect to WiFi
  Serial.println();
  Serial.println("Hello DG AMFi");
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

  
  delay(1000);

  myMQTT.setCallback(processCallback);
  subscribeMyTopics();
  // Sync serial input to the beginning of newline
  while (1)
    if(Serial.available() && Serial.read() == '\n')
      break;
}

void processMessage(char *msgBuf) {
  char printBuf[100];
  char *topic = NULL;
  
  // Process message received from Arduino Nano

  switch (msgBuf[0]) {
    case 'G':
      if (msgBuf[1] == '0') // Gen Off Command was sent to Arduino Nano
        switch(msgBuf[2]) {
          case '1':
            // Off succcessfully executed
            dg_contactor_state = false;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GEN_Topic)).c_str(), "Off");
            break;
        }
      else if (msgBuf[1] == '1') // Gen On Command was sent
        switch(msgBuf[2]) {
          case '1':
            // On succcessfully executed
            dg_contactor_state = true;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GEN_Topic)).c_str(), "On");
            break;
        }
      return;
    case 'M':
      if (msgBuf[1] == '0') // Mains Off Command was sent
        switch(msgBuf[2]) {
          case '1':
            // Off succcessfully executed
            eb_contactor_state = false;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(EB_Topic)).c_str(), "Off");
            break;
        }
      else if (msgBuf[1] == '1') // Mains On Command was sent
        switch(msgBuf[2]) {
          case '1':
            // On succcessfully executed
            eb_contactor_state = true;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(EB_Topic)).c_str(), "On");
            break;
        }
      return;
    case 'C':
      if (msgBuf[1] == '0') // Battery Charging Off Command was sent
        switch(msgBuf[2]) {
          case '1':
            // Off succcessfully executed
            bat_relay_state = false;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(BAT_Topic)).c_str(), "Off");
            break;
        }
      else if (msgBuf[1] == '1') // Battery Charging On Command was sent
        switch(msgBuf[2]) {
          case '1':
            // On succcessfully executed
            bat_relay_state = true;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(BAT_Topic)).c_str(), "On");
            break;
        }
      return;
    case 'S':
      if (msgBuf[1] == '0') // Genset Stop Command was sent
        switch(msgBuf[2]) {
          case '1':
            // Off succcessfully executed
            gs_relay_state = false;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GS_Topic)).c_str(), "Off");
            break;
        }
      else if (msgBuf[1] == '1') // Genset Start Command was sent
        switch(msgBuf[2]) {
          case '1':
            // On succcessfully executed
            gs_relay_state = true;
            mqttClient.publish((String(SITE_ADDRESS) + "/confirm/" + String(GS_Topic)).c_str(), "On");
            break;
        }
      return;
    case 'X':
      // Positions of Mains/Get Selector Switch
      switch (msgBuf[1]){
        case '1':
          mqttClient.publish((String(SITE_ADDRESS) + '/' + String(MG_ROTARY)).c_str(), "Mains");
          break;
        case '2':
          mqttClient.publish((String(SITE_ADDRESS) + '/' + String(MG_ROTARY)).c_str(), "Genset");
          break;
      }
      return;
    case 'Y':
      // Position of Auto/Manual/Off Switch
      switch (msgBuf[1]){
        case '2':
          mqttClient.publish((String(SITE_ADDRESS) + '/' + String(AM_ROTARY)).c_str(), "Manual");
          break;
        case '1':
          mqttClient.publish((String(SITE_ADDRESS) + '/' + String(AM_ROTARY)).c_str(), "Off");
          break;
        case '0':
          mqttClient.publish((String(SITE_ADDRESS) + '/' + String(AM_ROTARY)).c_str(), "Auto");
          break;
      }
      return;
    case 'B':
      // Battery Voltage value
      strcpy(printBuf, msgBuf+1);
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(VOLT_TOPIC)).c_str(), printBuf);
      return;
    default:
      return;
  };
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
        processMessage(serialBuf);
        break;
      }
      else serialIndex++;
    }
  }
}