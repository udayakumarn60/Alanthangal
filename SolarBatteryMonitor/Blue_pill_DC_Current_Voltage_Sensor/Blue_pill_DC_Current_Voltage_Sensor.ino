 /*********************************************************************
Code for using STM32f103c8t6 and ESP-01 to measure battery voltage/
current. This program displays the parameters to a monochrome OLED
display and/or a 16x2 LCD display, if they are connected. Define
the USE_OLED and USE_LCD symbols below to respectively use the OLED
or LCD code.

The code also sends the measured parameters to an ESP-01 module which
will connect to internet and publish MQTT messages accordingly.

STM32 ADCs are used to measure battery amps and volts using ACS709
hall effect sensor and a DC 25V voltage sensor respectively. the 
sensors map the battery current and voltage range to a 0 to 3.3 volts
range to be fed to STM32
*********************************************************************/
#undef USE_OLED
#define USE_LCD
#undef ACS709
#define WCS1600

#ifdef USE_OLED
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

#ifdef USE_LCD
#include <LiquidCrystal.h>
#endif
#include <LEDFlasher.h>

#ifdef USE_OLED
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
#endif

Flasher led(PC13, 500, 500);
#define CURRENT_PIN PA2
#define VOLTAGE_PIN PA1
#define SAMPLE_INTERVAL 500
float   currentAmps = 0;
float   currentVolt = 0 ;
static long lastMillis;

#ifdef USE_LCD
// lcd ports
const int rs = PB11, en = PB10;
const int d4 = PB0,  d5 = PB1, d6 = PC13, d7 = PC14 ;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
#endif

#ifdef USE_OLED
#define LOGO16_GLCD_HEIGHT 16 
#define LOGO16_GLCD_WIDTH  16
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

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

#endif // USE_OLED

#define BUFFER_SIZE 10
int currentReadings[BUFFER_SIZE];
int voltageReadings[BUFFER_SIZE];
bool firstCycleDone = false;
int my_index = 0;

int movingAverage(int input[]) {
  int i;
  int sum = 0;
  for (i = 0 ; i < BUFFER_SIZE; i++)
    sum += input[i];
  return (sum/BUFFER_SIZE);
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

void displayParameters() {
  char publishMsg[50];
  char ampsString[20];
  char voltString[20];
  char wattString[20];
  float mid = 1.65;
  int reject;
  if (!firstCycleDone){
    // We take the second value of two successive reads
    // on the ADC pin for Amps and Volts
    reject = analogRead(CURRENT_PIN);
    currentReadings[my_index] = analogRead(CURRENT_PIN);
    delayMicroseconds(100); // Allow time for the MUX to switch pins
    reject = analogRead(VOLTAGE_PIN);
    voltageReadings[my_index] = analogRead(VOLTAGE_PIN);
    my_index++;
    
    if (my_index >= BUFFER_SIZE) {
      firstCycleDone = true;
      my_index = 0;
      return;
    }
  }
  else {
    currentReadings[my_index] = analogRead(CURRENT_PIN);
    // Serial.print(currentReadings[my_index]);
    voltageReadings[my_index] = analogRead(VOLTAGE_PIN);
    my_index++;
    
    // We display the parameters every time buffer becomes full
    if (my_index >= BUFFER_SIZE) {
      int amps;
      int volt;
      int watt;
      
      // Reset the buffer pointer
      my_index = 0;

      amps = movingAverage(currentReadings);
      volt = movingAverage(voltageReadings);

#ifdef USE_OLED 
      display.clearDisplay();
      display.setCursor(0,0);
      display.print("Current : ");
#endif

#ifdef ACS709
      amps = map(amps, 0, 4095, -7500, 7500);
#endif
#ifdef WCS1600
      amps = map(amps, 241, 3855, -10000, 10000);
#endif
      convertToFormattedString(amps, ampsString, 2);

#ifdef USE_OLED
      display.print(ampsString);
#endif

#ifdef USE_LCD
      lcd.begin(16,2);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(ampsString);
      lcd.setCursor(6,0);
      lcd.print("A");
#endif

#ifdef USE_OLED
      display.println(" A");
      display.setCursor(0, 16);
      display.print("Voltage : ");
#endif

      volt = map(volt, 0 , 4095, 0 , 6133);
      convertToFormattedString(volt, voltString, 2);

#ifdef USE_LCD
      lcd.setCursor(8,0);
      lcd.print(voltString);
      lcd.setCursor(14,0);
      lcd.print("V");
#endif

#ifdef USE_OLED
      display.print(voltString);
      display.println(" V");
      display.setCursor(0, 32);
      display.print("Watts   : ");
#endif
      watt = int((float)volt/100 * (float)amps/100)*100;
      convertToFormattedString(watt, wattString, 2);

#ifdef USE_LCD
      lcd.setCursor(0,1);
      lcd.print(wattString);
      lcd.setCursor(6,1);
      lcd.print("W");
      lcd.setCursor(8,1);
      if (wattString[0] == '+')
        lcd.print("Charging");
      else
        lcd.print("Draining");
#endif

#ifdef USE_OLED
      display.print(wattString);
      display.println(" W");
      display.display();
#endif
      sprintf(publishMsg,"C%sV%sW%s",ampsString, voltString,wattString);
      Serial.println(publishMsg); 
    }
  }
}

void setup()   {  
                
  Serial.begin(9600);

  pinMode(CURRENT_PIN, INPUT_ANALOG);
  pinMode(VOLTAGE_PIN, INPUT_ANALOG);

#ifdef USE_OLED
  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)
  // init done
  
  // Show image buffer on the display hardware.
  // Since the buffer is intialized with an Adafruit splashscreen
  // internally, this will display the splashscreen.
  display.display();
#endif
//  Serial.println("LED Init");
#ifdef USE_LCD
  lcd.begin(16,2);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Bluepill LCD");
  lcd.setCursor(0,1);
  lcd.print("VA Sensor");
#endif

  delay(2000);
//  Serial.println("After Delay");
#ifdef USE_LCD
  lcd.clear();
#endif

#ifdef USE_OLED
  // Clear the i2c display buffer.
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
#endif
//  Serial.println("OLED Display Clear");
  // Read and display current from current sensor;
  // Also sends a MQTT message with current and voltage details
  
  displayParameters();
  lastMillis = millis();
//  Serial.println("End of setup");  
}


void loop() {
  long currentMillis = millis();
  led.Update();
  if (currentMillis > (lastMillis + SAMPLE_INTERVAL)) {
    displayParameters();
    lastMillis = currentMillis;
  }
}
