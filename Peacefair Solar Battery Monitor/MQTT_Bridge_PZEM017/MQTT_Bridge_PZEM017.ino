#undef DEBUG

#include <LEDFlasher.h>
#include <MyWiFiSTA.h>
#include <MyMQTTWrapper.h>
#include <ESP12Module.h>
#include <string.h>

#define MQTT_SERVER   "atgl.udayakumarn.in"
#define CONTROLLER_ID "atgl-pzem017-mqtt-bridge"
#define MY_SSID       "atgl-s24-wifi-hotspot"

#define PZEM_Topic    "sb-pzem017"
#define VOLT_Topic    "volts" // PZEM017 - sensed voltage
#define CURRENT_Topic "amps"  // PZEM017 - sensed current
#define ENERGY_Topic  "energy"    // PZEM017 - sensed energy
#define LowVoltage_Alarm_Topic  "battery-low" // PZEM017 - battery low alarm
#define HighVoltage_Alarm_Topic "battery-high" // PZEM017 - battery low alarm
#define ResetEnergy_Topic       "reset-energy"
#define SetHVAlarm_Topic        "set-hv-alarm"
#define SetLVAlarm_Topic        "set-lv-alarm"
#define ResetAlarm_Topic        "reset-alarm"

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

float batteryVoltage = 0.0;
float batteryCurrent = 0.0;
float batteryEnergy  = 0.0;
bool  lowBattery     = false;

// Define the maximum limits for the static array
#define MAX_STRINGS 10
#define MAX_LENGTH 15

/**
 * @brief Parses an input string and stores the substrings (tokens) 
 * separated by blanks into a static, pre-allocated 2D array.
 *
 * NOTE: The input string 'input' is MODIFIED by strtok.
 * NOTE: Output strings are truncated if they exceed MAX_LENGTH - 1.
 *
 * @param input The string to parse. This will be modified.
 * @param output_array The pre-allocated 2D array to store the tokens.
 * @param max_strings_cap The maximum number of tokens (rows) the array can hold.
 * @param max_length_cap The maximum length of each token (columns).
 * @return The number of substrings successfully stored in the output array.
 */
int split_string_static(char* input, 
                        char output_array[MAX_STRINGS][MAX_LENGTH],
                        int max_strings_cap, 
                        int max_length_cap) {
    
    if (input == NULL) {
        return 0;
    }

    const char* delimiters = " \t";
    int count = 0;

    // 1. Tokenize the string
    char* token = strtok(input, delimiters);

    while (token != NULL && count < max_strings_cap) {
        // 2. Copy the token into the static array slot
        // Use strncpy to ensure no buffer overflow (limited by max_length_cap)
        
        // strncpy copies at most (max_length_cap - 1) characters
        // to save space for the null terminator.
        strncpy(output_array[count], token, max_length_cap - 1);
        
        // strncpy does NOT guarantee a null terminator if the source string length
        // is >= the copy count, so we must manually ensure it.
        output_array[count][max_length_cap - 1] = '\0';

        count++;

        // Get the next token
        token = strtok(NULL, delimiters);
    }

    return count;
}

char tokens[MAX_STRINGS][MAX_LENGTH] = {0};

void processCallback(char* topic, byte* payload, unsigned int t_length) {
  int i = 0;
  char payloadBuf[50];

  for (i = 0; i <t_length; i++)
    payloadBuf[i] = payload[i];
  payloadBuf[t_length] = NULL;
//  Serial.print(topic);Serial.print(" "); Serial.println(payloadBuf);
  int num_tokens = split_string_static(
        payloadBuf, 
        tokens, 
        MAX_STRINGS, 
        MAX_LENGTH
  );

//  for (int i = 0; i < num_tokens; i++) {
//        Serial.print("Token "); Serial.print(i+1); Serial.print(' ');
//        Serial.println(tokens[i]);
//  }

  // Proceess GENSET Command from MQTT
  if (String(topic) == String(SITE_ADDRESS) + '/' + String(PZEM_Topic)) {
    if(!strcmp(tokens[0], "Status")){
      // Hardcoded current range
      // TBD : Query current range from sensor
      Serial.println("ST");
    } else if (!strcmp(tokens[0], "Set")) {
        if (!strcmp(tokens[1], "CurrentRange" ) && num_tokens == 3) {
          Serial.print("SC "); Serial.println(tokens[2]);
        } else if (!strcmp(tokens[1],"HiVoltAlarm") && num_tokens == 3){
          Serial.print("SH "); Serial.println(tokens[2]);
        } else if (!strcmp(tokens[1],"LoVoltAlarm") && num_tokens == 3) {
          Serial.print("SL "); Serial.println(tokens[2]);
        } else if (!strcmp(tokens[1], "ResetEnergy")){
          Serial.println("SR");
        } else if (!strcmp(tokens[1], "ResetAlarm")) {
          Serial.println("SA");
        }
    } 
  }
}

char serialBuf[100];
int  serialIndex = 0;

// Subscribe to our relevant MQTT topics
void subscribeMyTopics(){
  myMQTT.subscribe((String(SITE_ADDRESS) + '/' + String(PZEM_Topic)).c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  // Serial.println("In Setup");
  // Setup the callback dispatcher  

  // Connect to WiFi
  Serial.println();
  Serial.println("Hello PZEM017");
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
  Serial.println("Ready");
}

void processMessage(char *msgBuf) {
  char printBuf[100];
  char *voltPtr = NULL;
  char *ampsPtr = NULL;
  char *wHPtr = NULL;
  bool batteryDetails = false ;
  bool retvalReceived = false ;
  int i;
  // Process message received from Arduino Nano
  // Format 1:  Vxxx.xxAxxx.xxExxx.xx\n
  // Format 2:  Larm BLow Active
  // Format 3:  Larm BHi Active
  // Format 4:  Set CurrentRange|ResetEnergy|HiVoltAlarm|LoVoltAlarm OK
  // Format 5:  Error xx
  int len = strlen(msgBuf);
  for (i= 0 ; i < len ; i++) {
    switch (msgBuf[i]) {
      case 'V':
        voltPtr = msgBuf + (i + 1) ;
        batteryDetails = true ;
        break;
      case 'A':
        ampsPtr = msgBuf + (i + 1) ;
        msgBuf[i] = NULL ;
        break;
      case 'E':
        wHPtr = msgBuf + (i + 1);
        msgBuf[i] = NULL ;
        break;
      case 'L':
      case 'S':
        batteryDetails = false;
        retvalReceived = true;
        break;
      default:
        break;
    }
    if (retvalReceived)
      break;
  }
  if (batteryDetails) {
    mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(VOLT_Topic)).c_str(), voltPtr);
    mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(CURRENT_Topic)).c_str(), ampsPtr);
    mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(ENERGY_Topic)).c_str(), wHPtr);
    return ;
  }
  int numTokens;
  if (retvalReceived){
    numTokens = split_string_static(
      msgBuf, 
      tokens, 
      MAX_STRINGS, 
      MAX_LENGTH);
  }

  if (!strcmp(tokens[0], "Larm")) {
    if (!strcmp(tokens[1], "BLow")) {
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(LowVoltage_Alarm_Topic)).c_str(), "On");
    }
    else if (!strcmp(tokens[1], "BHi")){
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(HighVoltage_Alarm_Topic)).c_str(), "On");
    }
  } else if (!strcmp(tokens[0], "Set")) {
    if (!strcmp(tokens[1], "ResetEnergy")){
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(ResetEnergy_Topic)).c_str(), "Ok");
    }
    else if(!strcmp(tokens[1], "ResetAlarm")){
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(ResetAlarm_Topic)).c_str(), "Ok");
    }
    else if(!strcmp(tokens[1], "HiVoltAlarm")){
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(SetHVAlarm_Topic)).c_str(), "Ok");
    }
    else if (!strcmp(tokens[1], "LoVoltAlarm")){
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + '/' + String(SetLVAlarm_Topic)).c_str(), "Ok");
    }      
  } else if (!strcmp(tokens[0], "Status")) {
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + "/confirm/current-range").c_str(), tokens[1]);
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + "/confirm/low-volt-alarm").c_str(), tokens[2]);
      mqttClient.publish((String(SITE_ADDRESS) + '/' + String(PZEM_Topic) + "/confirm/high-volt-alarm").c_str(), tokens[3]);
  }
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