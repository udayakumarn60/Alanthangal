#define DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper-v1.h>
#include <ESP12Module.h>
#include <ESP8266Relay-v1.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define BACKUP_SERVER "hassio.local"
#define CONTROLLER_ID "atgl-street-lights-controller"
#define MY_SSID       "atgl-s24-wifi-hotspot"
#define SITE_ADDRESS   "/ATGL"
#define INFO_HANDLE    "/INFO"
#define CONFIRM_HANDLE "confirm"

WiFiClient wifiClient;
PubSubClient mqttClient_primary(wifiClient);
PubSubClient mqttClient_backup(wifiClient);
// We are using an onboard LED on ESP-01. It is on GPIO01. The default definition for builtin LED is GPIO02

#define ONBOARD_LED GPIO01
Flasher onboardLED(ONBOARD_LED, 100, 400);  // This is our LED flasher on GPIO01 on an ESP-01
MyWifiSTA wifiStation(MY_SSID, "uday1-mifi", 15);
MyMQTTWrapper myMQTT(&mqttClient_primary, &mqttClient_backup, MQTT_SERVER, BACKUP_SERVER, CONTROLLER_ID , "uday", "swim2sea");

/*
 *  Our ESP01 module object is instantiated below
 */
ESP12Module myESP01(CONTROLLER_ID, SITE_ADDRESS, CONFIRM_HANDLE , INFO_HANDLE, &mqttClient_primary);

/*
 * Following are our relay objects
 */
ESP8266Relay front_gate_lights(GPIO02,"gate-street-light","confirm", &myMQTT, LOW, HIGH, HIGH)  ;
ESP8266Relay rear_gate_lights(GPIO00,"driveway-street-light","confirm", &myMQTT, LOW, HIGH, HIGH)  ;

ESP8266Relay *relays[2] = {
  &front_gate_lights,
  &rear_gate_lights
};


/*
 * Call back dispatcher: loop through each Relay object and give it an opportunity to process the callback
 * 
 */
const int numRelays =  sizeof(relays) / sizeof(ESP8266Relay*);

void processCallback(char* topic, byte* payload, unsigned int t_length) {
  int i = 0;
#ifdef DEBUG
  Serial.print("In processCallback: ");
  Serial.println(topic);
#endif
  if (String(topic) == String(INFO_HANDLE) + String(CONTROLLER_ID)) {
    char payloadBuf[20];
    for (i = 0; i <t_length; i++)
      payloadBuf[i] = payload[i];
    payloadBuf[t_length] = NULL;
    if (!strcmp(payloadBuf, "Status"))
      for (i=0; i< numRelays; i++)
        relays[i]->publishStatus();
    return;
  }
  for (i=0 ; i < numRelays; i++)
    if (relays[i]->process(topic, payload, t_length))
      break;
}

inline void subscribeRelays() {
  for (int i=0 ; i < numRelays; i++)
    relays[i]->subscribe();  
}

void mqttDisconnectCallback() {
  // If MQTT disconnects,we want to simply switch off the relays
  int i = 0 ;
  for (i = 0 ; i < numRelays ; i++)
    relays[i]->setRelayOff("Off");
}

void setup() {
#ifdef DEBUG
   Serial.begin(115200);
   delay(100);
   Serial.println("Starting setup");
#endif   
  // setup code, to run once:

  // Setup the callback dispatcher  
  myMQTT.setCallback(processCallback);
  // myMQTT.setDisconnectCB(mqttDisconnectCallback);

  myESP01.setActuators(numRelays);
  
#ifdef DEBUG
  Serial.println("Starting Wifi");
#endif

  // Connect to WiFi

  while(!wifiStation.wifiConnected()){
    delay(500);
    wifiStation.reconnectWiFi();
  }
#ifdef DEBUG
  Serial.print(WiFi.status());
  Serial.println("Wifi connected");
#endif

  // Loop till MQTT is connected
  while (!myMQTT.connected()) {
    delay(250);
    myMQTT.connectMe();
  }
  
#ifdef DEBUG
  Serial.println("MQTT connected");
#endif

  // Let us advertise our controller name and site address
  myESP01.advertise();

  // Let us advertise our controller capabilities

  for (int i = 0 ; i < numRelays; i++){
    char msgBuf[150];
    sprintf(msgBuf, "%s/%s/%s/capability",INFO_HANDLE, CONTROLLER_ID, relays[i]->getTopic());
#ifdef DEBUG
    Serial.println(msgBuf);
#endif
    relays[i]->publishCapabilities(msgBuf);
    //mqttClient.publish(msgBuf, relays[i]->capability());
    delay(20);
  }
  // Let us subscribe for the module level topic which is /<information handle>/<controller name>
  myESP01.subscribe();
  
  delay(1000);

  // Subscribe the relays topics to MQTT broker
  subscribeRelays();
}

void loop() {
  // loop code, to run repeatedly:

  // Flash the onboard LED
  
  onboardLED.Update();

  // Check and keep Wifi alive
  wifiStation.keepalive();

  // Check and keep MQTT connection alive
  if (!myMQTT.keepalive()){
    // We got reconnected. So we need to resubscribe
    subscribeRelays();
  }
  delay(50);

}
