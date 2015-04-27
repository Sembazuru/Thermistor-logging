/*
 * Analog Logging with display on a 2-line LCD display.
 *
 * By Chris "Sembazuru" Elliott, SembazuruCDE (at) GMail.com
 * 2015-04-24
 */

// 16x2 LCD Display
#include <LiquidCrystal.h>
const byte LCD_RS = 2; // LCD pin4
const byte LCD_Enable = 3; // LCD pin6
const byte LCD_D4 = 6; // LCD pin11
const byte LCD_D5 = 7; // LCD pin12
const byte LCD_D6 = 8; // LCD pin13
const byte LCD_D7 = 9; // LCD pin14
// LCD_VSS (LCD pin1) to GND
// LCD_VDD (LCD pin2) to +5
// 10k Pot +5 to GND, wiper to LCD_V0 (LCD pin3) for contrast
// LCD_R/W (LCD pin5) to ground
LiquidCrystal lcd(LCD_RS, LCD_Enable, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Chronodot
#include <Wire.h>
#include <Chronodot.h>
Chronodot chron = Chronodot();
const byte sqwPin = 5; // Configure 1Hz square wave to this pin to provide a falling edge trigger (non interrupt) for 1 second timing.

// Analog input to monitor
const byte SensorPin = A2; // After the LED (see "Other globals", below) and I2C, only A2 and A3 are left on a UNO.

// SD Card
#include <SPI.h>
#include <SD.h>
const byte SD_CS = 4;  // Chip select for accessing the SD card.
File dataFile;  // make the file pointer a global, but don't try to open it yet.
char filename[] = "MMDDhhmm.csv";  // filename template.

// RunningMdian
#include <RunningMedian.h>
const byte medianAverageSize = 5;
RunningMedian analogADC = RunningMedian(15);

// Other globals
boolean indicatorLED = false;  // Tracking variable for the state of ledPin during normal operation.
unsigned long lastSample = 0;  // Timing variable set when last reading is done to know when to do the next reading.
const int sampleInterval = 100;  // 100 milliseconds between reads.
byte lastSQW; // Holding variable for square wave state
const byte ledPin = A1;
const byte ledGnd = A0;

void setup()
{
  pinMode(sqwPin, INPUT_PULLUP);
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
  dataFile.println(F("Date_Time,Raw ADC,Voltage,Samples"));
  dataFile.flush();
  for (byte i = 0; i < 10; i++) // ensure the first reported reading has at least 1 second's worth of readings.
  {
    readSensor();
    delay(100);
  }
}

void loop()
{
  // Every 100ms add a temperature reading to the running median buffer.
  if (millis() - lastSample >= sampleInterval)
  {
    readSensor();
    lastSample = millis(); // enforce 100ms between samples, not necessarily 1 sample every 100 ms. Subtle difference.
  }

  // When the Square wave output of the chronodot goes from high to low, write a line to the file and clear the buffer.
  lastSQW = dataRecord(digitalRead(sqwPin));
}

/* readSensor(void)
 *
 */
void readSensor()
{
  indicatorLED = !indicatorLED;
  digitalWrite(ledPin, indicatorLED);
  unsigned int rawADC = analogRead(SensorPin);
  analogADC.add(rawADC);
  displayReadIndicator();
}


/* dataRecord(byte)
 *
 */
byte dataRecord(byte sqwState)
{
  if (sqwState < lastSQW) // Only time this is true is if the SQWPin changed state from high to low since the last loop. This is the falling edge to trigger on.
  {
    char dateBuffer[] = "YYYY-MM-DD hh:mm:ss";
    chron.readTimeDate();
    displayTime();
    sprintf(dateBuffer, "%04u-%02u-%02u %02u:%02u:%02u", chron.timeDate.year, chron.timeDate.month, chron.timeDate.day, chron.timeDate.hours, chron.timeDate.minutes, chron.timeDate.seconds);
    dataFile.print(dateBuffer);
    dataFile.print(',');
    unsigned int averageADC;
    if (analogADC.getCount() > 0) {
      averageADC = analogADC.getAverage(medianAverageSize);
      dataFile.print(averageADC);
      displayTemperature(averageADC);
    }
    dataFile.print(',');
    if (analogADC.getCount() > 0) {
      dataFile.print(ADCtoV(averageADC), 3);
    }
    dataFile.print(',');
    dataFile.println(analogADC.getCount());
    dataFile.flush();
    analogADC.clear();
  }
  return sqwState;
}

/* SDError()
 *
 */

void SDError()
{
  displaySDError();
  while (1) // get stuck here forever.
  {
    // Use the 1Hz square wave output of the chronodot to blink the onboard led at 1Hz to indicate an error.
    digitalWrite(ledPin, digitalRead(sqwPin));
  }
}

/* ADCtoV(unsigned int) - convert ADC value to Voltage. Return a float.
 *
 */

float ADCtoV(unsigned int a)
{
  return (float)(a * 5) / 1024.0;
}

/* Display routines
 *
 */

void displayTime()
{
  char iso8601[] = "YYYYMMDD hhmmss";
  sprintf(iso8601, "%04u%02u%02u %02u%02u%02u", chron.timeDate.year, chron.timeDate.month, chron.timeDate.day, chron.timeDate.hours, chron.timeDate.minutes, chron.timeDate.seconds);
  lcd.setCursor(0, 0);
  lcd.print(iso8601);
}

void displayTemperature(unsigned int reading)
{
  char formattedReading[] = "0000cnts  0.000V";

  //Convert to voltage and separate integer part from decimal part because sprintf() doesn't handle floats in AVR gcc
  float voltage = ADCtoV(reading);
  int voltageInteger = (int)voltage; // Capture the integer part by explicit datatype conversion
  float decimal = voltage - (float)voltageInteger; // Remove the integer portion
  int voltageDecimal = decimal * 1000; // Move the decimal point 2 places and trim the rest of the decimal by implicit datatype conversion

  sprintf(formattedReading, "%4ucnts  %1u.%03uV", reading, voltageInteger, voltageDecimal);
  lcd.setCursor(0, 1); // first character of the last row
  lcd.print(formattedReading);
}

void displayReadIndicator()
{
  char spinner[] = "|}>}|{<{";
  static byte spinnerIndex = 0;
  lcd.setCursor(15, 0); // last character of the first row
  lcd.print(spinner[spinnerIndex]);
  spinnerIndex ++;
  if (spinnerIndex == 8)
  {
    spinnerIndex = 0;
  }
}

void displaySDError()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("* SDCard Error *"));
}

