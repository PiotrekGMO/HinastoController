// "Smart" mode: the controller keeps the room at a target temperature by itself, using the DHT22, and drives the
// heater through its manual levels H1..H6. No hardware in here, so the same code runs in the simulation on a PC
// (see tools/smart-sim.cpp).
//
// How it behaves:
//  * heater off: when the room is START_DROP below the target it starts the heater (and not sooner than MIN_OFF_S
//    after the last stop: a cold start costs fuel and wears the glow plug);
//  * heater on: it switches to manual mode and sets a first level from the size of the gap, then every few minutes
//    compares the measured rate of rise with the wanted one (gap / HORIZON_MIN) and moves one level up or down;
//  * at the target the wanted rate is zero: the level comes down, and when even H1 still lifts the temperature the
//    room is warm enough and the heater is switched off (it starts again when the room has cooled by START_DROP);
//  * safety: a sensor that went silent or is stuck switches the heater off, a heater that was switched off by someone
//    else (the remote, the panel) switches smart off, a heater that is not heard while the sensor is gone or the room is
//    too warm gets a power off anyway; a restart remembers what smart believed about the heater (see tools/smart-test.cpp).
#pragma once
#include <stdint.h>
#include <math.h>
#include "lang.h"

namespace smartcfg {
constexpr float START_DROP = 3.0f;        // degrees below the target that start a cold heater
constexpr float OVERSHOOT = 1.0f;         // degrees above the target: step down at once, off at H1
constexpr uint32_t SAMPLE_S = 30;         // temperature sample for the slope
constexpr uint32_t STARTUP_S = 240;       // after the heater reports "on": it is igniting, no commands yet
constexpr uint32_t SETTLE_S = 180;        // after a level change the room does not react yet
constexpr uint32_t WINDOW_MIN_S = 480;    // data needed after the settle time to trust a slope while heating up (big wanted rate)
constexpr uint32_t WINDOW_HOLD_S = 840;   // the same near the target: the wanted rate is ~0, so the sensor noise needs a longer window
constexpr uint32_t WINDOW_MAX_S = 1200;   // and the oldest data used
constexpr uint32_t ACT_GAP_S = 90;        // never two commands closer than this
constexpr float HORIZON_MIN = 40.0f;      // wanted time to close the gap to the target
constexpr float RISE_MAX = 0.12f;         // degrees/min: no point in asking for more
constexpr float BAND = 0.012f;            // degrees/min of tolerance around the wanted rate (one level changes the rate by about 0.01)
constexpr float RISE_OFF = 0.005f;        // degrees/min: still rising at H1 above the target -> off
constexpr uint32_t MIN_OFF_S = 600;       // pause between a stop and the next start
constexpr uint32_t SENSOR_LOST_S = 600;   // no reading this long while heating -> off
constexpr uint32_t START_RETRY_S = 600;   // a start that did not take is repeated after this
constexpr int START_TRIES = 3;
constexpr int START_SAMPLES = 3;          // a start needs this many cold samples in a row (one corrupted reading is not enough)
constexpr uint32_t STUCK_S = 3600;        // a reading that does not move by even 0.05 degrees for this long is a dead sensor
constexpr uint32_t UNKNOWN_OFF_S = 300;   // heater not heard this long while the sensor is gone or the room too warm -> send off anyway
}  // namespace smartcfg

enum SmartRun : uint8_t { SR_UNKNOWN, SR_OFF, SR_ON, SR_COOLING };
enum SmartAct : uint8_t { SA_NONE, SA_START, SA_MANUAL, SA_LEVEL, SA_OFF, SA_DISABLE };
enum SmartPhase : uint8_t { SP_WAIT, SP_STARTING, SP_WARMUP, SP_HEAT, SP_HOLD };

struct SmartIn {
  uint32_t now;      // seconds, monotonic
  bool sensorOk;     // a fresh DHT reading
  float temp;
  SmartRun run;
  bool manual;       // heater is in manual (level) mode
  int level;         // current level 1..6 when manual
  bool busy;         // a job is running or queued: do not add more
};

struct SmartOut {
  SmartAct act = SA_NONE;
  int level = 0;
  const char* why = "";
};

class Smart {
 public:
  bool enabled = false;
  float target = 20;

  void enable(float t, uint32_t now) {
    enabled = true;
    target = t;
    reset(now);
  }
  void disable() { enabled = false; }

  // for the controller to keep over a restart (see smartSave in control.cpp)
  bool wasOn() const { return wasOn_; }
  int tries() const { return startTries_; }
  void restore(bool wasOn, int tries) { wasOn_ = wasOn; startTries_ = tries; }

  SmartPhase phase() const { return phase_; }
  int wantedLevel() const { return lastLevelCmd_; }

  // degrees per minute over the recent window (after the last change), NAN when there is not enough data
  float slope(uint32_t now, uint32_t minSpan = smartcfg::WINDOW_MIN_S) const {
    uint32_t from = changedAt_ + smartcfg::SETTLE_S;
    if (now > smartcfg::WINDOW_MAX_S && now - smartcfg::WINDOW_MAX_S > from) from = now - smartcfg::WINDOW_MAX_S;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    uint32_t first = 0, last = 0;
    for (int i = 0; i < count_; i++) {
      const Sample& s = ring_[(head_ + RING - count_ + i) % RING];
      if (s.t < from) continue;
      if (!n) first = s.t;
      last = s.t;
      double x = (s.t - from) / 60.0;
      sx += x; sy += s.v; sxx += x * x; sxy += x * s.v;
      n++;
    }
    if (n < 8 || last - first + smartcfg::SAMPLE_S < minSpan) return NAN;
    double den = n * sxx - sx * sx;
    if (den < 1e-9) return NAN;
    return (float)((n * sxy - sx * sy) / den);
  }

  SmartOut step(const SmartIn& in) {
    using namespace smartcfg;
    SmartOut out;
    if (!enabled) return out;
    uint32_t now = in.now;

    if (in.sensorOk) {
      sensorSeenAt_ = now;
      if (!stuckAt_ || fabsf(in.temp - stuckRef_) >= 0.05f) { stuckRef_ = in.temp; stuckAt_ = now; }
      if (!count_ || now - lastSampleAt_ >= SAMPLE_S) {
        ring_[head_] = {now, in.temp};
        head_ = (head_ + 1) % RING;
        if (count_ < RING) count_++;
        lastSampleAt_ = now;
      }
    }
    bool sensorLost = now - sensorSeenAt_ > SENSOR_LOST_S;
    bool sensorStuck = !sensorLost && stuckAt_ && now - stuckAt_ > STUCK_S;
    bool sensorBad = sensorLost || sensorStuck;
    const char* sensorWhy = sensorStuck ? L_SM_STUCK : L_SM_NO_SENSOR;

    if (in.run == SR_UNKNOWN) {
      // No answer from the heater: no decisions, except one. A heater that smart believes is running, that is not heard
      // for a while while the sensor is gone or the room is clearly too warm, gets a power off (it is harmless when the
      // heater is off already, and the radio may well reach it).
      if (!unknownSince_) unknownSince_ = now;
      bool tooWarm = in.sensorOk && in.temp >= target + OVERSHOOT;
      if (wasOn_ && (sensorBad || tooWarm) && now - unknownSince_ > UNKNOWN_OFF_S && !in.busy && now - lastActAt_ >= ACT_GAP_S) {
        expectStop_ = true;
        return act(out, SA_OFF, 0, sensorBad ? sensorWhy : L_SM_OVERHEAT_H1, now, false);
      }
      return out;
    }
    unknownSince_ = 0;

    if (in.run == SR_ON) {
      wasOn_ = true;
      if (!onSince_) { onSince_ = now; changedAt_ = now; phase_ = SP_WARMUP; startTries_ = 0; startedAt_ = 0; }
      if (expectStop_ && now - lastActAt_ > 120) expectStop_ = false;  // the stop did not happen
    } else if (wasOn_) {
      if (!expectStop_) return disabled(out, L_SM_OFF_FOREIGN);
      if (in.run != SR_ON) { wasOn_ = false; expectStop_ = false; onSince_ = 0; lastOffAt_ = now; }  // the stop it asked for has happened
    }
    if (in.run != SR_ON) onSince_ = 0;
    if (stopWhy_ && in.run != SR_ON) return disabled(out, stopWhy_);  // smart gave up and the heater is stopped now

    if (in.run == SR_COOLING) {
      phase_ = SP_WAIT;
      lastOffAt_ = now;  // the pause counts from the end of the cooling
      return out;
    }

    if (in.run == SR_OFF) {
      phase_ = startedAt_ && now - startedAt_ < START_RETRY_S ? SP_STARTING : SP_WAIT;
      if (!in.sensorOk || sensorStuck || in.busy) return out;
      if (in.temp > target - START_DROP || !coldSamples()) return out;
      if (lastOffAt_ && now - lastOffAt_ < MIN_OFF_S) return out;
      if (startedAt_ && now - startedAt_ < START_RETRY_S) return out;
      if (startTries_ >= START_TRIES) return disabled(out, L_SM_NO_START);
      startTries_++;
      startedAt_ = now;
      expectStop_ = false;
      out.act = SA_START;
      out.why = L_SM_BELOW;
      return out;
    }

    // heater is on
    if (in.busy || now - lastActAt_ < ACT_GAP_S) return out;
    if (sensorBad) { expectStop_ = true; return act(out, SA_OFF, 0, sensorWhy, now, false); }
    if (stopWhy_) { expectStop_ = true; return act(out, SA_OFF, 0, stopWhy_, now, false); }  // keep asking until it is off
    if (!in.sensorOk) return out;
    if (now - onSince_ < STARTUP_S && phase_ == SP_WARMUP) return out;
    if (phase_ == SP_WARMUP) phase_ = SP_HEAT;

    float e = target - in.temp;  // positive: too cold
    if (!in.manual) {
      if (++manualTries_ > 5) {  // cannot take control of the heater that smart started: stop it, then switch smart off
        stopWhy_ = L_SM_NO_MANUAL;
        expectStop_ = true;
        return act(out, SA_OFF, 0, stopWhy_, now, false);
      }
      return act(out, SA_MANUAL, 0, L_SM_TO_MANUAL, now, true);
    }
    manualTries_ = 0;

    if (!initDone_) {
      int want = e >= 6 ? 4 : e >= 3 ? 3 : e > 0 ? 2 : 1;
      initDone_ = true;
      changedAt_ = now;
      if (in.level != want) return act(out, SA_LEVEL, want, L_SM_START_LEVEL, now, true);
    }

    phase_ = e <= 0 ? SP_HOLD : SP_HEAT;

    if (e <= -OVERSHOOT) {  // clearly too warm: do not wait for a window
      if (in.level > 1) return act(out, SA_LEVEL, in.level - 1, L_SM_OVERHEAT_DOWN, now, true);
      expectStop_ = true;
      return act(out, SA_OFF, 0, L_SM_OVERHEAT_H1, now, false);
    }

    uint32_t span = e >= 1.6f ? WINDOW_MIN_S : WINDOW_HOLD_S;  // a big gap wants a big rate, easy to see; near the target noise needs a longer look
    if (now - changedAt_ < SETTLE_S + span) return out;
    float s = slope(now, span);
    if (isnan(s)) return out;

    float wanted = e > 0 ? e / HORIZON_MIN : 0.0f;
    if (wanted > RISE_MAX) wanted = RISE_MAX;
    if (e > 0 && wanted < 0.008f) wanted = 0.008f;

    if (e <= 0 && in.level == 1 && s >= RISE_OFF) {
      expectStop_ = true;
      return act(out, SA_OFF, 0, L_SM_WARM_ENOUGH, now, false);
    }
    if (s < wanted - BAND && in.level < 6 && e > -0.3f) return act(out, SA_LEVEL, in.level + 1, L_SM_TOO_SLOW, now, true);
    if (s > wanted + BAND && in.level > 1) return act(out, SA_LEVEL, in.level - 1, L_SM_TOO_FAST, now, true);
    return out;
  }

 private:
  struct Sample { uint32_t t; float v; };
  static constexpr int RING = 64;
  Sample ring_[RING];
  int head_ = 0, count_ = 0;
  uint32_t lastSampleAt_ = 0, sensorSeenAt_ = 0;
  uint32_t onSince_ = 0, changedAt_ = 0, lastActAt_ = 0, lastOffAt_ = 0, startedAt_ = 0;
  bool wasOn_ = false, expectStop_ = false, initDone_ = false;
  const char* stopWhy_ = nullptr;  // set when smart decided to stop the heater and then switch itself off
  uint32_t unknownSince_ = 0, stuckAt_ = 0;
  float stuckRef_ = 0;
  int startTries_ = 0, manualTries_ = 0;

  // the last START_SAMPLES samples are all cold enough to start
  bool coldSamples() const {
    if (count_ < smartcfg::START_SAMPLES) return false;
    for (int i = 1; i <= smartcfg::START_SAMPLES; i++) {
      if (ring_[(head_ + RING - i) % RING].v > target - smartcfg::START_DROP) return false;
    }
    return true;
  }
  int lastLevelCmd_ = 0;
  SmartPhase phase_ = SP_WAIT;

  void reset(uint32_t now) {
    head_ = count_ = 0;
    onSince_ = lastActAt_ = lastOffAt_ = startedAt_ = 0;
    changedAt_ = now;
    sensorSeenAt_ = now;
    wasOn_ = expectStop_ = initDone_ = false;
    stopWhy_ = nullptr;
    unknownSince_ = stuckAt_ = 0;
    startTries_ = manualTries_ = 0;
    lastLevelCmd_ = 0;
    phase_ = SP_WAIT;
  }

  SmartOut disabled(SmartOut& out, const char* why) {
    enabled = false;
    out.act = SA_DISABLE;
    out.why = why;
    return out;
  }

  SmartOut act(SmartOut& out, SmartAct a, int level, const char* why, uint32_t now, bool restartWindow) {
    out.act = a;
    out.level = level;
    out.why = why;
    lastActAt_ = now;
    if (a == SA_LEVEL) lastLevelCmd_ = level;
    if (restartWindow) changedAt_ = now;
    return out;
  }
};
