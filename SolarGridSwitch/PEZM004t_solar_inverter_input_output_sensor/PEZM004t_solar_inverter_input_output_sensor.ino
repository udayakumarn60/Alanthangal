/*
 * This is an STM32 BluePill sketch
 */
#define USE_OLED
#undef DEBUG


#ifdef USE_OLED
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

#include <LEDFlasher.h>

#ifdef USE_OLED
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
#endif

#include <Arduino.h>
#include <HardwareSerial.h>
#include "PZEM004Tv30.h"
Flasher led(PC13, 500, 500);
#define TCAADDR 0x70


unsigned long volt_amp_scan_interval = 2000;
unsigned long power_scan_interval    = 2000;
unsigned long last_power_scan = 0;
unsigned long last_volt_amp_scan = 0;

float volts_grid = 0;
float amps_grid  = 0;
float watts_grid = 0;
float volts_solar = 0;
float amps_solar  = 0;
float watts_solar = 0;

char serialBuf[100];
int  serialIndex = 0;

#ifdef USE_OLED
#define LOGO16_GLCD_HEIGHT 16 
#define LOGO16_GLCD_WIDTH  16
/*
static const unsigned char PROGMEM logo16_glcd_bmp[] =
{ B00000000, B11000000,
  B00000001, B11000000,
  B00000001, B11000000,
  B00000011, B11100000,
  B11110011, B11100000,
  B11111110, B11111000,
  B01111110, B11111111,
  B00110011, B10011111,
  B00011111, B11111100,
  B00001101, B01110000,
  B00011011, B10100000,
  B00111111, B11100000,
  B00111111, B11110000,
  B01111100, B11110000,
  B01110000, B01110000,
  B00000000, B00110000 };
*/
#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

#endif // USE_OLED

void tcaselect(uint8_t i) {

#ifdef DEBUG
  Serial.print("TCA Select: ");
  Serial.print(i);
  Serial.println("");
#endif
  if (i > 7) return;
 
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
#ifdef DEBUG
  Serial.println("TCA Select Complete ");
#endif

}

char *convertToFormattedString(int input, char *buf, int fixed) {
  int index = 0;
  int decimalPosition;
//  Serial.print(input);
//  Serial.print(' ');
  if (!input) {
    strcpy(buf,"+00.00");
    return buf;
  }
  
  if (input < 0) {
    buf[index++] = '-';
    input = 0 - input;
  }
  else
    buf[index++] = '+';
//  Serial.println(input);
  if (input/100 < 100)
    decimalPosition = 3;
  else if (input/100 >= 100 && input/100 <1000)
    decimalPosition = 4;
  else
    decimalPosition = 99;
  if (input >= 100000)
    input /= 100;
  else if (input >= 10000)
    input /= 10;
  buf[index++] = (input /1000) + 48;
  buf[index++] = ((input % 1000)  /100 ) + 48;
  if (index == decimalPosition)
    buf[index++] = '.';
  buf[index++] = ((input % 100)   /10  ) + 48;
  if (index == decimalPosition)
    buf[index++] = '.';
  buf[index++] = (input % 10) + 48 ;
  if (decimalPosition == 99)
    buf[index++] = ' ';
  buf[index] = NULL;
//  Serial.println(buf);
  return buf;
}

unsigned long lastOutput = 0;
#define DISPLAY_DELAY 2500
#define GRID_DISPLAY  0
#define SOLAR_DISPLAY 1

void displayParameters(uint8_t displayID) {
  char publishMsg[60];
  char voltString[20];
  char ampsString[20];
  char wattString[20];
  char source;
  float power;
  int voltage_grid = volts_grid * 100;
  int current_grid = amps_grid * 100;
  int voltage_solar = volts_solar * 100;
  int current_solar = amps_solar * 100;
  
  char *volt_ptr, *amps_ptr;

  if (displayID == GRID_DISPLAY) {
    source = 'G';
    volt_ptr = convertToFormattedString(voltage_grid, voltString, 2);  
    amps_ptr = convertToFormattedString(current_grid, ampsString, 2);
    power = watts_grid;
  } else if (displayID == SOLAR_DISPLAY) {
    source = 'S';
    volt_ptr = convertToFormattedString(voltage_solar, voltString, 2);
    amps_ptr = convertToFormattedString(current_solar, ampsString, 2);
    power = watts_solar;
  }

#ifdef USE_OLED
  tcaselect(displayID);
  display.clearDisplay();
  display.setCursor(0,0);
  if(!displayID)
    display.print(String("Grid\nIn"));
  else if (displayID == SOLAR_DISPLAY)
    display.print(String("Solar\nOut"));  
  display.setCursor(64,0);
  display.print(String(volt_ptr+1)+" Vac");
  display.setCursor(64,16);
  display.print(String(amps_ptr+1)+" amps");
  display.display();
#endif

  sprintf(publishMsg,"%cV: %s A: %s P: %6.2f", source, volt_ptr+1, amps_ptr+1, power);
  Serial.println(publishMsg);
}

void setup() {
  Wire.begin();
  Serial.begin(115200);
  delay(50);
  Serial.println("Hello ESP01");

  // Set up the relay pins for Solar and Grid Contactors
  pinMode(PB8, OUTPUT);
  pinMode(PB9, OUTPUT);
  digitalWrite(PB8, LOW);
  digitalWrite(PB9, LOW);

#ifdef USE_OLED
  for (uint8_t displayId =0; displayId <= 1; displayId++) {
    tcaselect(displayId);
    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)
    display.dim(false);
/*
    display.ssd1306_command(SSD1306_SETPRECHARGE); //0xD9
    display.ssd1306_command(0);

    display.ssd1306_command(SSD1306_SETVCOMDETECT); //0xDB
    display.ssd1306_command(0);
*/

    // init done
  
    // Show image buffer on the display hardware.
    // Since the buffer is intialized with an Adafruit splashscreen
    // internally, this will display the splashscreen.
    display.display();
  }
  yield();
#endif
#ifdef DEBUG
  Serial.println("Finished OLED Init");
#endif
  delay(4000);

#ifdef USE_OLED
  for (uint8_t displayId =0; displayId <= 1; displayId++) {
    tcaselect(displayId);
    // Clear the i2c display buffer.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
  }
#endif

  Serial.println("Initializing");
  
/*  // Sync serial input to the beginning of newline
  while (1)
    if(Serial.available() && Serial.read() == '\n')
      break;
*/
}

/*
 * Solar contactor on and off (two separate) relays are triggered and held for one second before the relays are tripped
 * The MY2N relay on the solar grid switching circuit latches the solar contactor on.
 * The off relay just breaks the contactor circuit for one second.
 * 
 */
bool solarStartRelayTrig = false;
bool solarTripRelayTrig = false;
unsigned int relayHoldTime = 1000; // Hold relay for 1 second and then release
unsigned long onRelayTime = 0;
unsigned long offRelayTime = 0;

void tripSolarStartRelay() {
  digitalWrite(PB9, LOW);
  solarStartRelayTrig = false;
  Serial.println("1S");
}

void tripSolarTripRelay() {
  digitalWrite(PB8, LOW);
  solarTripRelayTrig = false;
  Serial.println("1G");
}

void processMessage(char *msgBuf) {
  Serial.println(msgBuf);
  switch (msgBuf[0]) {
    case 'S':
      if (msgBuf[1] == '1') {
        if (solarStartRelayTrig)
          break;
        digitalWrite(PB9, HIGH);
        solarStartRelayTrig = true;
        onRelayTime = millis();
      }
      break;
    case 'G':
      if (msgBuf[1] == '1') {
        if (solarTripRelayTrig)
          break;
        digitalWrite(PB8, HIGH);
        solarTripRelayTrig = true;
        offRelayTime = millis();
      }
      break;
  }
}



void loop() {
  PZEM004Tv30 pzemGrid(&Serial1);
  PZEM004Tv30 pzemSolar(&Serial2);
  unsigned long time_now = millis();
  int numBytes;
  char msgBuf[100];

  led.Update();
  //Serial.println("In Loop");

  if (solarStartRelayTrig) {
    if ((onRelayTime + relayHoldTime) < time_now) {
      tripSolarStartRelay();
      solarStartRelayTrig = false;
    }
  }

  if (solarTripRelayTrig) {
    if ((offRelayTime + relayHoldTime) < time_now) {
      tripSolarTripRelay();
      solarTripRelayTrig = false;
    }
  }
  
  if ((last_volt_amp_scan + volt_amp_scan_interval) <= time_now) {
    /*
     * Get Solar Output Voltage and Current
     */
    float voltage = pzemSolar.voltage();
    last_volt_amp_scan = time_now;
    if(voltage != NAN){
      volts_solar = voltage ;
    } else {
      volts_solar = 0;
    }

    float current = pzemSolar.current();

    if(current != NAN){
      amps_solar = current;
    } else {
      amps_solar = 0;
    }

    /*
     * Get Grid Input Voltage and Current
     */
    voltage = pzemGrid.voltage();
    if( voltage != NAN) {
      volts_grid = voltage;
    } else {
      volts_grid = 0;
    }
    current = pzemGrid.current();

    if( current != NAN) {
      amps_grid = current;
    } else {
      amps_grid = 0;
    }
  }
  
  if ((last_power_scan + power_scan_interval) <= time_now) {
    /*
     * Get Solar Output Power
     */
    float power = pzemSolar.power();
    last_power_scan = time_now;

    if(power != NAN){
        watts_solar = power;
    } else {
        watts_solar = 0;
    }

    /*
     * Get Grid Input Power
     */
    power = pzemGrid.power();

    if(power != NAN){
        watts_grid = power;
    } else {
        watts_grid = 0;
    }

    


// #ifdef DEBUG
//    float energy = pzem.energy();
//    Serial.print("Energy: ");
//    if(energy != NAN){
//      Serial.print(energy,3); 
//    } else {
//      Serial.print("NA");
//    }
//    Serial.println(" kWh");
// #endif
  }

//    float frequency = pzem.frequency();
//    if(current != NAN){
//        Serial.print("Frequency: "); Serial.print(frequency, 1); Serial.println("Hz");
//    } else {
//        Serial.println("Error reading frequency");
//    }

//    float pf = pzem.pf();
//    if(current != NAN){
//        Serial.print("PF: "); Serial.println(pf);
//    } else {
//        Serial.println("Error reading power factor");
//    }
  if (time_now <= (lastOutput + DISPLAY_DELAY))
    delay(50);
  else {
    lastOutput = time_now;
    displayParameters(0);
    displayParameters(1);
  }
  numBytes = Serial.available();
//  Serial.print(numBytes); Serial.println();
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
