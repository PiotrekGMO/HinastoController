// Host-side tests of the hardware free parts of the firmware: smart mode (sniffer/smart.h) and utf8Copy
// (sniffer/textutil.h). Run: g++ -std=c++17 -O2 tools/smart-test.cpp -o smart-test && ./smart-test
// Exit code 0 when everything holds. Every test names the failure it guards against.
#include <cstdio>
#include <cstring>
#include "../sniffer/smart.h"
#include "../sniffer/textutil.h"

static int failures = 0;
static bool is(const char* a, const char* b) { return a && !strcmp(a, b); }
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

// a smart instance in a room of a fixed temperature, stepped every 5 s
struct Rig {
  Smart s;
  uint32_t t = 100000;  // an arbitrary boot time in seconds
  SmartIn in{};
  Rig(float target) {
    s.enable(target, t);
    in.sensorOk = true; in.temp = 20; in.run = SR_OFF; in.manual = false; in.level = 1; in.busy = false; in.now = t;
  }
  SmartOut step(uint32_t seconds = 5) { t += seconds; in.now = t; return s.step(in); }
  // steps until the wanted action appears (any action when want is SA_NONE), at most `max` seconds; level changes in between are ignored
  SmartOut untilAct(SmartAct want, uint32_t max) {
    for (uint32_t i = 0; i < max; i += 5) { SmartOut o = step(); if (o.act == want) return o; }
    return SmartOut();
  }
  // steps until an action other than NONE appears, at most `max` seconds
  SmartOut until(uint32_t max) {
    for (uint32_t i = 0; i < max; i += 5) { SmartOut o = step(); if (o.act != SA_NONE) return o; }
    return SmartOut();
  }
};

static void testSingleColdSampleDoesNotStart() {
  Rig r(20);
  r.in.temp = 20; for (int i = 0; i < 6; i++) r.step(30);        // warm samples
  r.in.temp = 5; SmartOut o = r.step(30);                         // one corrupted cold reading
  CHECK(o.act != SA_START, "a single cold sample must not start the heater");
  r.in.temp = 20; o = r.until(300);
  CHECK(o.act != SA_START, "back to normal: no start");
}

static void testColdRoomStarts() {
  Rig r(20);
  r.in.temp = 15;
  SmartOut o = r.until(600);
  CHECK(o.act == SA_START, "a cold room (3 samples in a row) starts the heater");
}

static void testStuckSensorSwitchesOff() {
  Rig r(20);
  r.in.run = SR_ON; r.in.manual = true; r.in.level = 3; r.in.temp = 15;  // heating, reading frozen at 15.0
  SmartOut o = r.untilAct(SA_OFF, smartcfg::STUCK_S + 400);
  CHECK(o.act == SA_OFF && is(o.why, L_SM_STUCK), "a reading that never moves for an hour while heating switches the heater off");
}

static void testStuckSensorBlocksStart() {
  Rig r(20);
  r.in.temp = 10;                                                  // cold from the start, frozen
  CHECK(r.untilAct(SA_START, 600).act == SA_START, "a cold sensor starts the heater at first");
  r.in.run = SR_OFF;
  r.t += smartcfg::MIN_OFF_S + smartcfg::START_RETRY_S + smartcfg::STUCK_S;  // a long time later: the reading never moved
  r.s.restore(false, 0);
  CHECK(r.untilAct(SA_START, 900).act != SA_START, "a sensor frozen for an hour must not start the heater");
  r.in.temp = 9.5f;                                                // it moves again: alive
  CHECK(r.untilAct(SA_START, 900).act == SA_START, "a sensor that moves again is trusted again");
}
static void testSensorLostSwitchesOff() {
  Rig r(20);
  r.in.run = SR_ON; r.in.manual = true; r.in.level = 2; r.in.temp = 15;
  for (int i = 0; i < 20; i++) { r.in.temp = 15 + 0.1f * (i % 3); r.step(30); }
  r.in.sensorOk = false;
  SmartOut o = r.until(smartcfg::SENSOR_LOST_S + 400);
  CHECK(o.act == SA_OFF && is(o.why, L_SM_NO_SENSOR), "no reading for 10 minutes while heating switches the heater off");
}

static void testUnknownHeaterGetsOffWhenTooWarm() {
  Rig r(20);
  r.in.run = SR_ON; r.in.manual = true; r.in.level = 2; r.in.temp = 19;
  for (int i = 0; i < 100; i++) { r.in.temp = 19 + 0.1f * (i % 3); r.step(30); }
  CHECK(r.s.wasOn(), "smart knows it runs the heater");
  r.in.run = SR_UNKNOWN; r.in.temp = 23;                           // not heard any more, room clearly too warm
  SmartOut o = r.until(smartcfg::UNKNOWN_OFF_S + 200);
  CHECK(o.act == SA_OFF, "a heater that is not heard while the room is too warm still gets a power off");
  Rig q(20);
  q.in.run = SR_UNKNOWN; q.in.temp = 23;
  CHECK(q.until(1000).act == SA_NONE, "nothing is sent when smart does not believe the heater runs");
}

static void testNoManualStopsThenDisables() {
  Rig r(20);
  r.in.run = SR_ON; r.in.manual = false; r.in.temp = 15;
  SmartOut o; int offs = 0;
  for (uint32_t i = 0; i < 4000 && r.s.enabled; i += 5) {
    o = r.step();
    if (o.act == SA_OFF) offs++;
    if (offs && o.act == SA_OFF && r.in.run == SR_ON && offs == 1) { /* the first off is asked for */ }
    if (offs >= 2) r.in.run = SR_COOLING;                          // the heater finally obeys
    if (o.act == SA_DISABLE) break;
  }
  CHECK(offs >= 1, "when smart cannot take manual control it stops the heater it started");
  CHECK(!r.s.enabled, "and switches itself off afterwards");
}

static void testRestoredBeliefDisablesOnForeignStop() {
  Rig r(20);
  r.s.restore(true, 1);                                            // smart was running the heater before the restart
  r.in.run = SR_OFF; r.in.temp = 15;
  SmartOut o = r.until(100);
  CHECK(o.act == SA_DISABLE && is(o.why, L_SM_OFF_FOREIGN), "a heater found off after a restart ends smart mode instead of being started again");
  Rig q(20);
  q.s.restore(false, 0);
  q.in.run = SR_OFF; q.in.temp = 15;
  CHECK(q.until(600).act == SA_START, "when smart believed the heater to be off, a cold room still starts it");
  Rig w(20);
  w.s.restore(false, smartcfg::START_TRIES);
  w.in.run = SR_OFF; w.in.temp = 15;
  CHECK(w.until(600).act == SA_DISABLE, "the number of failed starts survives a restart");
}

static void testCoolingAfterOwnOffClearsBelief() {
  Rig r(20);
  r.in.run = SR_ON; r.in.manual = true; r.in.level = 1; r.in.temp = 24;                // far above the target
  SmartOut o = r.until(2000);
  CHECK(o.act == SA_OFF, "a clearly too warm room switches the heater off");
  r.in.run = SR_COOLING; r.step();
  CHECK(!r.s.wasOn(), "once the heater cools down after smart's own off, smart no longer believes it runs");
}

static void testUtf8Copy() {
  char b[8];
  utf8Copy(b, "abc", sizeof b);           CHECK(!strcmp(b, "abc"), "short text unchanged");
  utf8Copy(b, "wy\xC5\x82\xC4\x85" "cz", sizeof b);  // "wyłącz" = 6 chars, 8 bytes: needs 9 with the terminator
  CHECK(!strcmp(b, "wy\xC5\x82\xC4\x85" "c"), "cut at the end of a complete character");
  utf8Copy(b, "abcdef\xC5\x82", sizeof b);  // the 2-byte character would start at byte 7 and be cut in half
  CHECK(!strcmp(b, "abcdef"), "a character cut in half is dropped whole");
  char c[9];
  utf8Copy(c, "topidlo neodpov\xC3\xAD" "d\xC3\xA1", sizeof c);
  CHECK(strlen(c) <= 8 && !(((uint8_t)c[strlen(c) - 1]) & 0x80 && ((uint8_t)c[strlen(c) - 1] >= 0xC0)), "never ends with a lone lead byte");
  char d[32];
  utf8Copy(d, "neodpov\xC3\xAD" "d\xC3\xA1", sizeof d);
  CHECK(!strcmp(d, "neodpov\xC3\xAD" "d\xC3\xA1"), "a text that fits keeps its last character");
}

int main() {
  testSingleColdSampleDoesNotStart();
  testColdRoomStarts();
  testStuckSensorSwitchesOff();
  testStuckSensorBlocksStart();
  testSensorLostSwitchesOff();
  testUnknownHeaterGetsOffWhenTooWarm();
  testNoManualStopsThenDisables();
  testRestoredBeliefDisablesOnForeignStop();
  testCoolingAfterOwnOffClearsBelief();
  testUtf8Copy();
  printf(failures ? "%d test(s) FAILED\n" : "all tests passed\n", failures);
  return failures ? 1 : 0;
}
