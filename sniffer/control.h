// Heater control: the model of the heaters, the worker task that owns the radio, the event log, command intake.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "settings.h"
#include "lang.h"

#define FW_VERSION 25 // bump on every release so the log and the panel show which firmware runs

extern SemaphoreHandle_t radioMux;  // the CC1101 is shared between the serial console and the worker task

void controlSetup(const Settings& s);
void controlStatus();                    // serial console: "status"
void controlDumpLog();                   // serial console: "log"
// Waits for the remote of the heater (the user presses its buttons) and reports its address. false when nothing
// was heard often enough; heard gets the number of frames of the best candidate.
bool controlFindRemote(int seconds, HeaterId& out, int* heard);
bool controlIdle();                      // no job running or waiting: a good moment to restart for an update
void controlSetOtaBusy(bool busy);       // an OTA update is running: leave the radio alone
void controlSetVerbose(bool on);         // also log every raw frame (serial console: "verbose 1")
void controlAnomDump();                  // serial console: "anom", the odd frames kept in flash
void controlAnomClear();                 // serial console: "anom clear"

// ---- event log (ring buffer) ----
void wlog(const char* fmt, ...);
// appends the lines with an id above afterId as {"m":"..."},{"m":"..."} to out (at most maxLines);
// returns the id of the last one included, or afterId when there was nothing new
uint32_t logCollect(uint32_t afterId, String& out, int maxLines);

// ---- for the cloud client ----
// JSON members of the state: "env":{...}|null,"heaters":[...]  (no braces around, the caller wraps them)
String controlStateJson();
// Queues a command of the server ("on" "off" "level" "temp" "mode" "smart" "sniff"). The checks are the same as on the
// panel side (the heater state may have changed since). false + error text when it is refused right away;
// otherwise the result arrives later through controlPopAck with the same cmdId.
bool controlSubmit(int heater, const String& action, int value, int cmdId, String& error);
struct Ack {
  int id;
  bool ok;
  char msg[100];  // a text of the language of the build: German and Czech ones are the longest
};
bool controlPopAck(Ack& a);  // a finished command, true if there was one

// JSON string content: escapes and replaces broken UTF-8 (the server refuses a body with invalid UTF-8)
void jsonEscape(String& out, const char* s);

// ---- radio layer, implemented in sniffer.ino ----
extern uint8_t v1Addr[3];
extern uint8_t v1Hdr;
extern bool v1Active;
void configureHeaterV1();
void heaterTxV1(uint8_t cmd, uint8_t sub, uint8_t d1, uint8_t d2, bool verbose);
int v1ReadReply(uint32_t ms, uint8_t* b);
bool v1RawFrame(uint8_t* b, uint32_t ms);
