#ifndef EVDSRC_H
#define EVDSRC_H

#include <Arduino.h>
#include <HardwareSerial.h>

extern int pinSrcUartTx;
extern int pinSrcUartRx;
extern String receiverSource;
extern String receiverState;
extern uint32_t receiverSampleRate;
extern uint8_t receiverChannels;
extern uint8_t receiverBits;
extern uint32_t receiverBytes;
extern uint32_t receiverFrames;
extern uint32_t receiverErrors;

static HardwareSerial receiverUart(2);
static String receiverUartLine;
static unsigned long lastReceiverStatusRequestMs = 0;

inline void setupReceiverLink() {
  receiverUart.begin(115200, SERIAL_8N1, pinSrcUartRx, pinSrcUartTx);
  Serial.printf("[INFO ] Receiver UART TX=%d RX=%d\n", pinSrcUartTx, pinSrcUartRx);
}

inline void requestReceiverStatus() {
  receiverUart.print("STATUS\n");
}

inline void sendReceiverSourceCommand(const String &source) {
  receiverUart.print("SET_SOURCE ");
  receiverUart.print(source);
  receiverUart.print("\n");
}

inline void parseReceiverStatus(const String &line) {
  if (!line.startsWith("STATUS|")) return;

  int start = 7;
  while (start < line.length()) {
    int end = line.indexOf('|', start);
    if (end < 0) end = line.length();
    int equals = line.indexOf('=', start);
    if (equals > start && equals < end) {
      String key = line.substring(start, equals);
      String value = line.substring(equals + 1, end);
      if (key == "source") receiverSource = value;
      else if (key == "state") receiverState = value;
      else if (key == "rate") receiverSampleRate = value.toInt();
      else if (key == "channels") receiverChannels = value.toInt();
      else if (key == "bits") receiverBits = value.toInt();
      else if (key == "bytes") receiverBytes = value.toInt();
      else if (key == "frames") receiverFrames = value.toInt();
      else if (key == "errors") receiverErrors = value.toInt();
    }
    start = end + 1;
  }
}

inline void handleReceiverLink() {
  while (receiverUart.available()) {
    char c = receiverUart.read();
    if (c == '\n') {
      receiverUartLine.trim();
      parseReceiverStatus(receiverUartLine);
      receiverUartLine = "";
    } else if (c != '\r' && receiverUartLine.length() < 255) {
      receiverUartLine += c;
    }
  }

  if (millis() - lastReceiverStatusRequestMs >= 5000) {
    lastReceiverStatusRequestMs = millis();
    requestReceiverStatus();
  }
}

#endif
