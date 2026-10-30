
/*
*****************************************************
* Water level sensor using SR04M-2 - Pilot 13-Mar-2022
*****************************************************
*/
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>

#define ITERATIONS     5 // Number of iterations.
#define TRIGGER_PIN    2 // Arduino pin tied to trigger pin on ping sensor.
#define ECHO_PIN       3 // Arduino pin tied to echo pin on ping sensor.
#define MAX_DISTANCE 200 // Maximum distance (in cm) to ping.
#define PING_INTERVAL 33 // Milliseconds between sensor pings (29ms is about the min to avoid cross-sensor echo).




// WiFI/MQTT Configuration Setup

#define MQTT_SERVER "atgl.udayakumarn.in"

const char* ssid = "atgl-s24-wifi-hotspot";
const char* password = "uday1-mifi";


const char* controlTopic = "/ATGL/main-tank-level";
const char* confirmTopic = "/ATGL/confirm";
const char* stTankId     = "main-tank-level";
const char* stSSID      = "borepump/ssid";
const char* stMQTT      = "borepump/mqtt";

static int   ReconnectTrials = 0;

#define WIFI_RECONNECT_DELAY 15000
#define MQTT_RECONNECT_DELAY 30000
#define TANK_TEST_FREQUENCY  15000
#define TRIG_PIN 2
#define ECHO_PIN 3
 
// GPIO-2 > TRIGGER | GPIO-3 > ECHO

WiFiClient wifiClient;
PubSubClient client(wifiClient);
SoftwareSerial jsnSerial(TRIG_PIN, ECHO_PIN);

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

static int lastMeasure=0;
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
static long lastSonarCheck;

//
// LED flashing dataset
const int LED = 1;
const int ledFlash = 500;
static long currentMillis;
static int ledState = HIGH;
//

//
// Flash LED to show we are up and running
//


void toggleLED() {
   long now = millis();
   if (now < currentMillis + ledFlash)
       return;
//   Serial.println(".");
   currentMillis = now;
   if (ledState == HIGH)
       ledState = LOW;
   else
       ledState = HIGH;
   digitalWrite(LED, ledState);
}

void setup() {
  
  // Serial.begin (115200);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, ledState);
/*
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
*/
  jsnSerial.begin(9600);
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);

  //start wifi subsystem
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  // attempt to connect to the WIFI network and then connect to the MQTT server
  // Initial call to reconnect should have a boolean TRUE argument
  reconnect(true);
  //wait a bit before starting the main loop
  delay(2000);

  lastMillis = millis();
  lastSonarCheck = lastMillis;
}

#define SONAR_CHECK_INTERVAL 200
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

int getDistance(){
  unsigned int distance;
  byte startByte, h_data, l_data, sum = 0;
  byte buf[3];
  
  startByte = (byte)jsnSerial.read();
  if(startByte ==255){
    jsnSerial.readBytes(buf, 3);
    h_data = buf[0];
    l_data = buf[1];
    sum = buf[2];
    distance = (h_data<<8) + l_data;
    if((( h_data + l_data)) != sum){
       return(999);
    }
    else{
       return(distance/10);      
    } 
  }  
  else {
    delay(100);
    return (-1);
  }

}

int ping_cm(){
/*
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);  
*/
  int distance = -1;
  jsnSerial.write(0x01);
   delay(10);
  if(jsnSerial.available()){
    distance = getDistance();
  }
  return distance;
}

void loop() {
  long now = millis();
  bool statusTime = false;
  toggleLED();

  if ((lastSonarCheck + SONAR_CHECK_INTERVAL) <= now) {
    if (!firstCycleDone) {
      measures[my_index++] = ping_cm();
      // Serial.print("D : ");
      // Serial.print(measures[my_index - 1]);
      // Serial.println(" cm");
      lastSonarCheck = millis();
      if (my_index >= BUFFER_SIZE) {
        firstCycleDone = true;
        lastMeasure = averageDistance();
        statusTime = true;
        my_index = 0;
      }
    } else {
      measures[my_index++] = ping_cm();
      // Serial.print("D : ");
      // Serial.print(measures[my_index - 1]);
      // Serial.println(" cm");
      lastSonarCheck = millis();
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
        // Serial.println("Reconnecting...");
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
    //maintain MQTT connection
    client.loop();
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
    // Serial.println("");
    // Serial.println("WiFi connected");  
    // Serial.println("IP address: ");
    // Serial.println(WiFi.localIP());
  }

  // make sure we are connected to WIFI before attempting 
  // to connect to MQTT
  if(WiFi.status() == WL_CONNECTED){
    currMQTTchk = millis();
    if (currMQTTchk - lastMQTTchk > MQTT_RECONNECT_DELAY ) {
      if (!client.connected()) {
        // Serial.print("Client status is : ");
        // Serial.println(client.state());
        client.disconnect();
        // Serial.print("Attempting MQTT connection...");

        // Generate client name based on MAC address and last 8 bits of microsecond counter
        String clientName;
        clientName += "esp8266-";
        uint8_t mac[6];
        WiFi.macAddress(mac);
//        clientName += macToStr(mac);

        //if connected, subscribe to the topic(s) we want to be notified about
      
//        if (client.connect((char*) clientName.c_str()), "uday", "swim2sea") {
          if (client.connect("atgl-tank-sensor1", "uday", "swim2sea")) {
          // Serial.println("\tMQTT Connected");
          // Serial.print("Client status is : ");
          // Serial.println(client.state());
          client.subscribe(controlTopic);
          Confirm(stMQTT , MQTT_SERVER);
        }

        //otherwise print failed for debugging
        else{
          // Serial.print("\tFailed. rc="); 
          // Serial.println(client.state());
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

 /*
const unsigned int TRIG_PIN=13; //RX
const unsigned int ECHO_PIN=12; //TX
const unsigned int BAUD_RATE=115200;

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.begin(BAUD_RATE);
}

void loop() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);  

  const unsigned long duration= pulseIn(ECHO_PIN, HIGH);
  int distance= duration/29/2;
  if(duration==0){
    Serial.println("Warning: no pulse from sensor");
  } else {
    Serial.print("distance to nearest object:");
    Serial.println(distance);
    Serial.println(" cm");
  }
  delay(100);
}
*/
