#include <Arduino.h>

/*
Cats Ear Version 2

software for cat feeder hardware
Modules used
pcf8574n
74hc165
74hc595
Id3la
lcd
DPDT Relay


Nicholas Herbert
*/
#include <Feeder.h>
#include <Rfid.h>
#include <Wire.h>
#include "Bitmask.h"

//required for the pcf8574n
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header
hd44780_I2Cexp lcd; // declare lcd object: auto locate & config exapander chip
const int LCD_ROWS = 2;// LCD geometry
const int LCD_COLS = 16;

//required for 74hc165n
const int clock_pin_165       = 7;
const int clockEnable_Pin_165 = 10;
const int load_pin_165        = 9;
const int data_pin_165        = 8;
const int led_pin             = 13;

int NUMBER_OF_SHIFT_CHIPS  = 2; /* How many shift register chips are daisy-chained.*/
int DATA_WIDTH = NUMBER_OF_SHIFT_CHIPS * 8;/* Width of data (how many ext lines).*/
#define PULSE_WIDTH_USEC   5   /* Width of pulse to trigger the shift register to read and latch.*/

Bitmask16 io_bits;
Bitmask16 io_bits_old;
Bitmask8  output_bits;

//required for 74hc595
const int latch_pin_595 = 12;
const int clock_pin_595 = 11;
const int data_pin_595  = 13;

//required for Relay
const int relayTogglePin = 2;

//catfeeder Options
bool option_display_uptime = true;


enum component_input_bits { FISH = 0,
                      CRABS = 1,
                      JELLYFISH = 2,
                      SHARKS = 3,
                      VIB_2 = 4,
                      LASER_2 = 5,
                      WHALES = 6,
                      DOLPHINS = 7,
                      BUTTON_LEFT = 8,
                      BUTTON_UP = 9,
                      BUTTON_DOWN = 10,
                      BUTTON_B = 11,
                      BUTTON_A = 12,
                      BUTTON_START = 13,
                      BUTTON_RIGHT = 14,
                      BUTTON_SELECT = 15};

enum component_output_bits { RED   = 8,
                            GREEN = 7,
                            BLUE  = 6,
                            EXTRA = 5,
                            LASER = 4,
                            LED = 3,
                            FEED2 = 2,
                            FEED1 = 1};


enum feline{kaylee,minna};

struct stat{
  feline catName;
  String rfid;
  int opens;
  unsigned long canFeedFor;
};

struct stat Kaylee;
struct stat Minna;

class Feeder;
class Rfid;



//LCD display helper function
int lcdOffsetByDigit(int opens);

//LCD display feeding info
void printFeedStat(int opens,int myCat);

//read bytes off the 165
void set_595_shift_regs(byte data){
  digitalWrite(latch_pin_595, LOW);
  shiftOut(data_pin_595, clock_pin_595, MSBFIRST, data);
  digitalWrite(latch_pin_595, HIGH);
}
void read_165_shift_regs(){
    long bitVal;

    /* Trigger a parallel Load to latch the state of the data lines,
    */
    digitalWrite(clockEnable_Pin_165, HIGH);
    digitalWrite(load_pin_165, LOW);
    delayMicroseconds(PULSE_WIDTH_USEC);
    digitalWrite(load_pin_165, HIGH);
    digitalWrite(clockEnable_Pin_165, LOW);

    /* Loop to read each bit value from the serial out line
     * of the SN74HC165N.
    */
    for(int i = 0; i < DATA_WIDTH; i++){
        bitVal = digitalRead(data_pin_165);
        io_bits.SetOrClear(i, bitVal);//Set the corresponding bit in io_bits.
        digitalWrite(clock_pin_165, HIGH); //Pulse the Clock (rising edge shifts the next bit).
        delayMicroseconds(PULSE_WIDTH_USEC);
        digitalWrite(clock_pin_165, LOW);
    }
}
/* Dump the list of zones along with their current status.
*/
void display_pin_values()
{
    Serial.print("Pin States:\r\n");

    for(int i = 0; i < DATA_WIDTH; i++){
        Serial.print("  Pin-");
        Serial.print(i);
        Serial.print(": ");

        if(io_bits.IsSet(i))
          Serial.print("IO_HIGH");
        else
          Serial.print("IO_LOW");
        Serial.print("\r\n");
    }

    Serial.print("\r\n");
}


void write_feeder_state(Feeder feeder){

  //00101001
  // the_states & 0b01000000 ;
  //
  //set enums for the bit positions
  //enum MyFlags { PONIES = 1, DOLPHINS = 2 };
  //((incoming_bytes_165 >> i) & 1)
  //(incomming_bytes_165 &(1<<i))

  // display byte values is only needed for debugging.
  display_pin_values();


}




Feeder feeder1(FEED1,100,500,3,10,105,0,1000);
Feeder feeder2(FEED2,100,500,3,10,105,0,1000);
int tx_rx[2] = {8,9};
Rfid rfid(tx_rx, "start up", 0);

void setup(){
  Serial.begin(9600);
  // initialize LCD with number of columns and rows:
  if( lcd.begin(LCD_COLS, LCD_ROWS)){
    // begin() failed so blink the onboard LED if possible
    fatalError(1); // this never returns
  }

  output_bits.ClearAll();
  io_bits.ClearAll();
  NUMBER_OF_SHIFT_CHIPS = 2;
  DATA_WIDTH = NUMBER_OF_SHIFT_CHIPS * 8;

  lcd.backlight();

  /* Initialize our digital pins...
  */
  pinMode(load_pin_165, OUTPUT);
  pinMode(clockEnable_Pin_165, OUTPUT);
  pinMode(clock_pin_165, OUTPUT);
  pinMode(data_pin_165, INPUT);

  digitalWrite(clock_pin_165, LOW);
  digitalWrite(load_pin_165, HIGH);

  pinMode(latch_pin_595, OUTPUT);
  pinMode(clock_pin_595,OUTPUT);
  pinMode(data_pin_595,OUTPUT);

  // set the initial bits to zero
  set_595_shift_regs(output_bits.GetValue());

  /* Read in and display the pin states at startup.
  */
  read_165_shift_regs();
  display_pin_values();
  io_bits_old.CopyAll(io_bits);

  //if the 13th bit is HIGH then there is no controller attached recaculate data width
  //EG send 00001000 recieve 0000100000001000 = no controller attached
  //   send 00001000 recieve 0000100000000000 = controller is attached
  if (io_bits.IsSet(13)) {
    NUMBER_OF_SHIFT_CHIPS = 1;
    DATA_WIDTH = NUMBER_OF_SHIFT_CHIPS * 8;
  }

  // Timer0 is already used for millis() - we'll just interrupt somewhere
  // in the middle and call the "Compare A" function below
  OCR0A = 0xAF;
  TIMSK0 |= _BV(OCIE0A);

  lcd.print("Hello, world!");
}
//
// // Interrupt is called once a millisecond,
SIGNAL(TIMER0_COMPA_vect){
   read_165_shift_regs();
   set_595_shift_regs(output_bits.GetValue());
   unsigned long currentMillis = millis();
//
//   //set the state of the feeders based on outputs from 595
   feeder1.update(currentMillis);
//   feeder2.update(currentMillis);
//   //set the state of the rfid module
//   rfid.update(currentMillis); //rfid will read here
//
//
//
//   //write outputs from 595
//   //here we are telling the feeders and controller what to do next frame.
//   //outputs to
//   //mulitcolorled
//
//
//   //servo open or closed
//   if (io_bits.IsSet(1)){
//     feeder1.set_indicator_light(true);
//     feeder2.set_indicator_light(false);
//     feeder1.servo_open();
//   }
//   //feeder state led on onff
//   if( io_bits.IsSet(2)){
//     feeder1.set_indicator_light(true);
//     feeder2.set_indicator_light(false);
//     feeder1.servo_open();
//   }
//   if (io_bits.IsSet(3)){
//     feeder2.set_indicator_light(true);
//     feeder1.set_indicator_light(false);
//     feeder2.servo_open();
//   }
//   //feeder state led on onff
//   if( io_bits.IsSet(4)){
//     feeder2.set_indicator_light(true);
//     feeder1.set_indicator_light(false);
//     feeder2.servo_open();
//   }

if (io_bits.IsSet(VIB_2)){
  output_bits.Set(LASER);
}

if (io_bits.IsSet(LASER_2)){
  output_bits.Clear(LED);
}
else{
  output_bits.Set(LED);
}
if(io_bits.IsSet(BUTTON_A)){
  output_bits.Set(LASER);
}
if(io_bits.IsSet(BUTTON_B)){
  output_bits.Clear(LASER);
}
if(io_bits.IsSet(BUTTON_LEFT)){
  feeder1.set_indicator_light(output_bits);
}
else{
  feeder1.clear_indicator_light(output_bits);
}
if(io_bits.IsSet(BUTTON_RIGHT)){
  output_bits.Set(GREEN);
}
else{
  output_bits.Clear(GREEN);
}
if(io_bits.IsSet(BUTTON_DOWN)){
  output_bits.Set(BLUE);
}
else{
  output_bits.Clear(BLUE);
}

if(io_bits.IsSet(BUTTON_UP)){
  output_bits.Set(RED);
}
else{
  output_bits.Clear(RED);
}

}
//



void loop(){

  if(option_display_uptime){
    display_uptime();
  }
  // printFeedStat(feeder1.number_of_opens(), 0);
  // printFeedStat(feeder2.number_of_opens, 1);
  /* If there was a chage in state, display which ones changed.
  */
  if(io_bits.GetValue() != io_bits_old.GetValue()){
      Serial.print("*Pin value change detected*\r\n");
      display_pin_values();
      io_bits_old.CopyAll(io_bits);
  }





}

void display_uptime(){
  static unsigned long lastsecs = -1; // pre-initialize with non zero value
  unsigned long secs;

    secs = millis() / 1000;

    // see if 1 second has passed
    // so the display is only updated once per second
    if(secs != lastsecs){
      lastsecs = secs; // keep track of last seconds

      // set the cursor position to column 0, row 1
      // note: row 1 is the second row from top,
      // since row counting begins with 0
      lcd.setCursor(0, 1);

      // print uptime on lcd device: (time since last reset)
      PrintUpTime(lcd, secs);
    }
}
// PrintUpTime(outdev, secs) - print uptime in HH:MM:SS format
// outdev - the device to send output
//   secs - the total number of seconds uptime
void PrintUpTime(Print &outdev, unsigned long secs)
{
unsigned int hr, mins, sec;

	// convert total seconds to hours, mins, seconds
	mins =  secs / 60;	// how many total minutes
	hr = mins / 60;		// how many total hours
	mins = mins % 60;	// how many minutes within the hour
	sec = secs % 60;	// how many seconds within the minute


	// print uptime in HH:MM:SS format
	// Print class does not support fixed width formatting
	// so insert a zero if number smaller than 10
	if(hr < 10)
		outdev.write('0');
	outdev.print((int)hr);
	outdev.write(':');
	if(mins < 10)
		outdev.write('0');
	outdev.print((int)mins);
	outdev.write(':');
	if(sec < 10)
		outdev.write('0');
	outdev.print((int)sec);
}

//LCD display feeding info
void printFeedStat(int opens,int myCat){
  switch(myCat){
    case kaylee:
      lcd.setCursor(0, 0);
      lcd.print(opens,DEC);
      lcd.print("  <- Kaylee");
      break;
    case minna:
      lcd.setCursor(lcdOffsetByDigit(opens), 1);
      lcd.print("Minna ->  ");
      lcd.print(opens,DEC);
      break;
  }
}

//LCD display helper function
int lcdOffsetByDigit(int opens){
  int distance = 0;
  if(opens >= 0 && opens < 10){
     distance = 5;
  }
  else if(opens >= 10 && opens < 100){
    distance = 4;
  }
  else if(opens >= 100){
    distance = 3;
  }
  else{
     distance = 1;
  }
  return distance;
}

// fatalError() - loop & blink an error code
void fatalError(int ecode)
{
	hd44780::fatalError(ecode); // does not return
}
