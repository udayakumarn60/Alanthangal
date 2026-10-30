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
//     Topic for Commands -> /ATGL/borepump
//     Topic for Confirmation -> /ATGL/confirm
//
//     Configure the AP SSID/password, MQTT Server IP
//
// Requires PubSubClient found here: https://github.com/knolleary/pubsubclient
//
// Code revisited on 10 Feb 2021
// Code modified on 22 Feb 2024 to work for new hardware based on MOSFET switching circuit
// with snubber circuits added to relay and thyrister contacts
//   P-Channel MOSFET used for Pump Trip Circuit. So relay on/off logic is reverse
//   The trip relay is kept permanently on (except when tripping) as relay with NC contact
//   could not be sourced. 
//
// The starting capacitor in the motor starter compartment burnt out. On careful inspection it was found that the SSR
// (solid state relay) had shorted out permanently.
//
// On code analysis a race condition was found.
// 
// The timer that starts and trips the pump relies on one variable to decide whether to switch
// the start relay(s) or the stop relay. If a start commond is followed by an immediated stop command within 1 second
// the start relay timer would be running with onPin set to start relay which is On. Both the timer and onPin 
// would have been reset for the Off relay after firing the off relay. At the end of the timer, the start relay will
// continue to be on and the stop relay will be tripped. This is true vice versa also
//
// (1) This race condition is fixed now. The On or Off MQTT command is ignored if onPin is set to one of the relays
// (2) All Serial.print statements are conditional DEBUG code
//

#undef DEBUG
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
const int noRelay = 99;
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
const char* Trip        = "Trip"; // Force trip the pump
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
static unsigned long onHoldDelay = 750; // Allow relay hold for 750 millisecond
static bool onInitiated=false;
static bool trippedDuringStart=false;
static unsigned long onPin = noRelay;

//
// Following functions implement a timer that will auto trip the pump if it has been running for more than one hour
// This is required if the remote site islands from the internet after the pump is switched on.
//

#define PUMP_AUTO_TRIP_DELAY 3600000.          // -> 1 hour auto trip
// #define PUMP_AUTO_TRIP_DELAY 60000
static unsigned long auto_trip_timer = 0;
static bool auto_trip_timer_status = false ;

void autoTripTimerStart() {
  auto_trip_timer_status = true ;
  auto_trip_timer = millis();
}

void autoTripTimerCancel() {
  auto_trip_timer_status = false ;
  auto_trip_timer = 0;  
}

bool autoTripTimerStatus() {
  if (!auto_trip_timer_status)
      return false;
  if (millis() > (auto_trip_timer + PUMP_AUTO_TRIP_DELAY))
#ifdef DEBUG
  {
      unsigned long elapsed = (millis() - (auto_trip_timer))/1000;
      Serial.print(elapsed);
      Serial.println(" seconds");
      Serial.println("Pump has autotripped");
#endif
      return true;
#ifdef DEBUG
  }
#endif
  else
      return false;
}

void switchPumpStartRelay() {
  // switch the relay that drives the contactor on.
  // This will also power the starting coil
  // Once the contactor is on it will latch itself
  // Starting coil needs power till the pump starts running fully
  onMillis = millis();
  onInitiated = true;
  onPin = pumpStartPin;
  digitalWrite(pumpStartPin, HIGH);
#ifdef DEBUG
  Serial.println("Switching on Relay for contactor");
#endif
}

void switchPumpTripRelay() {
  // The pump trip relay just trips the contactor and the pump loses power
  onMillis = millis();
  onInitiated = true ;
  onPin = pumpTripPin;
  digitalWrite(pumpTripPin, HIGH);
#ifdef DEBUG
  Serial.println("Switching on Relay for trip pin contactor");
#endif
}

void tripRelayIfOn() {
  char msgBuf[100];
  // Both the on Relay and Off Relay should be tripped after holding for 1 second
  // If the on Relay does not trip after one second, the starting coil is likely to burn
  if (onInitiated) {
    long currentMillis = millis();
    if ((currentMillis - onMillis) > onHoldDelay) {
      sprintf(msgBuf,"Elapsed delay %d.", currentMillis - onMillis);
#ifdef DEBUG
      Serial.println(msgBuf);
#endif

      digitalWrite(onPin, LOW);
      onPin = noRelay;
#ifdef DEBUG
      Serial.println("Switching off relay");
#endif
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
#ifdef DEBUG
  //Print out some debugging info
  Serial.println("Callback update.");
  Serial.print("Topic: ");
  Serial.println(topicStr);
  Serial.println(payloadBuf);
#endif
  //turn the bore pump on if the payload is 'On' and publish to the MQTT server a confirmation message
  if(!strcmp(payloadBuf,On)){
    // If we are already processing a start or stop command and our 1 sec Timer is on, we ignore the new command
    if (!onInitiated && Borepump == OFF) {
        // We are switching to command mode. Setting Solenoid to ON
        // Solenoid to remain ON till command to switch it OFF is received
        Borepump = ON;
        switchPumpStartRelay();
        // We start a long term timer which will expire and help trip the pump if it is found running
        // for prolonged intervals. This can happen if the site islands from the internet due to last
        // mile connectivity problems
        autoTripTimerStart();
        State = COMMAND ;
        Confirm(stBorepump, On);      
    }
#ifdef DEBUG
    else {
      if(onInitiated)
        Serial.println("Attempt to start or stop when such action is in progress");
      if (Borepump != OFF)
        Serial.println("Attempt to start a pump that is already running");
    }
#endif
  }

  //turn the bore pump off if the payload is 'Off' and publish to the MQTT server a confirmation message
  else if (!strcmp(payloadBuf, Off)){
    // If we are already processing a start or stop command and our 1 sec Timer is on, we ignore the new command
    if (!onInitiated && Borepump == ON) {
        // We are in command mode. Setting Solenoid to OFF
        // Solenoid will remain off till next command to switch it ON
        Borepump = OFF;
        switchPumpTripRelay();
        autoTripTimerCancel(); // The long term timer can be reset now
        State = COMMAND ;
        Confirm(stBorepump, Off);
    }
    else {
      if(onInitiated) {
#ifdef DEBUG
        Serial.println("Attempt to start or stop when such action is in progress");
#endif
        if (Borepump == ON) {
          trippedDuringStart=true;
        }
      }
#ifdef DEBUG
      if (Borepump != ON)
        Serial.println("Attempt to stop an idle pump.");
#endif
    }
  }
  else if (!strcmp(payloadBuf, Trip)){
    // We are asked to force trip the pump
    Borepump = OFF;
    switchPumpTripRelay();
    autoTripTimerCancel();
    State = COMMAND;
    Confirm(stBorepump, Trip);
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
#ifdef DEBUG
  //start the serial line for debugging
   Serial.begin(115200);
#endif
   delay(100);
   
  //initialize the bore pump start relay as an output and set to LOW (off)
  pinMode(pumpStartPin, OUTPUT);
  digitalWrite(pumpStartPin, LOW);

  //initialize the bore pump stop relay as an output and set to LOW (off)
  pinMode(pumpTripPin, OUTPUT);
  digitalWrite(pumpTripPin, LOW);

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
  //
  // Pump protection code
  //
  if (autoTripTimerStatus() && Borepump == ON) {
    // Our long term timer has expired
    autoTripTimerCancel();
    switchPumpTripRelay();
    Borepump = OFF;
    Confirm(stBorepump, Trip);
    Confirm(stBorepump, Off);
  }
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
    //maintain MQTT connection
    client.loop();
  }
  
  //MUST delay to allow ESP8266 WIFI functions to run
  delay(10); 
  // Check and trip relays providing required momentary delay, if any has been triggered
  tripRelayIfOn();
  if(trippedDuringStart) {
    // Boundary condition of receiving stop command when start is in progress
#ifdef DEBUG
    Serial.println("Stopped while start is in progress");
#endif
    trippedDuringStart = false;
    autoTripTimerCancel();
    switchPumpTripRelay();
    Borepump = OFF;
    State = COMMAND;
    Confirm(stBorepump, Trip);
    Confirm(stBorepump, Off);
  }
  
//  if (State == LOOPING) {
//    Counter = Counter + 10;
//    if (Solenoid == ON) {
//      if (Counter >= OnCycle * 1000) {
// #ifdef DEBUG
//        Serial.println("Solenoid Off");
// #endif
//        Counter = 0;
//        Solenoid = OFF ;
//        switchSolenoidOff();
//        publishStatus();
//      }
//    }
//    else if (Solenoid == OFF) {
//      if (Counter >= OffCycle * 1000) {
// #ifdef DEBUG
//        Serial.println("Solenoid On");
// #endif
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
#ifdef DEBUG
    //debug printing
    Serial.print("Connecting to ");
    Serial.println(ssid);
#endif
    if (!initial) {
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
    //loop while we wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
#ifdef DEBUG
      Serial.print(".");
#endif
      reconnect_attempt ++;
      if (reconnect_attempt >= MAX_WIFI_RETRY_COUNT)
      {
        return ;
      }
    }
    Confirm(stSSID , ssid);
#ifdef DEBUG
    //print out some more debug once connected
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
#ifdef DEBUG
        Serial.print("Attempting MQTT connection...");
#endif

        // Generate client name based on MAC address and last 8 bits of microsecond counter
        String clientName;
        clientName += "esp8266-";
        uint8_t mac[6];
        WiFi.macAddress(mac);
        clientName += macToStr(mac);
#ifdef DEBUG
        Serial.println(clientName.c_str());
#endif

        //if connected, subscribe to the topic(s) we want to be notified about
      
//        if (client.connect((char*) clientName.c_str()), "uday", "swim2sea") {
          if (client.connect("atgl-borepump-test-cntrlr", "uday", "swim2sea")) {
#ifdef DEBUG
          Serial.println("\tMQTT Connected");
          Serial.print("Client status is : ");
          Serial.println(client.state());
#endif
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
String macToStr(const uint8_t* mac){  
  String result = "";
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);

    if (i < 5){
      result += ':';
    }
  }

#ifdef DEBUG
  Serial.println(result);
#endif
  return result;
}
