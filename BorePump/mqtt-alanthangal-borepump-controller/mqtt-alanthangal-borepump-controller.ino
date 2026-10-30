// ESP8266 MQTT based bore pump controller
//
// Udayakumar Nalinasekaren - Creative Commons 2016
// github.com/udayakumarn60
//
// Controls a Solenoid valve with the following MQTT Commands
//     On        -> Set Pump to On
//     Off       -> Set Pump to Off
//     C1<secs>  -> Set On Cycle Time to <secs>
//     Status    -> Publish current status to MQTT Broker
//
//     MQTT Broker Interface
//     Topic for Commands -> /ATGL/borepump-1
//     Topic for Confirmation -> /ATGL/confirm
//
//     Configure the AP SSID/password, MQTT Server IP
//
// Requires PubSubClient found here: https://github.com/knolleary/pubsubclient
//


#include <PubSubClient.h>
#include <ESP8266WiFi.h>


// WiFI/MQTT Configuration Setup

#define MQTT_SERVER "atgl.udayakumarn.in"

const char* ssid = "atgl-s24-wifi-hotspot";
const char* password = "uday1-mifi";

// Relay Pin 1 when switched on energises the solid state relay and the contactor
// Relay Pin 1 when switched on disconnects the contactor

const char* controlTopic = "/ATGL/borepump";
const char* confirmTopic  = "/ATGL/confirm";

const int relayPin1 = 0;
const int relayPin2 = 2;
const int pumpStartPin = relayPin2;
const int pumpTripPin  = relayPin1;

#define DEFAULT_ON_CYCLE 600
#define WIFI_RECONNECT_DELAY 15000
#define MQTT_RECONNECT_DELAY 30000

static int   OnCycle  = DEFAULT_ON_CYCLE ;
static int   OffCycle = 0 ;
static int   Counter  = 0;
static int   State    = 0;
static int   Borepump = 0;
static int   ReconnectTrials = 0;
static int   ValveOpen = 0;

static char msgBuf[50];
static char tmpBuf[50];

const int COMMAND = 0;

const int ON  = 1;
const int OFF = 2 ;

const char* On      = "On";
const char* Off     = "Off";
const char* onCycleStr  = "On Cycle : " ;
const char* stBorepump  = "borepump";
const char* stCycleOn   = "borepump/on-cycle";
const char* stSSID      = "borepump/ssid";
const char* stMQTT      = "borepump/mqtt";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

void reconnect(bool);

void Confirm(const char *subTopic, const char *msg) {
  if (client.connected()){
    if (subTopic) {
      sprintf(tmpBuf, "%s/%s",confirmTopic, subTopic);
      client.publish(tmpBuf, msg, false);
    }
    else
      client.publish(confirmTopic, msg, false);
  }
}

char* getCurrentCycleTime(int what = ON) {
  if (what == ON)
    sprintf(msgBuf, "%d", OnCycle);
  else
    sprintf(msgBuf, "%d", OffCycle);
  return msgBuf;
}

static unsigned long onMillis;
static unsigned long onHoldDelay = 1000; // Allow relay hold for 1 second
static bool onInitiated=false;
static unsigned long onPin;

void switchPumpStartRelay() {
  // switch the relay that drives the contactor on.
  // This will also power the starting coil
  // Once the contactor is on it will latch itself
  // Starting coil needs power till the pump starts running fully
  onMillis = millis();
  onInitiated = true;
  onPin = pumpStartPin;
  digitalWrite(pumpStartPin, LOW);
  Serial.println("Switching on Relay for contactor");
}

void switchPumpTripRelay() {
  // The pump trip relay just trips the contactor and the pump loses power
  onMillis = millis();
  onInitiated = true ;
  onPin = pumpTripPin;
  digitalWrite(pumpTripPin, LOW);
  Serial.println("Switching on Relay for trip pin contactor");
}

void tripRelayIfOn() {
  char msgBuf[100];
  // Both the on Relay and Off Relay should be tripped after holding for 1 second
  // If the on Relay does not trip after one second, the starting coil is likely to burn
  if (onInitiated) {
    long currentMillis = millis();
    if ((currentMillis - onMillis) > onHoldDelay) {
      sprintf(msgBuf,"Elapsed delay %d.", currentMillis - onMillis);
      Serial.println(msgBuf);
      onInitiated = false;
      digitalWrite(onPin, HIGH);
      Serial.println("Switching off relay");
      onInitiated = false;
    }
  }
}

void publishStatus() {
    // Let us publish our current configuration and status
    const char* borepumpState;
    
    if (Borepump == ON)
      borepumpState = On;
    else
      borepumpState = Off;
    Confirm(stBorepump, borepumpState);
    delay(10);
    Confirm(stCycleOn, getCurrentCycleTime(ON));
    delay(10);
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
  Serial.println("Callback update.");
  Serial.print("Topic: ");
  Serial.println(topicStr);
  Serial.println(payloadBuf);
  
  //turn the bore pump on if the payload is 'On' and publish to the MQTT server a confirmation message
  if(!strcmp(payloadBuf,On)){
    // We are switching to command mode. Setting Solenoid to ON
    // Solenoid to remain ON till command to switch it OFF is received
    Borepump = ON;
    switchPumpStartRelay();
    State = COMMAND ;
    Confirm(stBorepump, On);
  }

  //turn the bore pump off if the payload is 'Off' and publish to the MQTT server a confirmation message
  else if (!strcmp(payloadBuf, Off)){
    // We are in command mode. Setting Solenoid to OFF
    // Solenoid will remain off till next command to switch it ON
    Borepump = OFF;
    switchPumpTripRelay();
    State = COMMAND ;
    Confirm(stBorepump, Off);
  } 
  else if (!strcmp(payloadBuf, "Status")){
    publishStatus();
  }

  else if (payloadBuf[0] == 'C') {
    int n;
    if (payloadBuf[1] == '1') {
      OnCycle = (n=atoi(payloadBuf+2))?n:DEFAULT_ON_CYCLE ;
    }
    
    publishStatus();
  }
}

void setup() {
  //start the serial line for debugging
   Serial.begin(115200);
   delay(100);
   
  //initialize the bore pump start relay as an output and set to LOW (off)
  pinMode(pumpStartPin, OUTPUT);
  digitalWrite(pumpStartPin, HIGH);

  //initialize the bore pump stop relay as an output and set to LOW (off)
  pinMode(pumpTripPin, OUTPUT);
  digitalWrite(pumpTripPin, HIGH);

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

  //
  // get our system into a looping mode by default
  //
  OnCycle = DEFAULT_ON_CYCLE ;
  Counter = 0;
  State = COMMAND;
  Borepump = OFF ;
  onInitiated = true;
  // When we start we will trip the pump if it is already on.
  // Should not cause a problem because the controller is on the same power as the
  // starting panel
  switchPumpTripRelay();
  publishStatus();
}


void loop(){
  int currentState, currentBorepumpState;
  currentState = State ;
  currentBorepumpState = Borepump ;
  //check & reconnect if connection is lost
  if (!client.connected()) {
    if (Borepump == ON) {
      // If the pump is on, we will first switch it off
      // as a safety measure
      switchPumpTripRelay();
      Borepump = OFF;
      // Since we are not connected, calling publishStatus will
      // meaningless
    }
    if (WiFi.status() != WL_CONNECTED) {
      // WiFi disconnected. We will wait for 2.5 minutes and
      // try reconnecting
      ReconnectTrials++;
      if (ReconnectTrials > WIFI_RECONNECT_DELAY) {
        Serial.println("Reconnecting...");
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
  }
  
  //MUST delay to allow ESP8266 WIFI functions to run
  delay(10); 
  // Check and trip relays providing momentary delay if any has been switched on
  tripRelayIfOn();
  
//  if (State == LOOPING) {
//    Counter = Counter + 10;
//    if (Solenoid == ON) {
//      if (Counter >= OnCycle * 1000) {
//        Serial.println("Solenoid Off");
//        Counter = 0;
//        Solenoid = OFF ;
//        switchSolenoidOff();
//        publishStatus();
//      }
//    }
//    else if (Solenoid == OFF) {
//      if (Counter >= Of//fCycle * 1000) {
//        Serial.println("Solenoid On");
//        Counter = 0;
//        Solenoid = ON ;
        // ValveOpen += OnCycle ;
//        switchSolenoidOn();
//      }
//    }
//  }
}

#define MAX_WIFI_RETRY_COUNT 10

static unsigned long lastMQTTchk = 0;

void reconnect(bool initial) {
  int reconnect_attempt = 0;
  int currMQTTchk = 0;
  //attempt to connect to the wifi if connection is lost
  if(WiFi.status() != WL_CONNECTED){
    //debug printing
    Serial.print("Connecting to ");
    Serial.println(ssid);
    if (!initial) {
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
    //loop while we wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      reconnect_attempt ++;
      if (reconnect_attempt >= MAX_WIFI_RETRY_COUNT)
      {
        return ;
      }
    }
    Confirm(stSSID , ssid);
    //print out some more debug once connected
    Serial.println("");
    Serial.println("WiFi connected");  
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  // make sure we are connected to WIFI before attempting 
  // to connect to MQTT
  if(WiFi.status() == WL_CONNECTED){
    currMQTTchk = millis();
    if (currMQTTchk - lastMQTTchk > MQTT_RECONNECT_DELAY ) {
      if (!client.connected()) {
        Serial.print("Client status is : ");
        Serial.println(client.state());
        client.disconnect();
        Serial.print("Attempting MQTT connection...");

        // Generate client name based on MAC address and last 8 bits of microsecond counter
        String clientName;
        clientName += "esp8266-";
        uint8_t mac[6];
        WiFi.macAddress(mac);
        clientName += macToStr(mac);

        //if connected, subscribe to the topic(s) we want to be notified about
      
//        if (client.connect((char*) clientName.c_str()), "uday", "swim2sea") {
          if (client.connect("atgl-borepump-cntrlr", "uday", "swim2sea")) {
          Serial.println("\tMQTT Connected");
          Serial.print("Client status is : ");
          Serial.println(client.state());
          client.subscribe(controlTopic);
          Confirm(stMQTT , MQTT_SERVER);
        }

        //otherwise print failed for debugging
        else{
          Serial.print("\tFailed. rc="); 
          Serial.println(client.state());
          // abort();
        }
      }
      lastMQTTchk = currMQTTchk ;
    }
  }  
}

//generate unique name from MAC addr
String macToStr(const uint8_t* mac){  
  String result = "";
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);

    if (i < 5){
      result += ':';
    }
  }

  Serial.println(result);
  return result;
}
