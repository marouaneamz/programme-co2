/* For Senseair S8 004-0-0053
Vorlage : https://github.com/liutyi/esp32-oled-senseair/blob/787e3901e96e5c13aa463619f402b9873a8df80c/wemos-simple_co2meter.ino
Vorlage : https://github.com/Alexey-Tsarev/SensorsBox/blob/0b3699d5d33254caaf5594c4bb6daf278c9c700c/src/SensorsBox-firmware/SenseAirS8.cpp
//   Board: ESP-Dev
//   Anschlüsse Pin 21 und Pin 22
//   Kommunikationsformat mit SenseAir
//   byte Request[] = {0xFE, 0X04, 0X00, 0X03, 0X00, 0X01, 0XD5, 0XC5};      lt. Doc von SenseAir TDE2067
//   0     FE    any Address
//   1     04    function code  (03 read hold reg / 04 read inp. register / 06 write)
//   2,3   00 03 starting Address 
//   4,5   00 01 lenght of requested register
//   6,7   D5 C5 CRC low / high
//   Response = {0xFE, 0X04, 0X02, 0X03, 0X88, 0XD5, 0XC5};
//   0     FE    same Address as in Request
//   1     04    same funcation as in Request
//   2,3   02 03 Value of response (eg. CO-Value) (depending on requested registers)
//   4,5   D5 C5 CRC Low / high
//
//   Not documented Funtion 44 {0xFE, 0x44, 0X00, 0X08, 0X02, 0X9F, 0X25}; - reads also the CO2-Value (length is in bytes 0X02)
*/
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif
#include <Wire.h>


#define TXD2 21
#define RXD2 22

// Which pin on the Arduino is connected to the NeoPixels?
#define PIN        19// On Trinket or Gemma, suggest changing this to 1

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 8 // Popular NeoPixel ring size

// When setting up the NeoPixel library, we tell it how many pixels,
// and which pin to use to send signals. Note that for older NeoPixel
// strips you might need to change the third parameter -- see the
// strandtest example for more information on possible values.
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);


#define DELAYVAL 0 // Time (in milliseconds) to pause between pixels

byte CO2req[] = {0xFE, 0X04, 0X00, 0X03, 0X00, 0X01, 0XD5, 0XC5};
byte Response[20];
uint16_t crc_02;
int ASCII_WERT;
int int01, int02, int03;
unsigned long ReadCRC;      // CRC Control Return Code 

// Ausselesen ABC-Periode / FirmWare / SensorID 
byte ABCreq[] = {0xFE, 0X03, 0X00, 0X1F, 0X00, 0X01, 0XA1, 0XC3};   // 1f in Hex -> 31 dezimal
byte FWreq[] = {0xFE, 0x04, 0x00, 0x1C, 0x00, 0x01, 0xE4, 0x03};    // FW      ->       334       1 / 78
byte ID_Hi[] = {0xFE, 0x04, 0x00, 0x1D, 0x00, 0x01, 0xB5, 0xC3};    // Sensor ID hi    1821       7 / 29
byte ID_Lo[] = {0xFE, 0x04, 0x00, 0x1E, 0x00, 0x01, 0x45, 0xC3};    // Sensor ID lo   49124     191 / 228 e.g. in Hex 071dbfe4
//byte Tstreq[] = {0xFE, 0x03, 0x00, 0x01, 0x00, 0x01, 0xE4, 0x03};   // Read the holding register
//byte Tstreq[] = {0xFE, 0x04, 0x00, 0x01, 0x00, 0x01, 0xE4, 0x03}; // Read the input Register
byte Tstreq[] = {0xFE, 0x44, 0X00, 0X01, 0X02, 0X9F, 0X25};       // undocumented function 44
int Test_len = 7;               // length of Testrequest in case of function 44 only 7 otherwise 8

void send_Request (byte * Request, int Re_len)
{
  while (!Serial1.available())
  {
    Serial1.write(Request, Re_len);   // Send request to S8-Sensor
    delay(50);
  }
}

void read_Response (int RS_len)
{
  int01 = 0;
  while (Serial1.available() < 7 ) 
  {
    int01++;
    if (int01 > 10)
    {
      while (Serial1.available())
        Serial1.read();
      break;
    }
    delay(50);
  }
  for (int02 = 0; int02 < RS_len; int02++)    // Empfangsbytes
  {
    Response[int02] = Serial1.read();
  }
}

unsigned short int ModBus_CRC(unsigned char * buf, int len)
{
  unsigned short int crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (unsigned short int)buf[pos];   // XOR byte into least sig. byte of crc
    for (int i = 8; i != 0; i--) {         // Loop over each bit
      if ((crc & 0x0001) != 0) {           // If the LSB is set
        crc >>= 1;                         // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }  // Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
  return crc;  
}

unsigned long get_Value(int RS_len)
/* Extract the Value from the response (byte 3 and 4)  
    and check the CRC if the response is compleat and correct */
{
  /* Loop only for test and developtment purpose */
//  for (int i=0; i<RS_len-2; i++)
//  {
//    int03 = Response[i];
//    Serial.printf("Wert[%i] : %i / ", i, int03);
//  }

// Check the CRC //
  ReadCRC = (uint16_t)Response[RS_len-1] * 256 + (uint16_t)Response[RS_len-2];
  if (ModBus_CRC(Response, RS_len-2) == ReadCRC) {
    // Read the Value //
    unsigned long val = (uint16_t)Response[3] * 256 + (uint16_t)Response[4];
    return val * 1;       // S8 = 1. K-30 3% = 3, K-33 ICB = 10
  }
  else {
    Serial.print("Error");
    return 99;
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, RXD2, TXD2);      // UART to Sensair CO2 Sensor
  
  Serial.println();
  Serial.print("ABC-Tage: ");  
  send_Request(ABCreq, 8);                     // Request ABC-Information from the Sensor
  read_Response(7);
  Serial.printf("%02ld", get_Value(7));
  Serial.println("");

  Serial.print("Sensor ID : ");                // 071dbfe4
  send_Request(ID_Hi, 8);
  read_Response(7);
  Serial.printf("%02x%02x", Response[3], Response[4]);
  send_Request(ID_Lo, 8);
  read_Response(7);
  Serial.printf("%02x%02x", Response[3], Response[4]);
  Serial.println("");

  Serial.print("FirmWare  : ");
  send_Request(FWreq, 8);                // Send Request for Firmwar to the Sensor
  read_Response(7);                   // get Response (Firmware 334) from the Sensor
  Serial.printf("%02d%02d", Response[3], Response[4]);
  Serial.println("");

 // Read all Register 
  for (int i=1; i<=31; i++)
  {
    Tstreq[3] = i;
    crc_02 = ModBus_CRC(Tstreq,Test_len-2);     // calculate CRC for request
    Tstreq[Test_len-1] = (uint8_t)((crc_02 >> 8) & 0xFF);
    Tstreq[Test_len-2] = (uint8_t)(crc_02 & 0xFF);
    delay(100);
    Serial.print("Registerwert : ");
    Serial.printf("%02x", Tstreq[0]);
    Serial.printf("%02x", Tstreq[1]);
    Serial.printf("%02x", Tstreq[2]);
    Serial.print(" ");
    Serial.printf("%02x", Tstreq[3]);
    Serial.print(" ");
    Serial.printf("%02x", Tstreq[Test_len-3]);
    Serial.printf("%02x", Tstreq[Test_len-2]);
    Serial.printf("%02x", Tstreq[Test_len-1]);

    send_Request(Tstreq, Test_len);
    read_Response(7);
    // Check if CRC is OK 
    ReadCRC = (uint16_t)Response[6] * 256 + (uint16_t)Response[5];
    if (ModBus_CRC(Response, Test_len-2) == ReadCRC) {
      Serial.printf(" -> (d) %03d %03d", Response[3], Response[4]);
      Serial.printf(" -> (x) %02x %02x", Response[3], Response[4]);
      Serial.printf(" ->(Wert) %05d", (uint16_t)Response[3] * 256 + (uint16_t)Response[4]);
      ASCII_WERT = (int)Response[3];
      Serial.print(" -> (t) ");
      if ((ASCII_WERT <= 128) and (ASCII_WERT >= 47)) 
      {
        Serial.print((char)Response[3]);
        Serial.print(" ");
        ASCII_WERT = (int)Response[4];
        if ((ASCII_WERT <= 128) and (ASCII_WERT >= 47)) 
        {
          Serial.print((char)Response[4]);
        }
        else
        {
          Serial.print(" ");
        }
      }
      else 
      {
        Serial.print("   ");
      }
      if ((Response[1] == 68) and (Tstreq[3] == 8))
         Serial.print("   CO2");      
      Serial.println("  Ende ");
    }
    else 
      Serial.print("CRC-Error");
  }
    // These lines are specifically to support the Adafruit Trinket 5V 16 MHz.
  // Any other board, you can remove this part (but no harm leaving it):
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif
  // END of Trinket-specific code.

  pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
}  

void loop() {
  send_Request(CO2req, 8);               // send request for CO2-Data to the Sensor
  read_Response(7);                      // receive the response from the Sensor
  unsigned long CO2 = get_Value(7);
  delay(2000);
  String CO2s = "CO2: " + String(CO2);
  Serial.println(CO2s);
  if (CO2 < 430){
  pixels.clear(); // Set all pixel colors to 'off'

  // The first NeoPixel in a strand is #0, second is 1, all the way up
  // to the count of pixels minus one.
  for(int i=0; i<NUMPIXELS; i++) { // For each pixel...

    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    // Here we're using a moderately bright green color:
    pixels.setPixelColor(i, pixels.Color(0, 0, 255));

    pixels.show();   // Send the updated pixel colors to the hardware.

    delay(DELAYVAL); // Pause before next pass through loop
}
  }
else if (CO2 < 800){
  pixels.clear(); // Set all pixel colors to 'off'

  // The first NeoPixel in a strand is #0, second is 1, all the way up
  // to the count of pixels minus one.
  for(int i=0; i<NUMPIXELS; i++) { // For each pixel...

    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    // Here we're using a moderately bright green color:
    pixels.setPixelColor(i, pixels.Color(0, 255, 0));

    pixels.show();   // Send the updated pixel colors to the hardware.

    delay(DELAYVAL); // Pause before next pass through loop
}
}
else if (CO2 >= 800 && CO2 <1000){
  pixels.clear(); // Set all pixel colors to 'off'

  // The first NeoPixel in a strand is #0, second is 1, all the way up
  // to the count of pixels minus one.
  for(int i=0; i<NUMPIXELS; i++) { // For each pixel...

    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    // Here we're using a moderately bright green color:
    pixels.setPixelColor(i, pixels.Color(255, 150, 0));

    pixels.show();   // Send the updated pixel colors to the hardware.

    delay(DELAYVAL); // Pause before next pass through loop
}
}
else if (CO2 >= 1000){
  pixels.clear(); // Set all pixel colors to 'off'

  // The first NeoPixel in a strand is #0, second is 1, all the way up
  // to the count of pixels minus one.
  for(int i=0; i<NUMPIXELS; i++) { // For each pixel...

    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    // Here we're using a moderately bright green color:
    pixels.setPixelColor(i, pixels.Color(255, 0, 0));

    pixels.show();   // Send the updated pixel colors to the hardware.

    delay(DELAYVAL); // Pause before next pass through loop
}
}
}
