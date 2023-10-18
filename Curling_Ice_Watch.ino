/*
Curling Ice Watch - An IoT Ice House Temperature Monitor
2022 Haggarman
It's go time. Thursday 10/20/2022 release 33

SARA LTE modem on the Challenger RP2040 LTE board
Select Tools -> Board -> Raspberry Pi RP2040 Boards -> "iLabs Challenger 2040 LTE"
Uses constants/defines in "pins_arduino.h"

Basic Features:
* Sends TCP messages to Hologram.
* Now monitors and messages about Power Loss, in addition to Temperature Switch Alarm.
* You can power on with dip switch #4 set to on to skip SARA init.
* You can hold down button C on the OLED to power down the SARA modem.
* Green LED flashes when a message send is pending, otherwise it breathes lighter and dimmer.
* Subtle pixel shifting of text to try to head off OLED burn-in.
* Passes USB terminal text over to SARA even if the the state machines are running, but not busy with an auto command.
* Echoes text back to terminal if it involves terminal.

Issues:
* If you press Reset button on the OLED, the display will blank. It does not reset the RP2040.
* Reset for the RP2040 is unlabeled, but it is the one nearest the USB connector on the side.
* The Adafruit battery is protected from over discharge, but the RP2040 could go haywire at low voltage (untested).
* LOL no message rate limiting if sensor would continuously be toggling. YOLO.
* Serial Debug text stuff not quite removed yet. Nor am I comfortable removing it yet.

Required Libraries:
Adafruit SH110X, Adafruit GFX Library

*/
#include "ChallengerLTE.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "StringStuff.h"
#include "SARA_R4_modem.h"
#include "PLC_timers.h"


Adafruit_SH1107 Display = Adafruit_SH1107(64, 128, &Wire);
#define BUTTON_A D9
#define BUTTON_B D6
#define BUTTON_C D5
char str_display_button_pressed[] = "ABC";
char str_animate_button_C[] = "*****";
uint32_t display_refresh_interval_ms = 50;
uint32_t display_refresh_timeref;
char str_input_off[] = "off";
char str_input_on[] = "On";

const char D10_IDstr[] = "Power Detect";
const char D11_IDstr[] = "Temp Sensor";

const char * IceWatchMessagesText[] = {
  "Ice Monitoring System Restarted",
  "Not detecting electrical power",
  "Electrical power outage",
  "Electrical power has turned on",
  "Temperature sensor okay",
  "Temperature sensor alarm"
};

enum WATCH_ENUM_ALARMS {
  WATCH_SYSTEM_STARTED,
  WATCH_NO_POWER_AT_START,
  WATCH_POWER_OUTAGE,
  WATCH_POWER_TURNED_ON,
  WATCH_SENSOR_OK,
  WATCH_SENSOR_ALARM,
};

const uint16_t IceWatchNumMessages = sizeof(IceWatchMessagesText) / sizeof(IceWatchMessagesText[0]);
uint16_t IceWatchMessageToSendIndex = WATCH_SYSTEM_STARTED;
bool needToSendIceWatchMessage = false;
bool confimSentIceWatchMessage = false;
int max_attempts_IceWatchMessage = 3;
int attempt_count_IceWatchMessage = 0;
int max_attempts_HologramAck = 12;
int attempt_count_HologramAck = 0;

void IceWatchAlarm(WATCH_ENUM_ALARMS msgnum)
{
  IceWatchMessageToSendIndex = msgnum;
  attempt_count_IceWatchMessage = 0;
  needToSendIceWatchMessage = true;
  confimSentIceWatchMessage = false;
}

void Display_init()
{
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  Display.cp437(true);
  Display.begin(0x3C, false);
  Display.setRotation(1);
  Display.setTextSize(1);
  Display.setCursor(0,0);
  Display.setTextColor(SH110X_WHITE);
  Display.println("hello");
  Display.display();
  display_refresh_timeref = millis();
}

//burn-in prevention
int burnin_counter = 0;
int16_t burnin_shiftX = 0;   
int16_t burnin_shiftY = 0;
void Display_burnin_shift() {
  if (++burnin_counter < 24000) {
    burnin_shiftX = 0;
    burnin_shiftY = 0;
  } else if (burnin_counter < 48000) {
    burnin_shiftX = 1;
    burnin_shiftY = 0;
  } else if (burnin_counter < 72000) {
    burnin_shiftX = 1;
    burnin_shiftY = 1;
  } else if (burnin_counter < 96000) {
    burnin_shiftX = 0;
    burnin_shiftY = 1;
  } else {
    burnin_counter = 0;
  }
}

void Display_SetTextColRow(int16_t x, int16_t y) {
  //because of the poorly named setCursor is actually in pixels.
  //we're more likely to deal with text mode column/row cell.
  //intended only for the default 6x8 font.
  Display.setTextWrap(false);
  Display.setCursor(6*x+burnin_shiftX,8*y+burnin_shiftY);
}

void Display_SetTextColRow_HalfStepDown(int16_t x, int16_t y) {
  //downshift halfway because aesthetic
  Display.setTextWrap(false);
  Display.setCursor(6*x + burnin_shiftX, 8*y + burnin_shiftY + 4);
}

void Display_SetTextColRow_ThreeQuarterDown(int16_t x, int16_t y) {
  //now we have total artistic flair
  Display.setTextWrap(false);
  Display.setCursor(6*x + burnin_shiftX, 8*y + burnin_shiftY + 6);
}

void Display_PrintTextOffOn(bool value)
{
  if (value) {
    Display.setTextColor(SH110X_BLACK, SH110X_WHITE);
    Display.print(" on");
    Display.setTextColor(SH110X_WHITE);
  } else {
    Display.print("off");
  }
  
}

void Display_PrintRegistrationStatusTxt( int value)
{
  switch(value) {
    case 0:
      Display.print("Unregistered");
    break;

    case 1:
      Display.print("Registered");
    break;

    case 2:
      Display.print("Searching");
    break;

    case 3:
      Display.print("Denied");
    break;

    case 4:
      Display.print("Unknown");
    break;

    case 5:
      Display.print("Registered");
    break;

    default:
      Display.print("Invalid");
  }
}

SARA_R4_Class SARA;

bool SARA_init_skipped;

void setup()
{
  pinMode(D10, INPUT_PULLUP);
  pinMode(D11, INPUT_PULLUP);
  pinMode(D12, INPUT_PULLUP);
  pinMode(D13, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.begin(115200); // Terminal
  
  delay(500);
  Display_init();   //OLED Display
  
  if (digitalRead(D13)) {
    TERM_P("Ice Watch MK2! trochotron@gmail.com");
    TERM_P("Waiting for modem to start.");
    SARA.modem_init();
    SARA_init_skipped = false;
    TERM_P("What you type can now passthru to the SARA modem.");
  } else {
    //You can pull D13 low to allow for powering up for a download without yanking the plug mid-negotiation with the cell tower.
    //this kind of bad activity can get the device banned if repeated often enough.
    //Ultimately this sidesteps a design flaw that resets the SARA modem every time the RP2040 is reset for download.
    SARA_init_skipped = true;
    TERM_P("SARA init was skipped because D13 was low");
  }
  
}

bool flag_powerDropped = false;

Finite_State_Machine Watch;
Finite_State_Machine Bkg;

int builtin_led_brightness = 25;    // how bright the LED is
int builtin_led_fadeAmount = 1;     // how many points to fade the LED by
int builtin_led_flashCounter = 0;   //

TON_ms TON_ButtonA;
TON_ms TON_ButtonB;
TON_ms TON_ButtonC;
TON_ms TON_ButtonC1;
TON_ms TON_ButtonC2;
TON_ms TON_ButtonC3;
TON_ms TON_ButtonC4;
TON_ms TON_ButtonC5;
TON_ms TON_D10;
TON_ms TON_D11;

//SARA VARS
char SARA_DateTimeString[] = "22/10/20,01:23:45";
int SARA_VAR_SIGNAL_POWER = 0;
int SARA_VAR_REGISTRATION_STATUS = 0;
int SARA_TCP_SOCKET = -1;
int SARA_TCP_CHARS_SENT = 0;
int SARA_TCP_READ_AVAILABLE = 0;

void loop()
{
  //TONs being used as input debounce filter.
  TON_ButtonA.IN(!digitalRead(BUTTON_A), 50);
  TON_ButtonB.IN(!digitalRead(BUTTON_B), 50);
  bool btn_C = !digitalRead(BUTTON_C);
  TON_ButtonC.IN(btn_C, 20);
  TON_ButtonC1.IN(btn_C, 100);
  TON_ButtonC2.IN(btn_C, 250);
  TON_ButtonC3.IN(btn_C, 500);
  TON_ButtonC4.IN(btn_C, 750);
  TON_ButtonC5.IN(btn_C, 1000);
  
  //Everything seems to be pulldown to GND = true nowadays.
  TON_D10.IN(!digitalRead(D10), 100);
  TON_D11.IN(!digitalRead(D11), 100);
  
  //This Watch state machine is looking at the digital inputs
  // and deciding what the appropriate message to send is.
  //Eventually the message gets sent in the Bkg (background) state machine.
  // 1. set IceWatchMessageToSendIndex to the enum that represents the message.
  // 2. set the flag needToSendIceWatchMessage.
  // 3. wait for the flag above to be cleared.
  switch(Watch.update()) {
    case 0:
      ++Watch.nextState;
    break;
    
    case 1:
      if (Watch.stateChanged) {
        IceWatchAlarm(WATCH_SYSTEM_STARTED);
      } else {
        if (!needToSendIceWatchMessage) {
          ++Watch.nextState;
        }
      }
    break;
    
    case 2:
      //have to jump into state that mirrors present condition.
      // we have no idea how long it has been in that condition.
      if (TON_D10.Q && TON_D11.Q) {
        //power detected and sensor ok
        Watch.nextState = 30;
      } else if (TON_D10.Q) {
        //just the power detected
        Watch.nextState = 20;
      } else {
        //assume power not ok
        Watch.nextState = 3;
      }
    break;
    
    case 3:
      //power not ok when first started.
      if (Watch.stateChanged) {
        IceWatchAlarm(WATCH_NO_POWER_AT_START);
      } else {
        if (!needToSendIceWatchMessage) {
          ++Watch.nextState;
        }
      }
    break;
    
    case 4:
      //idle wait until power ok
      if (TON_D10.Q) {
        Watch.nextState = 20;
      } else if (Watch.timeout >= 300000) {
        //give a sensor not ok after 5 mins
        ;
      }
    break;
    
    case 10:
      //power not ok, (sensor not ok)
      if (Watch.stateChanged) {
        //message that power is not ok
        IceWatchAlarm(WATCH_POWER_OUTAGE);
      } else {
        if (!needToSendIceWatchMessage) {
          ++Watch.nextState;
        }
      }
    break;
    
    case 11:
      //power not ok, (sensor not ok)
      if (TON_D10.Q && TON_D11.Q) {
        //both ok at the same time?
        Watch.nextState = 30;
      } else if (TON_D10.Q) {
        //enter the state that messages that the power came back on
        Watch.nextState = 20;
      }
    break;
    
    case 20:
      //power detected, sensor not ok
      if (Watch.stateChanged) {
        //message that power is ok
        IceWatchAlarm(WATCH_POWER_TURNED_ON);
      } else {
        if (!needToSendIceWatchMessage) {
          ++Watch.nextState;
        }
      }
    break;
    
    case 21:
      //power detected, sensor not ok
      if (TON_D10.Q && TON_D11.Q) {
        //both ok at the same time?
        Watch.nextState = 30;
      } else if (!TON_D10.Q) {
        //the power went off
        Watch.nextState = 10;
      }
    break;
    
    case 30:
      //power detected and sensor ok
      if (Watch.stateChanged) {
        //message that sensor ok
        IceWatchAlarm(WATCH_SENSOR_OK);
      } else {
        if (!needToSendIceWatchMessage) {
          ++Watch.nextState;
        }
      }
    break;
    
    case 31:
      //power detected and sensor ok
      if (!(TON_D10.Q && TON_D11.Q)) {
        //one or the other or both dropped.
        //it is not possible to instantly determine if a sensor "not ok" is the result of
        // a loss of power or because it dropped. this is because of capacitance.
        flag_powerDropped = !TON_D10.Q; //...except this one case is determinable.
        Watch.nextState = 40;
      }
    break;
    
    case 40:
      //we have to wait to see if power drops within a timeout period
      if (!TON_D10.Q) {
        flag_powerDropped = true;
      }
      if (Watch.timeout > 3000) {
        if (flag_powerDropped) {
          //message that power dropped
          Watch.nextState = 10;
        } else {
          //enter state that messages that sensor not ok
          Watch.nextState = 50;
        }
      }
    break;
    
    case 50:
      //message that sensor not ok
      if (Watch.stateChanged) {
        //message sensor not ok
        IceWatchAlarm(WATCH_SENSOR_ALARM);
      } else {
        if (!needToSendIceWatchMessage) {
          ++Watch.nextState;
        }
      }
    break;
    
    case 51:
      //oddball, but get back on the trail
      if (!TON_D10.Q) {
        Watch.nextState = 10;
      } else if (TON_D11.Q) {
        Watch.nextState = 30;
      }
    break;
    
    default:
      Watch.nextState = 0;
  }
  
  //OLED Display redraw
  // Application specific HMI style information.
  // User interface code.
  if ((millis() - display_refresh_timeref) > display_refresh_interval_ms) {
    display_refresh_timeref += display_refresh_interval_ms;
    if ((millis() - display_refresh_timeref) >= display_refresh_interval_ms) {
      //fallen way behind. not critical to count dropped refreshes, so just fix it.
      display_refresh_timeref = millis();
    }
    
    //user led (green)
    if (needToSendIceWatchMessage || confimSentIceWatchMessage) {
      //a harsh flash
      ++builtin_led_flashCounter;
      
      if (builtin_led_flashCounter >= (400 / display_refresh_interval_ms)) {
        builtin_led_flashCounter = 0;
      } else if (builtin_led_flashCounter >= (200 / display_refresh_interval_ms)) {
        analogWrite(LED_BUILTIN, 0);
      } else {
        analogWrite(LED_BUILTIN, 80);
      }
    } else {
      //a breath in and out type fade
      analogWrite(LED_BUILTIN, builtin_led_brightness);
      
      // change the brightness for next time through the loop:
      builtin_led_brightness = builtin_led_brightness + builtin_led_fadeAmount;
      
      // reverse the direction of the fading at the ends of the fade:
      if (builtin_led_brightness <= 4 || builtin_led_brightness >= 50) {
        builtin_led_fadeAmount = -builtin_led_fadeAmount;
      }
    }
    
    //offset the OLED screen for burn-in reasons.
    Display_burnin_shift();
    
    Display.clearDisplay();
    Display.setTextColor(SH110X_WHITE);
    Display_SetTextColRow(0,2);
    str_display_button_pressed[0] = (TON_ButtonA.Q) ? 'A' : ' ';
    str_display_button_pressed[1] = (TON_ButtonB.Q) ? 'B' : ' ';
    str_display_button_pressed[2] = (TON_ButtonC.Q) ? 'C' : ' ';
    Display.print(str_display_button_pressed);
    
    if (TON_ButtonC.Q) {
      Display.setTextColor(SH110X_BLACK, SH110X_WHITE);
      str_animate_button_C[0] = (TON_ButtonC1.Q) ? 'S' : ' ';
      str_animate_button_C[1] = (TON_ButtonC2.Q) ? 'T' : ' ';
      str_animate_button_C[2] = (TON_ButtonC3.Q) ? 'O' : ' ';
      str_animate_button_C[3] = (TON_ButtonC4.Q) ? 'P' : ' ';
      str_animate_button_C[4] = (TON_ButtonC5.Q) ? '!' : ' ';
      Display.print(str_animate_button_C);
    }

    Display.setTextColor(SH110X_WHITE);
    
    if (confimSentIceWatchMessage) {
      Display_SetTextColRow(8,2);
      Display.printf("Confirming %i", IceWatchMessageToSendIndex);
    } else if (needToSendIceWatchMessage) {
      Display_SetTextColRow(8,2);
      Display.printf("Alarm %i", IceWatchMessageToSendIndex);
    }
    
    Display_SetTextColRow_HalfStepDown(0,3);
    Display.printf("D10:    %s", D10_IDstr);
    Display_SetTextColRow_HalfStepDown(4,3);
    Display_PrintTextOffOn(TON_D10.Q);
    
    Display_SetTextColRow_HalfStepDown(0,4);
    Display.printf("D11:    %s", D11_IDstr);
    Display_SetTextColRow_HalfStepDown(4,4);
    Display_PrintTextOffOn(TON_D11.Q);
    
    //moved to below the rows showing input status
    Display_SetTextColRow_ThreeQuarterDown(5,5);
    Display.printf("%2i Watch State", Watch.nextState);
    
    Display_SetTextColRow(0,7);
    Display.print(SARA_DateTimeString);
    
    if (SARA_init_skipped) {
      Display_SetTextColRow(0,0);
      Display.print("SARA Init was skipped"); //btw this entirely fills the first line (21 chars wide).
      Display_SetTextColRow(0,1);
      Display.print("Do your download now.");
    } else {
      Display_SetTextColRow(0,0);
      Display.printf(" \x03%2i", SARA_VAR_SIGNAL_POWER);
      
      Display_SetTextColRow(6,0);
      Display_PrintRegistrationStatusTxt(SARA_VAR_REGISTRATION_STATUS);
      
      Display_SetTextColRow(0,1);
      if (SARA_VAR_SIGNAL_POWER != 0 && SARA_VAR_SIGNAL_POWER != 99 && (SARA_VAR_REGISTRATION_STATUS==1 || SARA_VAR_REGISTRATION_STATUS==5)) {
        Display.print("READY ");
      } else {
        Display.print("NO-GO ");
      }
      
      //milliseconds are too visually distracting. going with 10ths of a second.
      uint32_t disp_seconds = Bkg.timeout / 1000;
      uint32_t disp_tenths = (Bkg.timeout / 100) % 10;
      Display.printf("Step:%2i %3i.%1i S", Bkg.nextState, disp_seconds, disp_tenths);
      
    }
    
    Display.display();  //transfer from draw buffer to screen.
  }
  
  // yeah you'll forget this delay is here...
  delay(1);
  
  //This Background State Machine
  // We absolutely must not stall waiting for chars from modem so we use a state machine.
  // Bkg is the "background" loop where it sends various commands to SARA and then interprets the response.
  // The "response listener" after it is basically trying to fill "SARA.ResponseBuffer" and give a OK or ERROR.
  // Because each AT command gives a pretty unique response, it doesn't make a whole lot of sense to make them function calls.
  // So I just made it the generic AT_SET(), AT_READ() commands. Don't overthink it. AT_SET doesn't care about a response other than OK.
  // The modem manual exhaustively lists all the AT commands and the responses.
  // A response typically has multiple values, so the desired element must be extracted from a usually comma-separated-value (CSV) list.
  switch(Bkg.update()) {
    case 0:
      Bkg.nextState = 1;
    break;
    
    case 1:
      //Alive? (AT command alone)
      if (Bkg.stateChanged) {
        SARA.AT_SET("");
      } else {
        if (SARA.CMD_OK) {
          ++Bkg.nextState;
        } else if ( Bkg.timeout >= 5000) {
          Serial.println(" AT Timeout");
          SARA_VAR_REGISTRATION_STATUS = 0;
          SARA_VAR_SIGNAL_POWER = 99;
          Bkg.nextState = 0;
        }
      }
    break;
    
    case 2:
      //RTC
      if (Bkg.stateChanged) {
        SARA.AT_READ("+CCLK?");
      } else {
        if (SARA.CMD_OK) {
          int rv = extractSubstringFromCsvString(SARA_DateTimeString, sizeof(SARA_DateTimeString), 1, SARA.ResponseBuffer, sizeof(SARA.ResponseBuffer));
          if (rv) {
            Serial.printf(" SARA UTC = %s \n", SARA_DateTimeString);
          }
          ++Bkg.nextState;
        } else if ( Bkg.timeout >= 3000) {
          Serial.println(" Timeout");
          ++Bkg.nextState;
        }
      }
    break;
    
    case 3:
        //Do a simple delay to avoid hammering SARA.
        //If dip switch #3 is on (set low) then hang here indefinitely.
        //This is because I need to use m-center to issue serial commands uninterrupted.
        //It is impossible to a network scan which takes 5 minutes, for example.
        if ( digitalRead(D12) && (Bkg.timeout >= 9000) ) {
          ++Bkg.nextState;
        }
    break;
    
    case 4:
      //Carrier Registration
      // Status is the second number.
      if (Bkg.stateChanged) {
        SARA.AT_READ("+CEREG?");
      } else {
        if (SARA.CMD_OK) {
          
          SARA_VAR_REGISTRATION_STATUS = extractIntFromCsvString(2, SARA.ResponseBuffer, sizeof(SARA.ResponseBuffer));
          Serial.print(" Carrier Registration = ");
          switch(SARA_VAR_REGISTRATION_STATUS) {
            case 0:
              Serial.println("0 not registered (offline)");
            break;

            case 1:
              Serial.println("1 registered");
            break;

            case 2:
              Serial.println("2 searching");
            break;

            case 3:
              Serial.println("3 denied registration");
            break;

            case 4:
              Serial.println("4 unknown (out of U-ETRAN coverage)");
            break;

            case 5:
              Serial.println("5 registered, roaming");
            break;

            default:
              Serial.print("? status not read correctly: ");
              Serial.println(SARA_VAR_REGISTRATION_STATUS);
          }
          ++Bkg.nextState;
        } else if ( Bkg.timeout >= 3000) {
          ++Bkg.nextState;
        }
      }
    break;
    
    case 5:
      if ( Bkg.timeout >= 8000) {
        ++Bkg.nextState;
      }
    break;
    
    case 6:
      //Signal Strength Quality
      // RSSI is the first number
      if (Bkg.stateChanged) {
        SARA.AT_READ("+CSQ");
      } else {
        if (SARA.CMD_OK) {
          SARA_VAR_SIGNAL_POWER = extractIntFromCsvString(1, SARA.ResponseBuffer, sizeof(SARA.ResponseBuffer));
          Serial.printf(" SARA SIGNAL POWER (0..31) = %i \n", SARA_VAR_SIGNAL_POWER);
          ++Bkg.nextState;
        } else if ( Bkg.timeout >= 3000) {
          SARA_VAR_SIGNAL_POWER = 99;
          ++Bkg.nextState;
        }
      }
    break;
    
    case 7:
      //check for modem requirements for sending message
      if (needToSendIceWatchMessage) {
        
        if (attempt_count_IceWatchMessage <= max_attempts_IceWatchMessage) {
          //check for modem being in a state that can send messages
          if (SARA_VAR_SIGNAL_POWER != 0 && SARA_VAR_SIGNAL_POWER != 99 && (SARA_VAR_REGISTRATION_STATUS==1 || SARA_VAR_REGISTRATION_STATUS==5)) {
            Serial.printf(" Sending Hologram Message #%i \n", IceWatchMessageToSendIndex);
            ++attempt_count_IceWatchMessage;
            Bkg.nextState = 8;
          } else {
            Serial.printf(" Cannot Send Hologram Message #%i \n", IceWatchMessageToSendIndex);
            Bkg.nextState = 1;
          }
          
        } else {
          Serial.printf(" Giving Up Sending Hologram Message #%i \n", IceWatchMessageToSendIndex);
          needToSendIceWatchMessage = false;
          Bkg.nextState = 1;
        }
      } else {
        Bkg.nextState = 1;
      }
    break;
    
    case 8:
      //set base-16 mode
      if (Bkg.stateChanged) {
        SARA.AT_SET("+UDCONF=1,1");
      } else {
        if (SARA.CMD_OK) {
          ++Bkg.nextState;
        } else if ( Bkg.timeout >= 3000) {
          Serial.println(" Timeout");
          Bkg.nextState = 1;
        }
      }
    break;
    
    case 9:
      //reserve a TCP socket number
      if (Bkg.stateChanged) {
        SARA.AT_READ("+USOCR=6");
      } else {
        if (SARA.CMD_OK) {
          SARA_TCP_SOCKET = extractIntFromCsvString(1, SARA.ResponseBuffer, sizeof(SARA.ResponseBuffer));
          Serial.printf(" Reserved SARA TCP Socket = %i \n", SARA_TCP_SOCKET);
          ++Bkg.nextState;
        } else if (SARA.CMD_ERROR) {
          Serial.println(" Failed to reserve SARA TCP Socket number!");
          Bkg.nextState = 1;
        } else if ( Bkg.timeout >= 3000) {
          Serial.println(" Timeout");
          Bkg.nextState = 1;
        }
      }
    break;
    
    case 10:
      //open TCP socket
      if (Bkg.stateChanged) {
        char punt10[] = "+USOCO=0,\"cloudsocket.hologram.io\",9999";
        punt10[7] = '0' + SARA_TCP_SOCKET;
        SARA.AT_SET(punt10);
      } else {
        if (SARA.CMD_OK) {
          Serial.println(" Opened Hologram cloudsocket!");
          ++Bkg.nextState;
        } else if (SARA.CMD_ERROR) {
          Serial.println(" Error on open Hologram cloudsocket!");
          Bkg.nextState = 15;    //LOOK you have to give the socket number back to SARA.
        } else if ( Bkg.timeout >= 20000) {
          Serial.println(" Timeout");
          Bkg.nextState = 15;    //LOOK you have to give the socket number back to SARA.
        }
      }
    break;
    
    case 11:
      //just give some rest for the IRC or URC stuff to come in.
      if ( Bkg.timeout >= 1000) {
        ++Bkg.nextState;
      }
    break;
    
    case 12:
      //Send a very particularly crafted packet to Hologram service.
      if (Bkg.stateChanged) {
        if (IceWatchMessageToSendIndex > 0) {
          SARA.HOLOGRAM(SARA_TCP_SOCKET, IceWatchMessagesText[IceWatchMessageToSendIndex], "user");
        } else {
          //the fear, is that the powerup message will flood users on a very dead Lipo battery.
          SARA.HOLOGRAM(SARA_TCP_SOCKET, IceWatchMessagesText[IceWatchMessageToSendIndex], "system");
        }
        attempt_count_HologramAck = 0;
      } else {
        if (SARA.CMD_OK) {
          SARA_TCP_CHARS_SENT = extractIntFromCsvString(2, SARA.ResponseBuffer, sizeof(SARA.ResponseBuffer));
          confimSentIceWatchMessage = true;
          Serial.printf(" Sent %i chars to Hologram.\n", SARA_TCP_CHARS_SENT);
          attempt_count_HologramAck = 1;
          ++Bkg.nextState;
        } else if (SARA.CMD_ERROR) {
          Serial.println(" Error, Hologram send did not work");
          ++Bkg.nextState;
        } else if ( Bkg.timeout >= 60000) {
          ++Bkg.nextState;
          Serial.println(" Timeout");
        }
      }
    break;
    
    case 13:
      //just give some rest for the IRC or URC stuff to come in.
      if ( Bkg.timeout >= 10000) {
        ++Bkg.nextState;
      }
    break;
    
    case 14:
      //read the number of bytes available in the TCP receive buffer to make SARA happy.
      //This also makes Hologram happy, I guess.
      //But it makes me unhappy to have to write this.
      if (Bkg.stateChanged) {
        char punt14[] = "+USORD=0,0";
        punt14[7] = '0' + SARA_TCP_SOCKET;   //for example socket 2: +USORD=2,0"
        SARA.AT_READ(punt14);
      } else {
        if (SARA.CMD_OK) {
          SARA_TCP_READ_AVAILABLE = extractIntFromCsvString(2, SARA.ResponseBuffer, sizeof(SARA.ResponseBuffer));
          Serial.printf(" Try %i, Number of Bytes in TCP Receive Buffer = %i \n", attempt_count_HologramAck, SARA_TCP_READ_AVAILABLE);
          
          //We must wait for Hologram's response.
          //Hologram does not give any indication of success or failure so it is not critical to compare.
          //Their reasoning is because device keys could be inferred by brute force if "failure" or "success" was given.
          if (SARA_TCP_READ_AVAILABLE) {
            needToSendIceWatchMessage = false;
            ++Bkg.nextState;
          } else {
            if (attempt_count_HologramAck >= max_attempts_HologramAck) {
              Serial.println(" Exceeded number of tries waiting for acknowledge from Hologram service.");
              ++Bkg.nextState;
            } else {
              ++attempt_count_HologramAck;
              --Bkg.nextState;
            }
          }
          
        } else if (SARA.CMD_ERROR) {
          Serial.println(" Cannot read number of chars waiting in TCP Receive Buffer. Socket probably closed.");
          ++Bkg.nextState;
        } else if ( Bkg.timeout >= 60000) {
          ++Bkg.nextState;
          Serial.println(" Timeout");
        }
      }
    break;
    
    case 15:
      //close the socket.
      if (Bkg.stateChanged) {
        char punt15[] = "+USOCL=0";
        punt15[7] = '0' + SARA_TCP_SOCKET;
        SARA.AT_SET(punt15);
        confimSentIceWatchMessage = false;    //subtle. if did not send the message, we still left that flag set to retry.
      } else {
        if (SARA.CMD_OK) {
          Serial.printf(" Closed SARA TCP Socket = %i \n", SARA_TCP_SOCKET);
          Bkg.nextState = 1;
        } else if (SARA.CMD_ERROR) {
          //just know europeans wrote this firmware, so closing a closed socket is an error.
          Serial.printf(" Error closing SARA TCP Socket = %i \n", SARA_TCP_SOCKET);
          Bkg.nextState = 1;
        } else if ( Bkg.timeout >= 10000) {
          Serial.println(" Timeout");
          Bkg.nextState = 1;
        }
      }
    break;
    
    default:
    Bkg.nextState = 0;
  }
  
  SARA.ResponseListener(TON_ButtonC5.Q);
}
