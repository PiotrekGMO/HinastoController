// Simulation of the smart mode against a simple room: g++ -std=c++17 -O2 tools/smart-sim.cpp -o smart-sim && ./smart-sim
// The room: C * dT/dt = P - k * (T - Tout). The heater has a power per level, a first-order ramp, an ignition and a
// cooling phase, and it REPORTS "on" from the start of the ignition (the controller cannot tell "starting" apart).
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include "../sniffer/smart.h"

static double gSigma = 0.08;  // sensor noise, degrees (argv[1])

struct Room {
  double C = 0.5;       // kWh/K
  double k = 0.10;      // kW/K
  double T = 12, Tout = 5;
};

struct Heater {
  enum { OFF, IGNITING, ON, COOLING } st = OFF;
  bool manual = false;
  int level = 6;                         // what it was left at
  double P = 0;                          // kW, ramps
  double stateT = 0;
  static double power(int l) { static const double p[7] = {0, 0.8, 1.1, 1.4, 1.7, 2.0, 2.3}; return p[l]; }
  static double fuel(int l) { static const double f[7] = {0, 0.10, 0.14, 0.18, 0.22, 0.26, 0.30}; return f[l]; }  // l/h
};

struct Result { double reachedMin = -1, fuel = 0, maxOver = -99, minAfter = 99; int starts = 0, levelChanges = 0; };

static Result run(const char* name, double T0, double Tout, double k, double target, double hours, bool verbose) {
  Room room; room.T = T0; room.Tout = Tout; room.k = k;
  Heater h;
  Smart smart;
  std::mt19937 rng(7);
  std::normal_distribution<double> noise(0.0, gSigma);
  Result r;
  const double dt = 5.0;  // s, the controller loop
  double pendingAt = -1; int pendingLevel = 0; enum { NONE, LEVEL, MANUAL, OFFC, STARTC } pending = NONE;
  smart.enable((float)target, 0);
  double lastPrint = -1e9;
  if (verbose) printf("\n=== %s: start %.1f C, outside %.1f C, k=%.2f kW/K, target %.1f C\n  t[min]  room  level  heater   phase\n", name, T0, Tout, k, target);
  for (double t = 0; t < hours * 3600; t += dt) {
    // heater physics
    h.stateT += dt;
    if (h.st == Heater::IGNITING && h.stateT > 180) { h.st = Heater::ON; }
    if (h.st == Heater::COOLING && h.stateT > 240) { h.st = Heater::OFF; }
    double Ptarget = (h.st == Heater::ON) ? Heater::power(h.manual ? h.level : 3) : (h.st == Heater::IGNITING ? 0.3 : 0);
    h.P += (Ptarget - h.P) * (dt / 120.0);
    room.T += dt / 3600.0 * (h.P - room.k * (room.T - room.Tout)) / room.C;
    if (h.st == Heater::ON || h.st == Heater::IGNITING) r.fuel += Heater::fuel(h.manual ? h.level : 3) * dt / 3600.0;
    // jobs finish a little later
    if (pending != NONE && t >= pendingAt) {
      if (pending == LEVEL) { h.level = pendingLevel; r.levelChanges++; }
      if (pending == MANUAL) { h.manual = true; h.level = 1; }
      if (pending == OFFC) { h.st = Heater::COOLING; h.stateT = 0; }
      if (pending == STARTC) { h.st = Heater::IGNITING; h.stateT = 0; r.starts++; }
      pending = NONE;
    }
    // what the controller sees
    SmartIn in;
    in.now = (uint32_t)t;
    in.sensorOk = true;
    in.temp = (float)(std::round((room.T + noise(rng)) * 10) / 10);  // DHT22: 0.1 resolution
    in.run = h.st == Heater::OFF ? SR_OFF : h.st == Heater::COOLING ? SR_COOLING : SR_ON;  // IGNITING looks like ON
    in.manual = h.manual;
    in.level = h.level;
    in.busy = pending != NONE;
    SmartOut o = smart.step(in);
    switch (o.act) {
      case SA_START: pending = STARTC; pendingAt = t + 40; break;
      case SA_MANUAL: pending = MANUAL; pendingAt = t + 25; break;
      case SA_LEVEL: pending = LEVEL; pendingLevel = o.level; pendingAt = t + 6 * abs(o.level - h.level) + 5; break;
      case SA_OFF: pending = OFFC; pendingAt = t + 20; break;
      case SA_DISABLE: printf("  !! smart disabled at %.0f min: %s\n", t / 60, o.why); return r;
      default: break;
    }
    if (o.act != SA_NONE && verbose) printf("  %6.0f %5.1f  %s%d   -> %s\n", t / 60, room.T, o.act == SA_LEVEL ? "H" : "", o.act == SA_LEVEL ? o.level : 0, o.why);
    if (r.reachedMin < 0 && room.T >= target) r.reachedMin = t / 60;
    if (r.reachedMin >= 0) { if (room.T - target > r.maxOver) r.maxOver = room.T - target; if (room.T < r.minAfter) r.minAfter = room.T; }
    if (verbose && t - lastPrint >= 1200) {
      lastPrint = t;
      const char* hs[] = {"off", "start", "on", "cool"};
      const char* ph[] = {"wait", "start", "warm-up", "heat", "hold"};
      printf("  %6.0f %5.1f  %s%d     %-6s   %s\n", t / 60, room.T, h.manual ? "H" : "A", h.manual ? h.level : 0, hs[h.st], ph[smart.phase()]);
    }
  }
  return r;
}

int main(int argc, char** argv) {
  if (argc > 1) gSigma = atof(argv[1]);
  struct Case { const char* name; double T0, Tout, k, target; double hours; } cases[] = {
    {"frost: garage 10 C, outside 0 C", 10, 0, 0.09, 18, 8},
    {"mild: garage 14 C, outside 10 C", 14, 10, 0.09, 18, 8},
    {"hard frost, leaky: 5 C, outside -10 C", 5, -10, 0.12, 15, 8},
    {"warm: room 17 C, target 18 C, outside 12 C", 17, 12, 0.07, 18, 8},
  };
  for (auto& c : cases) {
    Result r = run(c.name, c.T0, c.Tout, c.k, c.target, c.hours, true);
    printf("  -> target reached after %.0f min, starts %d, level changes %d, fuel %.2f l / %.0f h, max over target %.1f C, min after reaching it %.1f C\n",
           r.reachedMin, r.starts, r.levelChanges, r.fuel, c.hours, r.maxOver, r.minAfter);
  }
  return 0;
}
