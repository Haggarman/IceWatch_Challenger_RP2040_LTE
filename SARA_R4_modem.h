#include "ChallengerLTE.h"
#include "SerialDebug.h"
#include "StringStuff.h"
#include "Arduino_secrets.h"

#define RESPONSE_BUFFER_SIZE (500)

class SARA_R4_Class
{
  public:
    int RawReceiveBufferIndex = 0;
    char RawReceiveBuffer[RESPONSE_BUFFER_SIZE];
    char ResponseBuffer[RESPONSE_BUFFER_SIZE];
    char ExpectedResponsePrefixBuffer[RESPONSE_BUFFER_SIZE];
    
    int CMD_STATE = 0;
    int CMD_ID = 0;
    bool CMD_OK = 0;
    bool CMD_ERROR = 0;
    
    
    void AT_SET(const char command[150])
    {
      //set typically only returns OK
      CMD_STATE = 1;
      ++CMD_ID;
      SARA_SERIAL_PORT.printf("AT%s\r",command);
      CMD_OK = 0;
      CMD_ERROR = 0;
      ExpectedResponsePrefixBuffer[0] = 0;
    }
    
    
    void AT_READ(const char command[150])
    {
      //read means you want some kind of response back
      //often but not always the command has a ? at the end of it.
      CMD_STATE = 1;
      ++CMD_ID;
      SARA_SERIAL_PORT.printf("AT%s\r",command);
      CMD_OK = 0;
      CMD_ERROR = 0;
      
      if (command[0] == '+') {
        ExpectedResponsePrefixBuffer[0] = '+';
        
        size_t x = 1;
        char c = 0;
        do {
          c = command[x];
          if (isalpha(c)) {
            ExpectedResponsePrefixBuffer[x] = c;
          } else {
            ExpectedResponsePrefixBuffer[x] = 0;
            break; //do-while
          }
          ++x;
        } while (c != 0);
      } else {
        ExpectedResponsePrefixBuffer[0] = 0;
      }
    }
    
    
    void HOLOGRAM(int TCPsocket, const char message[150], const char topics[150])
    {
      //requires very specific JSON formatting for unique key, message data, and topic.
      // //refer to HOLOGRAM docs for how to bracket multiple topics, if needed.
      CMD_STATE = 1;
      ++CMD_ID;
      CMD_OK = 0;
      CMD_ERROR = 0;
      strcpy(ExpectedResponsePrefixBuffer, "+USOWR");  //includes the null char
      
      size_t asciilen = 0;
      char ascii_str[300];
      char hex_str[300];
      char key[] = SECRET_HOLOGRAM_DEVICE_KEY;
      
      snprintf(ascii_str, 300, "{\"k\":\"%s\",\"d\":\"%s\",\"t\":\"%s\"}", key, message, topics);
      //Serial.printf("%s\n",ascii_str);
      //A terminating null character is automatically appended after the content written by snprintf
      
      asciilen = base16encode(hex_str, sizeof(hex_str), ascii_str, sizeof(ascii_str));
      
      SARA_SERIAL_PORT.printf("AT+USOWR=%1i,%zu,\"%s\"\r", TCPsocket, asciilen, hex_str);
      //Serial.printf("AT+USOWR=%1i,%zu,\"%s\"\r\n", TCPsocket, asciilen, hex_str);
    }
    
    
    // SARA Response/Result Listener
    uint32_t WHITESPACE_CMDSTART = 0;   //for curiosity sake
    uint32_t RESPONSE_OVERFLOWS = 0;
    uint32_t AT_COMMAND_ECHOES = 0;
    int listenerState = 0;
    
    void ResponseListener(bool modem_quit)
    {
      int TERM_byte;  //it is not lost on me that an int is being used to store a byte.
      int SARA_byte;
      int SARA_delta;
      
      switch(listenerState) {
        case 0:
          //not actively dealing with a command.
          if (CMD_STATE == 1) {
            //command is being sent to SARA
            memset(ResponseBuffer, 0, sizeof(ResponseBuffer));
            listenerState = 1;
          } else if (SARA_SERIAL_PORT.available() > 0) {
            //SARA is saying something but we didn't ask.
            //just echo to terminal.
            while ( (SARA_delta=SARA_SERIAL_PORT.available()) > 0) {
              SARA_byte = SARA_SERIAL_PORT.read();
              TERM_ECHO(SARA_byte);
            }
          } else if (Serial.available() > 0) {
            //need to kick over to terminal send.
            listenerState = 10;
          } else if (modem_quit) {
            TERM_P("Quitting because Button C was pressed\n");
            SARA_SERIAL_PORT.printf("AT+CPWROFF\r");
          }
        break;
        
        case 1:
          //(re)initialize raw receive buffer.
          //we are expecting an OK or an ERROR.
          //but we can still get other complications like URC or IRC codes from SARA.
          WHITESPACE_CMDSTART = 0;
          
          RawReceiveBufferIndex = 0;
          memset(RawReceiveBuffer, 0, sizeof(RawReceiveBuffer));
          listenerState = 2;
        //no break; FALL THRU INTENTIONAL
        
        case 2:
          //first thing is get rid of the beginning control codes and spaces and actually get an alphanumeric code
          //but the timing of all of this is asynchronous so we could get nothing for a while.
          
          while ( (SARA_delta=SARA_SERIAL_PORT.available()) > 0) {
            SARA_byte = SARA_SERIAL_PORT.read();
            //TERM_ECHO(SARA_byte);
            
            if (SARA_byte > 32) {
              RawReceiveBuffer[RawReceiveBufferIndex++] = SARA_byte;
              listenerState = 3;
              break; //while
            } else {
              ++WHITESPACE_CMDSTART;
            }
          }
        break;
        
        case 3:
          while ( (SARA_delta=SARA_SERIAL_PORT.available()) > 0) {
            SARA_byte = SARA_SERIAL_PORT.read();
            //TERM_ECHO(SARA_byte);
            
            if (SARA_byte != 13) {
              if (SARA_byte == 10) {
                //read of one line is complete.
                RawReceiveBuffer[RawReceiveBufferIndex] = 0;
                listenerState = 4;
                break; //while
              } else {
                RawReceiveBuffer[RawReceiveBufferIndex++] = SARA_byte;
                if (RawReceiveBufferIndex >= RESPONSE_BUFFER_SIZE) {
                  RawReceiveBufferIndex = RESPONSE_BUFFER_SIZE-1;
                  RawReceiveBuffer[RawReceiveBufferIndex] = 0;
                  listenerState = 9;
                  break; //while
                }
              }
            }
          }
        break;
        
        case 4:
          if (doesCsvStringBeginWith("OK", RawReceiveBuffer, sizeof(RawReceiveBuffer))) {
            //(strstr(RawReceiveBuffer, "OK") != NULL)
            //check for OK
            CMD_OK = 1;
            CMD_STATE = 2;
            listenerState = 0;
          } else if (strstr(RawReceiveBuffer, "ERROR") != NULL) {
            //check for ERROR anywhere in the response.
            //this is about 99% solved. this could fail on really bizarre edge cases.
            CMD_ERROR = 1;
            CMD_STATE = 3;
            listenerState = 0;
            TERM_P(RawReceiveBuffer);
          } else if (RawReceiveBuffer[0] == '+') {
            //check for starting with +, which is a response to a question, URC or IRC.
    
            //is this the response we were expecting?
            if (doesCsvStringBeginWith(ExpectedResponsePrefixBuffer, RawReceiveBuffer, sizeof(RawReceiveBuffer))) {
              size_t x = 0;
              do {
                ResponseBuffer[x] = RawReceiveBuffer[x];
              } while (ResponseBuffer[x++] != 0);
              //TERM_P("Copied Expected Response from AT Read Command");
            } else {
              TERM_P("Unexpected Response to AT Read Command: ");
              TERM_P(RawReceiveBuffer);
              //TODO: probably need to process this
            }
            listenerState = 1;
          } else if (doesCsvStringBeginWith("AT", RawReceiveBuffer, sizeof(RawReceiveBuffer))) {
            //old (strstr(RawReceiveBuffer, "AT") != NULL)
            //check for starting with AT, meaning command is being echoed back. Do nothing with it. go back for another line.
            ++AT_COMMAND_ECHOES;
            listenerState = 1;
          } else {
            //LOL I dunno
            //ATI does whatever but ends with OK
            listenerState = 1;
          }
        break;
        
        case 9:
          ++RESPONSE_OVERFLOWS;
          CMD_ERROR = 1;
          CMD_STATE = 4;
          listenerState = 0;
        break;
        
        case 10:
          TERM_byte = -1;
          while (Serial.available() > 0) {
            TERM_byte = Serial.read();
            SARA_SERIAL_PORT.write(TERM_byte);
            //Serial.print(TERM_byte, HEX);
            //Serial.print(" ");
            
            //a carriage return marks the end of the message from the terminal
            if (TERM_byte == 13) {
              listenerState = 0;
              break;
            }
          }
        break;
      }
    }


    #define SYSTEM_MNO_PROF 100
    #define DEFAULT_PS_ENABLED 0
    void modem_init()
    {
      // Power on modem
      SARA_SERIAL_PORT.setFIFOSize(200);
      if (!Challenger2040LTE.doPowerOn()) {
        TERM_P("Failed to start the modem properly !");
        while(1);
      } else {
        TERM_P("Modem started !");
        delay(1000);
    
        // Read current modem MNO profile
        int mnoprof = Challenger2040LTE.getMNOProfile();
        if (mnoprof < 0) {
          TERM_P("Failed setting reading the MNO profile !");
          while(1);
        }
        // if current modem MNO profile differs from system default change it
        if (mnoprof != SYSTEM_MNO_PROF) {
          TERM_P("Current modem MNO profile differs from system default !");
          if(Challenger2040LTE.setMNOProfile(100)) {
            TERM_P("Suceeded setting new MNO profile !");
          } else {
            TERM_P("Failed setting new MNO profile !");
          }
        }
      }
    }
};
