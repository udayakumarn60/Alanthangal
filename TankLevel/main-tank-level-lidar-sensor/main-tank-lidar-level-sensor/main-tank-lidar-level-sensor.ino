
/*
************************************************************************
* Water level sensor using Tiny VL53L0X LIDAR Sensor - Pilot 09-Jan-2020
* We need to use NodeMCU Board package to compile this code
************************************************************************
*/

#include "ESP12Module.h"
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include "Adafruit_VL53L0X.h"
#include "LEDFlasher.h"

#define DEBUG

#define ONBOARD_LED GPIO02  // GPIO-2 is the pin for Onboard LED in NodeMCU
Flasher onboardLED(ONBOARD_LED, 100, 400);  // This is our LED flasher
Adafruit_VL53L0X lox = Adafruit_VL53L0X();


// WiFI/MQTT Configuration Setup

#define MQTT_SERVER "atgl.udayakumarn.in"

const char* ssid = "atgl-s24-wifi-hotspot";
const char* password = "uday1-mifi";


const char* controlTopic = "/ATGL/main-tank-level";
const char* confirmTopic = "/ATGL/confirm";
const char* stTankId     = "main-tank-level";
const char* stSSID      = "main-tank-level/ssid";
const char* stMQTT      = "main-tank-level/mqtt";

static int   ReconnectTrials = 0;

#define WIFI_RECONNECT_DELAY 15000
#define MQTT_RECONNECT_DELAY 30000
 
WiFiClient wifiClient;
PubSubClient client(wifiClient);

void reconnect(bool);

void Confirm(const char *subTopic, const char *msg) {
  static char tmpBuf[50];
  if (client.connected()){
    if (subTopic) {
      sprintf(tmpBuf, "%s/%s",confirmTopic, subTopic);
      client.publish(tmpBuf, msg, false);
    }
    else
      client.publish(confirmTopic, msg, false);
  }
}

static int lastMeasure=130; // We set the last measurement as 130 cm. The LIDAR sensor's accurate range is 1.2 m

void publishStatus() {
    // Let us publish last measurement
    char msgBuf[20];
    sprintf(msgBuf,"%d", lastMeasure);
    Confirm(stTankId, msgBuf);
}

void callback(char* topic, byte* payload, unsigned int length) {

  //convert topic to string to make it easier to work with
  String topicStr = topic; 
  String payloadStr = (char *)payload;
  char payloadBuf[15];
  short int i;

  for (i=0; i<length; i++)
    payloadBuf[i] = payloadStr[i];
  payloadBuf[i] = '\0';
  
  //Print out some debugging info
  // Serial.println("Callback update.");
  // Serial.print("Topic: ");
  // Serial.println(topicStr);
  // Serial.println(payloadBuf);
  
  if (!strcmp(payloadBuf, "Status")){
    publishStatus();
  }
}

static long lastMillis;
static long lastLIDARCheck;


void setup() {
  bool reconnect_flag = true;

#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("");
  
  // wait until serial port opens for native USB devices
  while (! Serial)
  {
    delay(10);
  }

  Serial.println("Serial port opened.");
#endif

  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);

  //start wifi subsystem
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  // attempt to connect to the WIFI network and then connect to the MQTT server
  // Initial call to reconnect should have a boolean TRUE argument
  delay(2000);
  reconnect(reconnect_flag);
  reconnect_flag = false;
#ifdef DEBUG
  Serial.println("setup:reconnect complete.");
#endif

  // Wait a bit 
  delay(2000);
  
  while (!lox.begin())
  {
#ifdef DEBUG
    Serial.println("LIDAR Sensor Initialisation failed. Retrying");
#endif
//    Confirm(stTankId, "sensor fail");
    delay(500);
  }

#ifdef DEBUG
  Serial.println("LIDAR initialized.");
#endif
 
  lastMillis = millis();
  lastLIDARCheck = lastMillis;
}

#define LIDAR_CHECK_INTERVAL 200
#define BUFFER_SIZE 10

int my_index = 0;
bool firstCycleDone = false;
int measures[BUFFER_SIZE];

int averageDistance() {
  int i;
  int sum = 0;
  for (i=0; i <BUFFER_SIZE; i++) 
    sum += measures[i];
  return (sum/BUFFER_SIZE);
}

void loop() {
  VL53L0X_RangingMeasurementData_t currentMeasure;
  long now = millis();
  bool statusTime = false;
  // Flash the onboard LED
  onboardLED.Update();
  client.loop();
  lox.rangingTest(&currentMeasure, false); // pass in 'true' to get debug data printout!

  // Maximum accurate range of this LIDAR sensor is 1200 mm
  
  if (currentMeasure.RangeStatus == 4) currentMeasure.RangeMilliMeter = 1200;
  
  if ((lastLIDARCheck + LIDAR_CHECK_INTERVAL) <= now) {
    if (!firstCycleDone) {
      measures[my_index++] = currentMeasure.RangeMilliMeter / 10;
      delay(100);
      if (my_index >= BUFFER_SIZE) {
        firstCycleDone = true;
        lastMeasure = averageDistance();
        statusTime = true;
        my_index = 0;
      }
    } else {
      measures[my_index++] = currentMeasure.RangeMilliMeter / 10;

      lastLIDARCheck = millis();
      if (my_index >= BUFFER_SIZE) {
        lastMeasure = averageDistance();
        statusTime = true;
        my_index = 0; 
      }
    }
  }
  
  //check & reconnect if connection is lost
  if (!client.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      // WiFi disconnected. We will wait for 2.5 minutes and
      // try reconnecting
      ReconnectTrials++;
      if (ReconnectTrials > WIFI_RECONNECT_DELAY) {
#ifdef DEBUG
        Serial.println("Reconnecting...");
#endif
        reconnect(false);
      }
    }
    else {
      // Reconnect to MQTT ASAP if we did not lose WiFi
      reconnect(false);
    }
  }
  if (client.connected()) {
    ReconnectTrials = 0;
    if (statusTime) {
      publishStatus();
      statusTime = false;
    }
  }
  
  //MUST delay to allow ESP8266 WIFI functions to run
  delay(10); 
  
}

#define MAX_WIFI_RETRY_COUNT 10

static unsigned long lastMQTTchk = 0;

void reconnect(bool initial) {
  int reconnect_attempt = 0;
  int currMQTTchk = 0;
  //attempt to connect to the wifi if connection is lost
  if(WiFi.status() != WL_CONNECTED){
    //debug printing
    // Serial.print("Connecting to ");
    // Serial.println(ssid);
    if (!initial) {
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
    //loop while we wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      // Serial.print(".");
      reconnect_attempt ++;
      if (reconnect_attempt >= MAX_WIFI_RETRY_COUNT)
      {
        return ;
      }
    }
    Confirm(stSSID , ssid);
    //print out some more debug once connected
#ifdef DEBUG
    Serial.println("");
    Serial.println("WiFi connected");  
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
#endif
  }

  // make sure we are connected to WIFI before attempting 
  // to connect to MQTT
  if(WiFi.status() == WL_CONNECTED){
    currMQTTchk = millis();
    if (currMQTTchk - lastMQTTchk > MQTT_RECONNECT_DELAY ) {
      if (!client.connected()) {
#ifdef DEBUG
        Serial.print("Client status is : ");
        Serial.println(client.state());
#endif
        client.disconnect();
        // Serial.print("Attempting MQTT connection...");

        // Generate client name based on MAC address and last 8 bits of microsecond counter
        String clientName;
        clientName += "esp8266-";
        uint8_t mac[6];
        WiFi.macAddress(mac);
//        clientName += macToStr(mac);

        //if connected, subscribe to the topic(s) we want to be notified about
      
        if (client.connect("atgl-main-tank-lidar-sensor", "uday", "swim2sea")) {
          // Serial.println("\tMQTT Connected");
          // Serial.print("Client status is : ");
          // Serial.println(client.state());
          client.subscribe(controlTopic);
          Confirm(stMQTT , MQTT_SERVER);
        }

        //otherwise print failed for debugging
        else{
#ifdef DEBUG
          Serial.print("\tFailed. rc="); 
          Serial.println(client.state());
#endif
          // abort();
        }
      }
      lastMQTTchk = currMQTTchk ;
    }
  }  
}

//generate unique name from MAC addr
//String macToStr(const uint8_t* mac){  
//  String result = "";
//  for (int i = 0; i < 6; ++i) {
//    result += String(mac[i], 16);
//
//    if (i < 5){
//      result += ':';
//    }
//  }
//
//  Serial.println(result);
//  return result;
//}
 
