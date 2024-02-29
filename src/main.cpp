/*

Cat "stay-off-counter" airPuffer alarm.   Laser range finder detects cat.  Two distance zones defined: MEDIUM and NEAR.
If cat gets within MEDIUM, then a warning beep sounds.  If the cat gets within NEAR distance, a short puff of air is 
released from an air tank via an air valve.  

There is an "armed/disarmed" LED to indicate when the air-puffer is active.  
Red flashing means air-puffer is enabled.  Green flashing means just the buzzer is active.  

When the system is powered on, a "puff-counter" is initialized.  When MAX_PUFFS count is reached, the LED starts 
flashing blue to indicate you are out of air.  The buzzer will still function, just the air puffer will be disabled.
Note, if you power up with a partial air fill,  the count will not reflect your actual available air.

The detection zone is controlled with a potentiometer that will adjust from 0mm to 1023mm.  This defines the MEDIUM zone.  
NEAR gets set to 50% of MEDIUM.

Inspired by an instructable project I saw at: https://www.instructables.com/Pet-Deterrent-keep-them-away-from-those-off-limits/

dlf  2/23/2024
*/

#include <Arduino.h>
#include "Adafruit_VL53L0X.h"

// ENUMS
enum colorEnum {RED, GREEN, BLUE};

// Function prototypes
//dlf int getDistance();
boolean checkSwitch(uint8_t pin);
void flashLED(colorEnum color, uint8_t brightness, int howLong);

// Misc definitions
const uint8_t SWITCH_PRESSED    = 0;  // Using switches to ground with a GPIO internal pullup so "pressed" = a zero
const uint8_t SWITCH_UNPRESSED  = 1;
const uint8_t RELAY_ON    = 0;  // Relay active low enable
const uint8_t RELAY_OFF  = 1;

// Range definitions
VL53L0X_RangingMeasurementData_t measureLaserRange;
Adafruit_VL53L0X laserRanger = Adafruit_VL53L0X();
int distanceToObject = 5000;   // Set to big default number in case we want to test for out of range

// **************   I/O Pin Definitions  *********
const uint8_t BUZZER = 2;           // Buzzer + pin
const uint8_t DISARM_AIR_SWITCH = 3;    // For diaabling the air-puffer
const uint8_t LED_R = 9;            // Red input of RGB LED
const uint8_t LED_G = 10;           // Green input of RGB LED
const uint8_t LED_B = 11;           // Blue input of RGB LED
const uint8_t AIR_RELAY = 5;        // Air-switch Relay control pin
const uint8_t LASER_RANGER_XSHUT_PIN = 8;  // Laser range finder enable pin

// State vars
boolean airArmed = false;   // True if we have the air puffer enabled
boolean outOfAir = false;   // Keep track of when we're out of air

// air puffer
const uint8_t MAX_PUFFS = 15;         // The number of puffs per fill of the tank
const int PUFF_LOCKOUT_TIME = 5000;   // Amount of time before we will puff again
uint8_t puffsSoFar = 0;               // Keep track of the number of puffs so we can signal when out of air 
int puffLength = 100;                 // How long the air valve will be on (ms)

// Timer vars
const int CHECK_INTERVAL = 100;               // How often to check the range sensor
unsigned long lastCheck = millis();           // For checking the last time we did a distance ranging
const int OUT_OF_AIR_FLASH_INTERVAL = 1000;   // Out of air flasher period
unsigned long outOfAirFlasherLastCheck = millis();  // For blinking the out of air led
const int WARNING_BUZZ_DURATION = 1000;       // How long (ms) to buzz the buzzer to warn of proximity violation
const int FLASH_ON_TIME = 200;                // Flash the LED red or green so show armed/disarmed
unsigned long ARMED_DISARMED_FLASH_INTERVAL = 1000;  
unsigned long lastFlashTime = millis();
unsigned long lastPuffTime = millis();


//##########################################################################
// Run Once Setup 
//##########################################################################
void setup() {
  // Turn on serial monitor
  Serial.begin(115200);
  delay(1000);

  // Misc IO definitions
  pinMode(BUZZER,OUTPUT);
  pinMode(DISARM_AIR_SWITCH,INPUT_PULLUP);

  // PWM pins to RGB LED
  pinMode(LED_R,OUTPUT);   
  pinMode(LED_G,OUTPUT);
  pinMode(LED_B,OUTPUT);

  // Relay that drives the air valve
  pinMode(AIR_RELAY,OUTPUT);

  // Pot to set the alarm range.  Sets the mediumAlarmDist.  nearAlarmDist is 50% of that.
  pinMode(A0, INPUT);  //Range potentiometer

  // Turn everything off
  digitalWrite(BUZZER,0);
  digitalWrite(AIR_RELAY,RELAY_OFF);

  // Initialize the laser range finders
   pinMode(LASER_RANGER_XSHUT_PIN, OUTPUT);
   if (!laserRanger.begin()) {
     Serial.println(F("Failed to boot laserRanger VL53L0X"));
     while(1);
   }
}

//##########################################################################
// Main loop
//##########################################################################
void loop() {

  int nearAlarmDist, mediumAlarmDist;

  if(checkSwitch(DISARM_AIR_SWITCH) == SWITCH_PRESSED) {
    airArmed = false;
  } else {
    airArmed = true;
  }

  // Check the range Pot.  No mapping necessary as we are using 1023mm as our max distance.
  // So, just read the 0-1023 value and use as our MEDIUM range.  Just make nearAlarmDist 50% of MEDIUM.
  mediumAlarmDist = analogRead(A0);
  nearAlarmDist = mediumAlarmDist/2;

  // Periodically check the range sensor.
  if(millis() >= lastCheck+CHECK_INTERVAL) {
    laserRanger.rangingTest(&measureLaserRange, false); // pass in 'true' to get debug data printout!
    if(measureLaserRange.RangeStatus != 4) {   // 4 indicates out of range
       distanceToObject = measureLaserRange.RangeMilliMeter;
       if(distanceToObject <= nearAlarmDist) {
         Serial.print(F("laserRange NEAR: ")); Serial.println(distanceToObject);
       } else if(distanceToObject <= mediumAlarmDist) {
         Serial.print(F("laserRange MEDIUM: ")); Serial.println(distanceToObject);
       }
    }
    lastCheck = millis();
  }

  // If getting close, buzz the warning buzzer
  if(distanceToObject <= mediumAlarmDist) {
    digitalWrite(BUZZER,1);
    delay(WARNING_BUZZ_DURATION);
    digitalWrite(BUZZER,0);
  } 

  // If within short distance limit puff the air.
  if(airArmed && distanceToObject <= nearAlarmDist) {
    if(puffsSoFar < MAX_PUFFS && millis() > lastPuffTime + PUFF_LOCKOUT_TIME) {
      // Puff the air
      digitalWrite(AIR_RELAY,RELAY_ON);
      delay(puffLength);
      digitalWrite(AIR_RELAY,RELAY_OFF);
      lastPuffTime = millis();
      puffsSoFar++;

      //  Just a counter so if we power on without a full tank of air, it will be inaccurate.
      //  Could add a pressure sensor on the tank but I think that would be overkill!
      //  Keeping it simple.  Fill the air tank, then turn on the system.
      if(puffsSoFar == MAX_PUFFS) {
        outOfAir = true;
      }
    } 
  }

  // Flash the LED to indicate air armed or disarmed
  if(!outOfAir && millis() >= lastFlashTime + ARMED_DISARMED_FLASH_INTERVAL) {
    if(airArmed) {
      flashLED(RED,128,FLASH_ON_TIME);
    } else {
      flashLED(GREEN,128,FLASH_ON_TIME);
    }
    lastFlashTime = millis();
  }

  // If out of air, flash LED blue. 
  if(outOfAir) {
    if(millis() >= outOfAirFlasherLastCheck + OUT_OF_AIR_FLASH_INTERVAL) {
      flashLED(BLUE,128,FLASH_ON_TIME);
      outOfAirFlasherLastCheck=millis(); 
    }
  }
}

//##########################################################################
// Functions
//##########################################################################

// Flash the LED
void flashLED(colorEnum color, uint8_t brightness, int howLong) {
  if(color == RED) {
    analogWrite(LED_R,brightness);
    analogWrite(LED_G,0);
    analogWrite(LED_B,0);
  }else if(color == GREEN) {
    analogWrite(LED_R,0);
    analogWrite(LED_G,brightness);
    analogWrite(LED_B,0);
  }else if(color == BLUE) {
    analogWrite(LED_R,0);
    analogWrite(LED_G,0);
    analogWrite(LED_B,brightness);
  }
  delay(howLong);

  // back to black
  analogWrite(LED_R,0);
  analogWrite(LED_G,0);
  analogWrite(LED_B,0);
}

// Check the state of a switch after debouncing it
boolean checkSwitch(uint8_t pin) {
    boolean state;      
    boolean prevState;  
    int debounceDelay = 20;
    prevState = digitalRead(pin);
    for(int counter=0; counter < debounceDelay; counter++) {
        delay(1);       
        state = digitalRead(pin);
        if(state != prevState) {
            counter=0;  
            prevState=state;
        }               
    }                       
    // At this point the switch state is stable
    if(state == HIGH) {     
        return true;    
    } else {            
        return false;   
    }                       
}                       
