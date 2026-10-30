#undef DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>
#include <ESP8266Relay.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "atgl-driveway-lights-controller"
#define MY_SSID       "atgl-s24-wifi-hotspot"
#define SITE_ADDRESS   "/ATGL"
#define INFO_HANDLE    "/INFO"
#define CONFIRM_HANDLE "confirm"

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
// We are using an onboard LED on ESP-01. It is on GPIO01. The default definition for builtin LED is GPIO02

#define ONBOARD_LED GPIO01
Flasher onboardLED(ONBOARD_LED, 150, 350);  // This is our LED flasher on GPIO01 on an ESP-01
MyWifiSTA wifiStation(MY_SSID, "uday1-mifi", 15);
MyMQTTWrapper myMQTT(&mqttClient, MQTT_SERVER, CONTROLLER_ID , "uday", "swim2sea");

/*
 *  Our ESP01 module object is instantiated below
 */
ESP12Module myESP01(CONTROLLER_ID, SITE_ADDRESS, CONFIRM_HANDLE , INFO_HANDLE, &mqttClient);

/*
 * Following are our relay objects
 */
ESP8266Relay driveway_front_lights(GPIO02,"driveway-lights","confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266Relay driveway_mid_lights(GPIO00,"remote-switch","confirm", &mqttClient, LOW, HIGH, HIGH)  ;

ESP8266Relay *relays[2] = {
  &driveway_front_lights,
  &driveway_mid_lights
};


/*
 * Call back dispatcher: loop through each Relay object and give it an opportunity to process the callback
 * 
 */
const int numRelays =  sizeof(relays) / sizeof(ESP8266Relay*);

void processCallback(char* topic, byte* payload, unsigned int t_length) {
  int i = 0;
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

void setup() {
#ifdef DEBUG
   Serial.begin(115200);
   delay(100);
   Serial.println("Starting setup");
#endif   
  // setup code, to run once:

  // Setup the callback dispatcher  
  myMQTT.setCallback(processCallback);

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
  while (!myMQTT.connected())
    myMQTT.connectMe();

#ifdef DEBUG
  Serial.println("MQTT connected");
#endif

  // Let us advertise our controller name and site address
  myESP01.advertise();

  // Let us advertise our controller capabilities

  for (int i = 0 ; i < numRelays; i++){
    char msgBuf[100];
    sprintf(msgBuf, "%s/%s/%s/capability",INFO_HANDLE, CONTROLLER_ID, relays[i]->getTopic());
#ifdef DEBUG
    Serial.println(msgBuf);
#endif
    relays[i]->publishCapabilities(msgBuf);
    delay(20);
  }
  // Let us subscribe for the module level topic which is /<information handle>/<controller name>
  myESP01.subscribe();
  
  delay(1000);

  // Subscribe the relays topics to MQTT broker
  subscribeRelays();
}

void processSwitchTimers() {
  int i = 0;
  for (i=0 ; i < numRelays; i++)
    relays[i]->loop();
}

void loop() {
  // loop code, to run repeatedly:

  // Flash the onboard LED
  onboardLED.Update();

  // Check and keep Wifi alive
  wifiStation.keepalive();
  processSwitchTimers();

  // Check and keep MQTT connection alive
  if (!myMQTT.keepalive()){
    // We got reconnected. So we need to resubscribe
    subscribeRelays();
  }
}
