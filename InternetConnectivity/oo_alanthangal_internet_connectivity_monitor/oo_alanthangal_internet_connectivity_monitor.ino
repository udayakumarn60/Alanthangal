#define DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>
#include <ESP8266Relay.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "test-internet-connectivity-monotor"
#define MY_SSID       "atgl-s24-wifi-hotspot"
#define SITE_ADDRESS   "/ATGL"
#define INFO_HANDLE    "/INFO"
#define CONFIRM_HANDLE "confirm"
#define HEART_BEAT     "/ATGL/heart-beat"

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// We are using an onboard LED on ESP-01. It is on GPIO02 for newer versions of the ESP. 
// 
#ifndef DEBUG
#define ONBOARD_LED GPIO01
Flasher onboardLED(ONBOARD_LED, 150, 350);  // This is our LED flasher on GPIO01 on an ESP-01
#endif
MyWifiSTA wifiStation(MY_SSID, "uday1-mifi", 15);
MyMQTTWrapper myMQTT(&mqttClient, MQTT_SERVER, CONTROLLER_ID , "uday", "swim2sea");
int missing_heart_beat = 0;

/*
 *  Our ESP01 module object is instantiated below
 */
ESP12Module myESP01(CONTROLLER_ID, SITE_ADDRESS, CONFIRM_HANDLE , INFO_HANDLE, &mqttClient,1,1);

/*
 * Following are our relay objects
 */
ESP8266Relay power_cycler(GPIO02,"power-cycler-01","confirm", &mqttClient, HIGH, LOW, LOW)  ;


ESP8266Relay *relays[1] = {
  &power_cycler,
};


/*
 * Call back dispatcher: loop through each Relay object and give it an opportunity to process the callback
 * 
 */
const int numRelays =  sizeof(relays) / sizeof(ESP8266Relay*);

void processCallback(char* topic, byte* payload, unsigned int t_length) {
  int i = 0;
#ifdef DEBUG
  Serial.print("Callback received: ");
  Serial.print("Topic: ");
  Serial.println(topic);
#endif
  if (String(topic) == String(INFO_HANDLE) + String("/") + String(CONTROLLER_ID)) {
    char payloadBuf[20];
    for (i = 0; i <t_length; i++)
      payloadBuf[i] = payload[i];
    payloadBuf[t_length] = NULL;
    if (!strcmp(payloadBuf, "Status")){
      for (i=0; i< numRelays; i++)
        relays[i]->publishStatus();
    }
    return;
  }
  if(String(topic) == String(HEART_BEAT)) {
#ifdef DEBUG
    Serial.println("Yay!! Got heartbeat");
#endif
    missing_heart_beat = 0;
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

bool power_cycler_triggered = false;
bool mqtt_connection_lost   = false;

static uint8_t              my_state = 0; // Our state variable
static unsigned long        powerCyclerDelay = 40000; // 40 seconds
static unsigned long        routerReconnectWait = 120000 ; // 2 minutes
static unsigned long        mqttReconnectWait = 5000; // 2 seconds
static unsigned long        heartbeatGap = 10000; // 10 seconds
static unsigned long        lastHeartbeat = millis();
static unsigned long        lastOutgoingHeartbeat = millis() ; 
static unsigned long        maxRetrials = 30 ;
static unsigned long        relayTriggeredTime = 0;
static unsigned long        powerRestoredTime  = 0;
static unsigned long        mqttLastReconnect  = 0;

//
// Machine STATES
//

#define STATE_NORMAL          1   // This state keeps checking for the MQTT active connection
#define MQTT_DISCONNECTED     2   // This state preps up for a power cycle of the mobile router
#define POWER_CYCLER_ON       3   // This state switches the power cycle relay on which disconnects power to router
#define POWER_CYCLER_OFF      4   // This state switches the power cycle relay off
#define RECONNECT_MQTT        5   // This state attempts to reconnect to MQTT broker after power cycle
#define RETRY_RECONNECT_MQTT  6   // This state implements a delay before RECONNECT_MQTT is tried again
#define MQTT_RECONNECTED      7   // This state resubcribes to all topics and restores to STATE_NORMAL

void setup() {
#ifdef DEBUG
   Serial.begin(115200);
   delay(100);
   Serial.println("Starting setup");
#endif
  
  // setup code, to run once:

  // Setup the callback dispatcher  
  myMQTT.setCallback(processCallback);

  // Our controller has one relay and one tank level sensor
  myESP01.setActuators(numRelays);
  myESP01.setSensors(0);
  
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
  missing_heart_beat = 0;
  lastHeartbeat = millis();
  myMQTT.subscribe(String(HEART_BEAT).c_str());

  // Let us advertise our controller capabilities
  char msgBuf[100];
  // sprintf(msgBuf, "%s/%s/%s/capability",INFO_HANDLE, CONTROLLER_ID, sensor.getTopic());
  // sensor.publishCapabilities(msgBuf);
  for (int i = 0 ; i < numRelays; i++){
    sprintf(msgBuf, "%s/%s/%s/capability",INFO_HANDLE, CONTROLLER_ID, relays[i]->getTopic());
#ifdef DEBUG
    Serial.println(msgBuf);
#endif
    relays[i]->publishCapabilities(msgBuf);
  }
  // Let us subscribe for the module level topic which is /<information handle>/<controller name>
  myESP01.subscribe();
  
  delay(1000);

  // Subscribe the relays topics to MQTT broker
  // sensor.subscribe();
  subscribeRelays();
  my_state = STATE_NORMAL;
}

static unsigned long mqttFailCounter = 0;
static unsigned long mqttConnectRetryCount = 0;


void loop() {

  // loop code, to run repeatedly:

  long time_now = millis();
  
#ifndef DEBUG
  // Flash the onboard LED
  onboardLED.Update();
#endif

  // Check and keep Wifi alive
  wifiStation.keepalive();
  mqttClient.loop();
  switch (my_state) {
    case STATE_NORMAL:
    
      // Check for MQTT active connection
      if( ! myMQTT.connected()) {
        mqttFailCounter ++;
        delay(60);
        if (mqttFailCounter >= 1000) {
           my_state = MQTT_DISCONNECTED;
#ifdef DEBUG
           Serial.println("State set to MQTT_DISCONNECTED");
#endif
        }
      } else {
        mqttFailCounter = 0;
      }

      if (time_now >= (lastOutgoingHeartbeat + heartbeatGap)){
        
        // Time to publish a heartbeat to MQTT Server
        
        lastOutgoingHeartbeat = time_now;
#ifdef DEBUG
        Serial.println("Sending outgoing heart beat.");
#endif
        power_cycler.publish("mqtt-ok");
      }
      
      if (time_now >= (lastHeartbeat + 30000)) {
#ifdef DEBUG
        Serial.print("Missing heartbeat counter : ");
        Serial.println(missing_heart_beat);
#endif
        missing_heart_beat++;
        lastHeartbeat = time_now;
      }
      
      if (missing_heart_beat > 10) {
#ifdef DEBUG
        Serial.println("Incoming heart beats have been missing");
#endif
        // Publish a message that we have not been receiving heartbeats
        power_cycler.publish("mqtt-mh");
        missing_heart_beat = 0;
      }
      
      return;
        
    case MQTT_DISCONNECTED:
    
      // Trigger the power cycler - so power to the mobile router will be disconnected
      relayTriggeredTime = millis();
      // relays[0]->process("/ATGL/power-cycler-01", (byte *)"On", 2);
      relays[0]->setRelayOn("On");
      delay(50);
#ifdef DEBUG
      Serial.println("Power to mobile router disconnected.");
#endif
      my_state = POWER_CYCLER_ON;
      return;

    case POWER_CYCLER_ON:
    
      if (( relayTriggeredTime + powerCyclerDelay) <= time_now )
      {
        // Switch the power cycler back to provide power to the mobile router
        // relays[0]->process("/ATGL/power-cycler-01", (byte *)"Off", 3);
        relays[0]->setRelayOff("Off");
#ifdef DEBUG
        Serial.println("Power to mobile router reconnected.");
#endif
        my_state = POWER_CYCLER_OFF;
        powerRestoredTime = millis ();
      }
      delay(50);
      return;
    
    case POWER_CYCLER_OFF:

      // Now that mobile router is powered on, we have to wait for the mobile router
      // to establish its internet connection

      if ((powerRestoredTime + routerReconnectWait) <= time_now)
      {
        // Our wait is complete. Let us reconnect MQTT
#ifdef DEBUG
        Serial.println("Wait for router to reconnect to network is complete.");
#endif
        my_state = RECONNECT_MQTT ;
        mqttConnectRetryCount = 0;
      }
      // Our wait is not yet complete. Do nothing
      delay(50);
      return;
      
    case RECONNECT_MQTT:
    
      // Try reconnecting MQTT
      myMQTT.connectMe();
      delay(500);
      if(!myMQTT.connected()) {
        mqttLastReconnect = millis();
        my_state = RETRY_RECONNECT_MQTT;
#ifdef DEBUG
        Serial.println("Waiting to establish MQTT connection.");
#endif
      } else {
        my_state = MQTT_RECONNECTED ;
#ifdef DEBUG
        Serial.println("MQTT connection established.");
#endif          
      }
      return;
      
    case RETRY_RECONNECT_MQTT:

      // Wait for the Reconnect time out

      if ((mqttLastReconnect + mqttReconnectWait) > time_now) {
        delay(50);
      } else {
        if(mqttConnectRetryCount++ >= maxRetrials) {
          // After a powercycle and maxRetrials we are not getting an MQTT connection. 
          // Let us repeat the power cycle operation and reboot the router
          mqttConnectRetryCount = 0;
#ifdef DEBUG
          Serial.println("Power cycling again because MQTT is not reconnecting.");
#endif
          my_state = MQTT_DISCONNECTED ;
        }
        else
          my_state = RECONNECT_MQTT;
      }
      return;

    case MQTT_RECONNECTED:

        // We are connected again. Let us resubscribe to our topics and get back into
        // business as usual
 #ifdef DEBUG
        Serial.println("Subscribing our MQTT topics.");
#endif       
        subscribeRelays();
        myMQTT.subscribe(String(HEART_BEAT).c_str());
        
        // We should publish that we recovered from an MQTT disconnection and also provide info
        // on the (1) number of power cycles required to recover (20 time of recovery
        power_cycler.publish("mqtt-rc");
        my_state = STATE_NORMAL;
        mqttConnectRetryCount = 0;
        missing_heart_beat = 0;
        return;        
  }
}
