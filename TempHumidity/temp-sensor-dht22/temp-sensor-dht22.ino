//ItKindaWorks - Creative Commons 2016
//github.com/ItKindaWorks
//
//Requires PubSubClient found here: https://github.com/knolleary/pubsubclient
//
//ESP8266 MQTT temp sensor node


#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <DHT.h>

#define DHTPIN  2

//DHT 22 is on PIN 2
DHT dht(DHTPIN, DHT22 /*,20*/);


//EDIT THESE LINES TO MATCH YOUR SETUP
//#define MQTT_SERVER "192.168.0.3"
#define MQTT_SERVER "13.232.180.158"
const char* ssid = "atgl-s24-wifi-hotspot";
const char* password = "uday1-mifi";


//topic to publish to for the temperature
char* tempTopic = "/ATGL/temp-1";
char* humidTopic = "/ATGL/humid-1";

//MQTT callback
void callback(char* topic, byte* payload, unsigned int length) {}

WiFiClient wifiClient;
PubSubClient client(MQTT_SERVER, 1883, callback, wifiClient);

void setup() {
  
  // Start the Sensor
  dht.begin();

  //start the serial line for debugging
  Serial.begin(115200);
  delay(100);

  // start wifi subsystem
  WiFi.begin(ssid, password);

  // attempt to connect to the WIFI network and then 
  // connect to the MQTT server
  reconnect();

  //wait a bit before starting the main loop
      delay(1000);
}


static char msgBuf[50];

void loop(){

  float h = dht.readHumidity();
  // Read temperature in Celcius (default)
  float f = dht.readTemperature();

  dtostrf(f, 6, 2, msgBuf);
  //publish the new temperature
  client.publish(tempTopic, msgBuf);

  dtostrf(h, 6, 2, msgBuf);
  //publish the new temperature
  client.publish(humidTopic, msgBuf);
  
  //reconnect if connection is lost
  if (!client.connected() && WiFi.status() == 3) {reconnect();}
  //maintain MQTT connection
  client.loop();
  // Take temperature every 15 seconds
  delay(15000); 
}


//networking functions

void reconnect() {

  //attempt to connect to the wifi if connection is lost
  if(WiFi.status() != WL_CONNECTED){

    //loop while we wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }

  }

  //make sure we are connected to WIFI before attemping to reconnect to MQTT
  if(WiFi.status() == WL_CONNECTED){
  // Loop until we're reconnected to the MQTT server
    while (!client.connected()) {

      // Generate client name based on MAC address and last 8 bits of microsecond counter
      String clientName;
      clientName += "esp8266-";
      uint8_t mac[6];
      WiFi.macAddress(mac);
      clientName += macToStr(mac);

      //if connected, subscribe to the topic(s) we want to be notified about
      if (client.connect((char*) "atgl-temp-sensor", "uday", "swim2sea")) {
        //subscribe to topics here
      }
    }
  }
}

//generate unique name from MAC addr
String macToStr(const uint8_t* mac){

  String result;

  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);

    if (i < 5){
      result += ':';
    }
  }

  return result;
}


