/*
 * Thermistor Logging with display - Add display functions for a 2-line LCD display,
 * but structure it to potentially upgrade to a small screen with trend log display.
 *
 * By Chris "Sembazuru" Elliott, SembazuruCDE (at) GMail.com
 * 2015-01-07
 */

// 16x2 LCD Display
#include <LiquidCrystal.h>
const byte LCD_RS = A1; // pin4
const byte LCD_Enable = A0; // pin6
const byte LCD_D4 = 2; // pin11
const byte LCD_D5 = 3; // pin12
const byte LCD_D6 = 6; // pin13
const byte LCD_D7 = 7; // pin14
// LCD_VSS (pin1) to GND
// LCD_VDD (pin2) to +5
// 10k Pot +5 to GND, wiper to LCD_V0 (pin3) for contrast
// LCD_R/W (pin5) to ground
LiquidCrystal lcd(LCD_RS, LCD_Enable, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Chronodot
#include <Wire.h>
#include <Chronodot.h>
Chronodot chron = Chronodot();
const byte sqwPin = 5;

// Thermocouple
#include <SPI.h>
#include "Adafruit_MAX31855.h"
const byte tC_CS = 10;  // Chip select for reading the thermoucouple with the MAX31855
Adafruit_MAX31855 thermocouple(tC_CS);

// SD Card
#include <SD.h>
const byte SD_CS = 4;  // Chip select for accessing the SD card.
File dataFile;  // make the file pointer a global, but don't try to open it yet.
char filename[] = "MMDDhhmm.csv";  // filename template.

// RunningMdian
#include <RunningMedian.h>
const byte medianAverageSize = 5;
RunningMedian temperatures = RunningMedian(15);

// Other globals
boolean indicatorLED = false;  // Tradking variable for the state of ledPin during normal operation.
unsigned long lastSample = 0;  // Timing variable set when last reading is done to know when to do the next reading.
const int sampleInterval = 100;  // 100 milliseconds between reads.
byte lastSQW; // Holding variable for square wave state
const byte ledPin = 8;
const byte ledGnd = 9;

void setup()
{
/*
  Serial.begin(115200);  // Change this to whatever your like running your Serial Monitor at.
  while (!Serial);  // Wait for serial port to connect. Needed for Leonardo only.
  delay(1000);  // Simply to allow time for the ERW versions of the IDE time to automagically open the Serial Monitor. 1 second chosen arbitrarily.
*/
  pinMode(sqwPin, INPUT_PULLUP);
  pinMode(tC_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(ledGnd, OUTPUT);
  digitalWrite(ledGnd, LOW);

  // set up the LCD's number of columns and rows: 
  lcd.begin(16, 2);

  chron.setSQW(1);  // Set square wave pin output of 1Hz
  lastSQW = digitalRead(sqwPin);
  chron.readTimeDate();
  sprintf(filename, "%02u%02u%02u%02u.csv", chron.timeDate.month, chron.timeDate.day, chron.timeDate.hours, chron.timeDate.minutes);

  if (!SD.begin(SD_CS))  // mount the SD card
  {
    SDError();  // don't do anything more
  }
  dataFile = SD.open(filename, FILE_WRITE);
  if (!dataFile)
  {
    SDError();  // don't do anything more
  }
  dataFile.println(F("Date_Time,Temperature,Samples"));
  dataFile.flush();
  for(byte i = 0; i < 10; i++) // ensure the first reported reading has at least 1 second's worth of readings.
  {
    readThermocouple();
    delay(100);
  }
  setupTimeSeries();
}

void loop()
{
  // Every 100ms add a temperature reading to the running median buffer.
  if (millis() - lastSample >= sampleInterval)
  {
    readThermocouple();
    lastSample = millis(); // enforce 100ms between samples, not necessarily 1 sample every 100 ms. Subtle difference.
  }
  
  // When the Square wave output of the chronodot goes from high to low, write a line to the file and clear the buffer.
  lastSQW = dataRecord(digitalRead(sqwPin));
}

/* readThermocouple(void)
 *
 */
void readThermocouple()
{
  indicatorLED = !indicatorLED;
  digitalWrite(ledPin, indicatorLED);
  double degF = thermocouple.readFarenheit();
  if (!isnan(degF)) // occasionally there is a bad reading. This filters them out.
  {
    temperatures.add(degF);
    displayReadIndicator();
  }
}


/*
 * dataRecord(byte)
 *
 */
byte dataRecord(byte sqwState)
{
  char dateBuffer[] = "YYYY-MM-DD hh:mm:ss";
  
  if (sqwState < lastSQW) // Only time this is true is if the SQWPin changed state from high to low since the last loop. This is the falling edge to trigger on.
  {
    chron.readTimeDate();
    displayTime();
    sprintf(dateBuffer, "%04u-%02u-%02u %02u:%02u:%02u", chron.timeDate.year, chron.timeDate.month, chron.timeDate.day, chron.timeDate.hours, chron.timeDate.minutes, chron.timeDate.seconds);
    dataFile.print(dateBuffer);
    dataFile.print(',');
    if (temperatures.getCount() > 0){
      float temperatureReading = temperatures.getAverage(medianAverageSize);
      dataFile.print(temperatureReading, 2);
      displayTemperature(temperatureReading);
      updateTimeSeries(temperatureReading);
    }
    dataFile.print(',');
    dataFile.println(temperatures.getCount());
    dataFile.flush();
    temperatures.clear();
  }
  return sqwState;
}

/*
 * SDError()
 * 
 */

void SDError()
{
  displaySDError();
  while(1)  // get stuck here forever.
  {
    // Use the 1Hz square wave output of the chronodot to blink the onboard led at 1Hz to indicate an error.
    digitalWrite(ledPin,digitalRead(sqwPin));
  }
}

/*
 * Display routines
 * 
 */

void displayTime()
{
  char iso8601[] = "YYYYMMDD hhmmss";
  sprintf(iso8601, "%04u%02u%02u %02u%02u%02u", chron.timeDate.year, chron.timeDate.month, chron.timeDate.day, chron.timeDate.hours, chron.timeDate.minutes, chron.timeDate.seconds);
  lcd.setCursor(0,0);
  lcd.print(iso8601);
}

void displayTemperature(float reading)
{
  boolean negate = false;
  if(reading < 0)
  { // if the reading is negative, make it positive to simplify rounding, and remember to make negative later
    negate = true;
    reading = reading * -1;
  }
  char formattedTemperature[] = "+000.0C";
  
  int temperatureInteger = (int)reading; // Capture the integer part by implicit datatype conversion
  float temperatureDecimal = reading - (float)temperatureInteger; // Capture the first 3 decimal points
  int temperatureDecimal1 = temperatureDecimal * 10; // Capture the hundredths place of the 3 decimal points
  if(negate)
  { // reinstate negative if need be.
    temperatureInteger = temperatureInteger * -1;
  }
  sprintf(formattedTemperature, "% 4u.%1uC", temperatureInteger, temperatureDecimal1);
  lcd.setCursor(0,1); // first character of the last row
  lcd.print(formattedTemperature);
}

void displayReadIndicator()
{
  char spinner[] = "|}>}|{<{";
  static byte spinnerIndex = 0;
  lcd.setCursor(15,1); // last character of the last row
  lcd.print(spinner[spinnerIndex]);
  spinnerIndex += 1;
  if(spinnerIndex == 8)
  {
    spinnerIndex = 0;
  }
}

void displaySDError()
{
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("* SDCard Error *"));
}

void setupTimeSeries()
{
  // For future use
}

void updateTimeSeries(float reading)
{
  // For future use
}
