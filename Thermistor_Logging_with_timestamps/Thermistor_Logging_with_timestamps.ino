/*
 * Thermistor Logging with timestamps - Use a MAX31855 to interface with a K-type thermocouple. Take multiple readings over a second,
 * and average the median 5. Use a chronodot to create a timestamp for a filename and save the 1 second temperature average with a
 * timestamp to the file in csv format.
 *
 * By Chris "Sembazuru" Elliott, SembazuruCDE (at) GMail.com
 * 2014-09-27
 */

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
  }
/*
  Serial.println(temperatures.getCount());
*/

}


/*
 * dataRecord(byte)
 *
 */
byte dataRecord(byte sqwState)
{
  char dateBuffer[20];
  
  if (sqwState < lastSQW) // Only time this is true is if the SQWPin changed state from high to low since the last loop. This is the falling edge to trigger on.
  {
    chron.readTimeDate();
    sprintf(dateBuffer, "%04u-%02u-%02u %02u:%02u:%02u", chron.timeDate.year, chron.timeDate.month, chron.timeDate.day, chron.timeDate.hours, chron.timeDate.minutes, chron.timeDate.seconds);
    dataFile.print(dateBuffer);
    dataFile.print(',');
    if (temperatures.getCount() > 0){
      dataFile.print(temperatures.getAverage(medianAverageSize), 2);
    }
    dataFile.print(',');
    dataFile.println(temperatures.getCount());
/*
    Serial.print(dateBuffer);
    Serial.print(',');
    Serial.print(temperatures.getAverage(medianAverageSize), 2);
    Serial.print(',');
    Serial.println(temperatures.getCount());
*/
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
  while(1)  // get stuck here forever.
  {
    // Use the 1Hz square wave output of the chronodot to blink the onboard led at 1Hz to indicate an error.
    digitalWrite(ledPin,digitalRead(sqwPin));
  }
}

