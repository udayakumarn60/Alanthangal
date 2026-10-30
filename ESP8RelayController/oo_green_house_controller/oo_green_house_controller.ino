#undef DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>
#include <ESP8266RelaySerial.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "atgl-green-house-controller-1"
#define MY_SSID       "atgl-s24-wifi-hotspot"
#define SITE_ADDRESS   "/ATGL"
#define INFO_HANDLE    "/INFO"
#define CONFIRM_HANDLE "confirm"


WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

Flasher led1(ONBOARD_LED, 100, 400);  // This is our LED flasher on GPIO02 on an ESP-12e
MyWifiSTA wifiStation(MY_SSID, "uday1-mifi", 15);
MyMQTTWrapper myMQTT(&mqttClient, MQTT_SERVER, CONTROLLER_ID , "uday", "swim2sea");

/*
    Our ESP12 module object is instantiated below
*/
ESP12Module myESP12(CONTROLLER_ID, SITE_ADDRESS, CONFIRM_HANDLE , INFO_HANDLE, &mqttClient);

/*
   Following are our relay objects
*/
ESP8266RelaySerial Relay_1("R_1", "Relay_1", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266RelaySerial Relay_2("R_2", "Relay_2", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266RelaySerial Relay_3("R_3", "Relay_3", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266RelaySerial Relay_4("R_4", "Relay_4", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266RelaySerial Relay_5("R_5", "Relay_5", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266RelaySerial Relay_6("R_6", "Relay_6", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266RelaySerial Relay_7("R_7", "Relay_7", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;
ESP8266RelaySerial Relay_8("R_8", "Relay_8", "confirm", &mqttClient, LOW, HIGH, HIGH)  ;

ESP8266RelaySerial *relays[8] = {
  &Relay_1,
  &Relay_2,
  &Relay_3,
  &Relay_4,
  &Relay_5,
  &Relay_6,
  &Relay_7,
  &Relay_8
};


/*
   Call back dispatcher: loop through each Relay object and give it an opportunity to process the callback

*/
const int numRelays =  sizeof(relays) / sizeof(ESP8266RelaySerial*);

void processCallback(char* topic, byte* payload, unsigned int t_length) {
  int i = 0;
  if (String(topic) == String(INFO_HANDLE) + String(CONTROLLER_ID)) {
    char payloadBuf[20];
    for (i = 0; i < t_length; i++)
      payloadBuf[i] = payload[i];
    payloadBuf[t_length] = NULL;
    if (!strcmp(payloadBuf, "Status"))
      for (i = 0; i < numRelays; i++)
        relays[i]->publishStatus();
    return;
  }
  for (i = 0 ; i < numRelays; i++)
    if (relays[i]->process(topic, payload, t_length))
      break;
}

inline void subscribeRelays() {
  for (int i = 0 ; i < numRelays; i++)
    relays[i]->subscribe();
}

void setup() {

  Serial.begin(115200);
  delay(100);
#ifdef DEBUG
  Serial.println("Starting setup");
#endif
  // setup code, to run once:

  // Setup the callback dispatcher
  myMQTT.setCallback(processCallback);

  myESP12.setActuators(numRelays);

#ifdef DEBUG
  Serial.println("Starting Wifi");
#endif

  // Connect to WiFi

  while (!wifiStation.wifiConnected()) {
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
  myESP12.advertise();

  // Let us advertise our controller capabilities

  for (int i = 0 ; i < numRelays; i++) {
    char msgBuf[500];
    sprintf(msgBuf, "%s/%s/%s/capability", INFO_HANDLE, CONTROLLER_ID, relays[i]->getTopic());
#ifdef DEBUG
    Serial.println(msgBuf);
#endif
    relays[i]->publishCapabilities(msgBuf);
    delay(20);
  }
  // Let us subscribe for the module level topic which is /<information handle>/<controller name>
  myESP12.subscribe();

  delay(1000);

  // Subscribe the relays topics to MQTT broker
  subscribeRelays();
}

void loop() {
  // loop code, to run repeatedly:

  // Flash the onboard LED
  led1.Update();

  // Check and keep Wifi alive
  wifiStation.keepalive();

  // Check and keep MQTT connection alive
  if (!myMQTT.keepalive()) {
    // We got reconnected. So we need to resubscribe
    subscribeRelays();
  }
}
