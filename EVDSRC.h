#ifndef EVDSRC_H
#define EVDSRC_H

#include <Arduino.h>
#include <HardwareSerial.h>

extern int pinSrcUartTx;
extern int pinSrcUartRx;
extern String receiverSource;
extern String receiverState;

static HardwareSerial receiverUart(2);

inline void setupReceiverLink() {
  receiverUart.begin(115200, SERIAL_8N1, pinSrcUartRx, pinSrcUartTx);
}

inline void sendReceiverSourceCommand(const String &source) {
  if (receiverUart) {
    receiverUart.printf("SRC:%s\n", source.c_str());
  }
}

inline void sendReceiverCommand(const String &cmd) {
  if (receiverUart) {
    receiverUart.printf("CMD:%s\n", cmd.c_str());
  }
}

inline void handleReceiverLink() {
  while (receiverUart.available()) {
    String line = receiverUart.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      // Simple response handling - can be extended if needed
    }
  }
}

#endif