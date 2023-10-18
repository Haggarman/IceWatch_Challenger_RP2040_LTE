#pragma once

#define UART_DEBUG

#ifdef UART_DEBUG
  #define TERM_P(str) Serial.println(str)
  #define TERM_ECHO(str) Serial.write(str)
#else
  #define TERM_P(str)
  #define TERM_ECHO(str)
#endif
